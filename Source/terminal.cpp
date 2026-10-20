#include <canopy/terminal.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <pwd.h>
#endif

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::string(v) : fallback;
}

std::string base_name(const std::string& path) {
    std::string name = fs::path(path).filename().string();
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.erase(name.size() - 4);
    return name;
}

#if defined(_WIN32)
std::wstring wide(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(0, n)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::wstring quoted(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    out += L"\"";
    return out;
}

void kill_descendants(DWORD root) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    std::multimap<DWORD, std::pair<DWORD, std::wstring>> children;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            children.emplace(entry.th32ParentProcessID, std::make_pair(entry.th32ProcessID, std::wstring(entry.szExeFile)));
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    std::vector<DWORD> doomed;
    std::vector<DWORD> queue{ root };
    while (!queue.empty()) {
        const DWORD parent = queue.back();
        queue.pop_back();
        const auto range = children.equal_range(parent);
        for (auto it = range.first; it != range.second; ++it) {
            const DWORD pid = it->second.first;
            if (pid == root || std::find(doomed.begin(), doomed.end(), pid) != doomed.end()) continue;
            std::wstring exe = it->second.second;
            for (wchar_t& c : exe) c = static_cast<wchar_t>(towlower(c));
            if (exe == L"conhost.exe") continue;
            doomed.push_back(pid);
            queue.push_back(pid);
        }
    }
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
        if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, *it)) {
            TerminateProcess(h, 0xC000013A);
            CloseHandle(h);
        }
    }
}
#else
std::string bash_rc_file() {
    const fs::path dir = fs::path(user_config_dir()) / "terminal";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path file = dir / "bashrc";
    std::ofstream out(file, std::ios::trunc);
    out << "[ -f /etc/bashrc ] && . /etc/bashrc\n"
           "[ -f \"$HOME/.bashrc\" ] && . \"$HOME/.bashrc\"\n"
           "if [ -n \"$CANOPY_PROMPT\" ]; then\n"
           "    PS1='\\[\\e[1;32m\\]\\u@\\h\\[\\e[0m\\]:\\[\\e[1;34m\\]\\w\\[\\e[0m\\]\\$ '\n"
           "fi\n"
           "__canopy_cwd() { printf '\\033]7;file://%s%s\\033\\\\' \"${HOSTNAME:-localhost}\" \"$PWD\"; }\n"
           "case \";$PROMPT_COMMAND;\" in *\";__canopy_cwd;\"*) ;; *) PROMPT_COMMAND=\"__canopy_cwd${PROMPT_COMMAND:+;$PROMPT_COMMAND}\" ;; esac\n";
    return file.string();
}

std::string user_name() {
    std::string name = env_or("USER", "");
    if (name.empty()) {
        if (const passwd* pw = getpwuid(getuid())) name = pw->pw_name;
    }
    return name.empty() ? std::string("user") : name;
}

std::string host_name() {
    char buf[256] = {};
    if (gethostname(buf, sizeof(buf) - 1) != 0) return "localhost";
    std::string host = buf;
    const std::size_t dot = host.find('.');
    if (dot != std::string::npos) host.erase(dot);
    return host.empty() ? std::string("localhost") : host;
}

bool executable(const std::string& path) { return !path.empty() && access(path.c_str(), X_OK) == 0; }

std::string find_in_path(const std::string& name) {
    if (name.find('/') != std::string::npos) return executable(name) ? name : std::string();
    const std::string path = env_or("PATH", "/usr/local/bin:/usr/bin:/bin");
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(':', start);
        const std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!dir.empty()) {
            const std::string candidate = (fs::path(dir) / name).string();
            if (executable(candidate)) return candidate;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return std::string();
}
#endif

std::size_t line_start(const std::string& text) {
    const std::size_t nl = text.rfind('\n');
    return nl == std::string::npos ? 0 : nl + 1;
}

} // namespace

#if defined(_WIN32)

namespace {

using ConsoleHandle = void*;
using CreateConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, ConsoleHandle*);
using ResizeConsoleFn = HRESULT(WINAPI*)(ConsoleHandle, COORD);
using CloseConsoleFn = void(WINAPI*)(ConsoleHandle);

struct ConsoleApi {
    CreateConsoleFn create = nullptr;
    ResizeConsoleFn resize = nullptr;
    CloseConsoleFn close = nullptr;
};

const ConsoleApi& console_api() {
    static const ConsoleApi api = [] {
        ConsoleApi a;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
            a.create = reinterpret_cast<CreateConsoleFn>(reinterpret_cast<void*>(GetProcAddress(k, "CreatePseudoConsole")));
            a.resize = reinterpret_cast<ResizeConsoleFn>(reinterpret_cast<void*>(GetProcAddress(k, "ResizePseudoConsole")));
            a.close = reinterpret_cast<CloseConsoleFn>(reinterpret_cast<void*>(GetProcAddress(k, "ClosePseudoConsole")));
        }
        return a;
    }();
    return api;
}

COORD console_size(unsigned columns, unsigned rows) {
    COORD c;
    c.X = static_cast<SHORT>(std::clamp<unsigned>(columns, 2, 32767));
    c.Y = static_cast<SHORT>(std::clamp<unsigned>(rows, 1, 32767));
    return c;
}

} // namespace

struct TerminalProcess::Native {
    HANDLE process = nullptr;
    HANDLE input = nullptr;
    HANDLE output = nullptr;
    ConsoleHandle console = nullptr;
    DWORD pid = 0;
    std::optional<int> code;
    std::thread reader;
    std::mutex mutex;
    std::string pending;
    std::atomic<bool> done{ false };
};

TerminalProcess::TerminalProcess() = default;

TerminalProcess::~TerminalProcess() { stop(); }

bool TerminalProcess::start(const TerminalOptions& options) {
    stop();
    m_error.clear();
    m_shell = options.shell.empty() ? env_or("ComSpec", "cmd.exe") : options.shell;
    const ConsoleApi& api = console_api();
    const bool pseudo = api.create && api.close;
    m_pseudo = pseudo;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = pseudo ? FALSE : TRUE;
    HANDLE in_read = nullptr;
    HANDLE in_write = nullptr;
    HANDLE out_read = nullptr;
    HANDLE out_write = nullptr;
    if (!CreatePipe(&in_read, &in_write, &sa, 0) || !CreatePipe(&out_read, &out_write, &sa, 0)) {
        m_error = "couldn't make the pipes for the terminal";
        return false;
    }
    if (!pseudo) {
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    }
    ConsoleHandle console = nullptr;
    if (pseudo && FAILED(api.create(console_size(options.columns, options.rows), in_read, out_write, 0, &console))) {
        CloseHandle(in_read);
        CloseHandle(in_write);
        CloseHandle(out_read);
        CloseHandle(out_write);
        m_error = "couldn't create a pseudo-console";
        return false;
    }
    std::wstring line = quoted(wide(m_shell));
    const std::string name = base_name(m_shell);
    if (name == "cmd" && !pseudo) line += L" /K chcp 65001>nul";
    else if ((name == "powershell" || name == "pwsh") && !pseudo) line += L" -NoLogo -NoExit";
    else if (name == "powershell" || name == "pwsh") line += L" -NoLogo";
    const std::wstring folder = wide(options.folder);
    PROCESS_INFORMATION pi{};
    BOOL ok = FALSE;
    if (pseudo) {
        STARTUPINFOEXW si{};
        si.StartupInfo.cb = sizeof(si);
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<unsigned char> list(size);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(list.data());
        if (InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &size) &&
            UpdateProcThreadAttribute(si.lpAttributeList, 0, static_cast<DWORD_PTR>(0x00020016), console, sizeof(console), nullptr, nullptr)) {
            ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, nullptr, folder.empty() ? nullptr : folder.c_str(), &si.StartupInfo, &pi);
        }
        DeleteProcThreadAttributeList(si.lpAttributeList);
    } else {
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = in_read;
        si.hStdOutput = out_write;
        si.hStdError = out_write;
        ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, folder.empty() ? nullptr : folder.c_str(), &si, &pi);
    }
    CloseHandle(in_read);
    CloseHandle(out_write);
    if (!ok) {
        if (console) api.close(console);
        CloseHandle(in_write);
        CloseHandle(out_read);
        m_error = "couldn't start " + m_shell;
        return false;
    }
    CloseHandle(pi.hThread);
    m_native = std::make_unique<Native>();
    m_native->process = pi.hProcess;
    m_native->pid = pi.dwProcessId;
    m_native->input = in_write;
    m_native->output = out_read;
    m_native->console = console;
    Native* native = m_native.get();
    native->reader = std::thread([native] {
        char buf[16384];
        while (true) {
            DWORD got = 0;
            if (!ReadFile(native->output, buf, sizeof(buf), &got, nullptr) || got == 0) break;
            std::lock_guard<std::mutex> lock(native->mutex);
            native->pending.append(buf, got);
        }
        native->done = true;
    });
    return true;
}

bool TerminalProcess::running() {
    if (!m_native || !m_native->process) return false;
    if (m_native->code) return false;
    if (WaitForSingleObject(m_native->process, 0) == WAIT_TIMEOUT) return true;
    DWORD code = 0;
    GetExitCodeProcess(m_native->process, &code);
    m_native->code = static_cast<int>(code);
    return false;
}

std::optional<int> TerminalProcess::exit_code() {
    running();
    return m_native ? m_native->code : std::nullopt;
}

std::string TerminalProcess::read() {
    std::string out;
    if (!m_native) return out;
    std::lock_guard<std::mutex> lock(m_native->mutex);
    out.swap(m_native->pending);
    return out;
}

bool TerminalProcess::write(const std::string& text) {
    if (!m_native || !m_native->input || !running()) return false;
    std::string data = text;
    if (!m_pseudo) {
        std::string converted;
        for (char c : data) {
            if (c == '\r') converted += "\r\n";
            else converted += c;
        }
        data.swap(converted);
    }
    DWORD wrote = 0;
    return WriteFile(m_native->input, data.data(), static_cast<DWORD>(data.size()), &wrote, nullptr) && wrote == data.size();
}

bool TerminalProcess::send_line(const std::string& line) { return write(line + "\r"); }

bool TerminalProcess::resize(unsigned columns, unsigned rows) {
    if (!m_native || !m_native->console) return false;
    const ConsoleApi& api = console_api();
    return api.resize && SUCCEEDED(api.resize(m_native->console, console_size(columns, rows)));
}

void TerminalProcess::interrupt() {
    if (!m_native) return;
    if (m_pseudo) write(std::string(1, '\x03'));
    else if (m_native->pid) kill_descendants(m_native->pid);
}

void TerminalProcess::end_of_input() {
    if (m_pseudo) write(std::string("\x1a\r"));
}

void TerminalProcess::stop() {
    if (!m_native) return;
    if (m_native->process) {
        if (WaitForSingleObject(m_native->process, 0) == WAIT_TIMEOUT) {
            kill_descendants(m_native->pid);
            TerminateProcess(m_native->process, 1);
        }
    }
    if (m_native->input) CloseHandle(m_native->input);
    m_native->input = nullptr;
    if (m_native->console) console_api().close(m_native->console);
    m_native->console = nullptr;
    if (!m_native->done && m_native->output) CancelIoEx(m_native->output, nullptr);
    if (m_native->reader.joinable()) m_native->reader.join();
    if (m_native->output) CloseHandle(m_native->output);
    if (m_native->process) CloseHandle(m_native->process);
    m_native.reset();
}

#else

struct TerminalProcess::Native {
    int master = -1;
    pid_t pid = -1;
    std::optional<int> code;
};

TerminalProcess::TerminalProcess() = default;

TerminalProcess::~TerminalProcess() { stop(); }

bool TerminalProcess::start(const TerminalOptions& options) {
    stop();
    m_error.clear();
    m_shell = options.shell;
    if (m_shell.empty()) m_shell = find_in_path("bash");
    if (m_shell.empty()) m_shell = env_or("SHELL", "/bin/sh");
    const std::string found = find_in_path(m_shell);
    if (found.empty()) {
        m_error = "couldn't find the shell '" + m_shell + "'";
        return false;
    }
    m_shell = found;
    const int master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
        if (master >= 0) close(master);
        m_error = "couldn't open a pseudo-terminal";
        return false;
    }
    const char* slave_name = ptsname(master);
    if (!slave_name) {
        close(master);
        m_error = "couldn't open a pseudo-terminal";
        return false;
    }
    const std::string slave_path = slave_name;
    winsize size{};
    size.ws_col = static_cast<unsigned short>(options.columns);
    size.ws_row = static_cast<unsigned short>(options.rows);
    ioctl(master, TIOCSWINSZ, &size);

    const std::string name = base_name(m_shell);
    std::vector<std::string> args{ m_shell };
    std::string prompt;
    if (name == "bash") {
        args.push_back("--rcfile");
        args.push_back(bash_rc_file());
        args.push_back("-i");
    } else {
        args.push_back("-i");
        prompt = user_name() + "@" + host_name() + ":$PWD$ ";
    }
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(master);
        m_error = "couldn't start " + m_shell;
        return false;
    }
    if (pid == 0) {
        for (int sig : { SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGCHLD, SIGPIPE, SIGHUP, SIGTERM, SIGALRM, SIGUSR1, SIGUSR2 }) signal(sig, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);
        setsid();
        const int slave = open(slave_path.c_str(), O_RDWR);
        if (slave < 0) _exit(127);
#ifdef TIOCSCTTY
        ioctl(slave, TIOCSCTTY, 0);
#endif
        termios t{};
        if (tcgetattr(slave, &t) == 0) {
            t.c_lflag |= ECHO | ICANON | ISIG;
            t.c_oflag |= OPOST | ONLCR;
            tcsetattr(slave, TCSANOW, &t);
        }
        dup2(slave, 0);
        dup2(slave, 1);
        dup2(slave, 2);
        if (slave > 2) close(slave);
        close(master);
        if (!options.folder.empty() && chdir(options.folder.c_str()) != 0) {
            const char* home = std::getenv("HOME");
            const int moved = home ? chdir(home) : -1;
            (void)moved;
        }
        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);
        setenv("TERM_PROGRAM", "canopy", 1);
        setenv("CANOPY_TERMINAL", "1", 1);
        unsetenv("COLUMNS");
        unsetenv("LINES");
        if (options.canopy_prompt) setenv("CANOPY_PROMPT", "1", 1);
        else unsetenv("CANOPY_PROMPT");
        if (options.canopy_prompt && !prompt.empty()) setenv("PS1", prompt.c_str(), 1);
        execv(argv[0], argv.data());
        _exit(127);
    }
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
    fcntl(master, F_SETFD, FD_CLOEXEC);
    m_native = std::make_unique<Native>();
    m_native->master = master;
    m_native->pid = pid;
    return true;
}

bool TerminalProcess::running() {
    if (!m_native || m_native->pid <= 0) return false;
    if (m_native->code) return false;
    int status = 0;
    const pid_t r = waitpid(m_native->pid, &status, WNOHANG);
    if (r == 0) return true;
    if (r == m_native->pid) m_native->code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    else m_native->code = -1;
    return false;
}

std::optional<int> TerminalProcess::exit_code() {
    running();
    return m_native ? m_native->code : std::nullopt;
}

std::string TerminalProcess::read() {
    std::string out;
    if (!m_native || m_native->master < 0) return out;
    char buf[16384];
    for (int rounds = 0; rounds < 64; ++rounds) {
        const ssize_t n = ::read(m_native->master, buf, sizeof(buf));
        if (n > 0) {
            out.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        break;
    }
    return out;
}

bool TerminalProcess::write(const std::string& text) {
    if (!m_native || m_native->master < 0 || !running()) return false;
    std::size_t done = 0;
    for (int tries = 0; done < text.size() && tries < 200; ++tries) {
        const ssize_t n = ::write(m_native->master, text.data() + done, text.size() - done);
        if (n > 0) done += static_cast<std::size_t>(n);
        else if (n < 0 && errno != EAGAIN && errno != EINTR) return false;
        else usleep(1000);
    }
    return done == text.size();
}

bool TerminalProcess::send_line(const std::string& line) { return write(line + "\r"); }

bool TerminalProcess::resize(unsigned columns, unsigned rows) {
    if (!m_native || m_native->master < 0) return false;
    winsize size{};
    size.ws_col = static_cast<unsigned short>(std::clamp<unsigned>(columns, 1, 65535));
    size.ws_row = static_cast<unsigned short>(std::clamp<unsigned>(rows, 1, 65535));
    return ioctl(m_native->master, TIOCSWINSZ, &size) == 0;
}

void TerminalProcess::interrupt() { write(std::string(1, '\x03')); }

void TerminalProcess::end_of_input() { write(std::string(1, '\x04')); }

void TerminalProcess::stop() {
    if (!m_native) return;
    if (m_native->pid > 0 && !m_native->code) {
        kill(-m_native->pid, SIGHUP);
        kill(m_native->pid, SIGHUP);
        int status = 0;
        bool gone = false;
        for (int i = 0; i < 20 && !gone; ++i) {
            if (waitpid(m_native->pid, &status, WNOHANG) == m_native->pid) gone = true;
            else usleep(5000);
        }
        if (!gone) {
            kill(-m_native->pid, SIGKILL);
            kill(m_native->pid, SIGKILL);
            waitpid(m_native->pid, &status, 0);
        }
    }
    if (m_native->master >= 0) close(m_native->master);
    m_native.reset();
}

#endif

void terminal_feed(TerminalScreen& screen, const std::string& data) {
    std::string s = screen.pending + data;
    screen.pending.clear();
    std::string& out = screen.text;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0x1B) {
            if (i + 1 >= s.size()) {
                screen.pending = s.substr(i);
                return;
            }
            const char next = s[i + 1];
            if (next == '[') {
                std::size_t j = i + 2;
                while (j < s.size() && !(static_cast<unsigned char>(s[j]) >= 0x40 && static_cast<unsigned char>(s[j]) <= 0x7E)) ++j;
                if (j >= s.size()) {
                    screen.pending = s.substr(i);
                    return;
                }
                const std::string params = s.substr(i + 2, j - i - 2);
                const char final = s[j];
                if (final == 'K' && params == "2") {
                    out.erase(line_start(out));
                } else if (final == 'J' && (params == "2" || params == "3")) {
                    out.clear();
                }
                i = j + 1;
                continue;
            }
            if (next == ']' || next == 'P' || next == 'X' || next == '^' || next == '_') {
                std::size_t j = i + 2;
                bool done = false;
                while (j < s.size()) {
                    if (s[j] == '\x07') {
                        ++j;
                        done = true;
                        break;
                    }
                    if (s[j] == '\x1B' && j + 1 < s.size() && s[j + 1] == '\\') {
                        j += 2;
                        done = true;
                        break;
                    }
                    ++j;
                }
                if (!done) {
                    screen.pending = s.substr(i);
                    return;
                }
                i = j;
                continue;
            }
            if (next == '(' || next == ')' || next == '*' || next == '+' || next == '#' || next == '%') {
                if (i + 2 >= s.size()) {
                    screen.pending = s.substr(i);
                    return;
                }
                i += 3;
                continue;
            }
            i += 2;
            continue;
        }
        if (c == '\r') {
            std::size_t j = i;
            while (j < s.size() && s[j] == '\r') ++j;
            if (j >= s.size()) {
                screen.pending = "\r";
                return;
            }
            if (s[j] == '\n') {
                out += '\n';
                i = j + 1;
                continue;
            }
            out.erase(line_start(out));
            i = j;
            continue;
        }
        if (c == '\n') {
            out += '\n';
            ++i;
            continue;
        }
        if (c == '\b') {
            if (!out.empty() && out.back() != '\n') {
                out.pop_back();
                while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
            }
            ++i;
            continue;
        }
        if (c == '\t') {
            out += '\t';
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7F) {
            ++i;
            continue;
        }
        out += static_cast<char>(c);
        ++i;
    }
}

std::string terminal_last_line(const TerminalScreen& screen) { return screen.text.substr(line_start(screen.text)); }

std::string terminal_finished_lines(const TerminalScreen& screen) {
    const std::size_t start = line_start(screen.text);
    if (start == 0) return std::string();
    return screen.text.substr(0, start - 1);
}

void terminal_trim(TerminalScreen& screen, std::size_t keep) {
    if (screen.text.size() <= keep) return;
    std::size_t cut = screen.text.size() - keep;
    const std::size_t nl = screen.text.find('\n', cut);
    cut = nl == std::string::npos ? cut : nl + 1;
    screen.text.erase(0, cut);
}

std::string terminal_shell(const Settings& settings) {
    std::string shell = settings.get_or("terminal", "shell", "auto");
    if (shell == "auto" || shell == "default") shell.clear();
    return shell;
}

std::string terminal_prompt_preview(const std::string& folder) {
#if defined(_WIN32)
    std::string shown = fs::path(folder).make_preferred().string();
    return shown + ">";
#else
    std::string shown = folder;
    const std::string home = env_or("HOME", "");
    if (!home.empty() && (shown == home || shown.rfind(home + "/", 0) == 0)) shown = "~" + shown.substr(home.size());
    return user_name() + "@" + host_name() + ":" + shown + "$ ";
#endif
}

} // namespace canopy
