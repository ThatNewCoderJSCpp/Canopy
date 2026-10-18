#include <canopy/build.hpp>

#include <cctype>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace canopy {

namespace {

void append(std::vector<std::string>& to, const std::vector<std::string>& from) {
    to.insert(to.end(), from.begin(), from.end());
}

std::vector<std::string> msvc_flags(const std::vector<std::string>& flags) {
    std::vector<std::string> out;
    for (const std::string& f : flags) {
        if (!f.empty() && f[0] == '/') out.push_back(f);
    }
    return out;
}

std::string missing_tool(const Toolchain& toolchain, const std::string& key) {
    for (const ToolProblem& p : toolchain.problems) {
        if (p.key == key) return p.message;
    }
    return "no " + key + " tool found";
}

} // namespace

std::vector<std::string> split_flags(const std::string& flags) {
    std::vector<std::string> out;
    std::string current;
    bool in_quotes = false;
    bool has_token = false;
    for (const char c : flags) {
        if (c == '"') {
            in_quotes = !in_quotes;
            has_token = true;
        } else if (!in_quotes && (c == ' ' || c == '\t')) {
            if (has_token) out.push_back(current);
            current.clear();
            has_token = false;
        } else {
            current += c;
            has_token = true;
        }
    }
    if (has_token) out.push_back(current);
    return out;
}

std::string quote_argument(const std::string& argument) {
    if (!argument.empty() && argument.find_first_of(" \t\"'\\$`&|;<>()*?") == std::string::npos) return argument;
#if defined(_WIN32)
    if (argument.find_first_of(" \t\"&|<>()^") == std::string::npos && !argument.empty()) return argument;
    std::string out = "\"";
    for (const char c : argument) {
        if (c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
#else
    std::string out = "'";
    for (const char c : argument) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

std::string command_line(const std::vector<std::string>& command) {
    std::string out;
    for (const std::string& a : command) {
        if (!out.empty()) out += ' ';
        out += quote_argument(a);
    }
    return out;
}

BuildMode build_mode(const Settings& settings, const std::optional<BuildMode>& requested) {
    if (requested) return *requested;
    return settings.get_or("build", "mode", "debug") == "release" ? BuildMode::Release : BuildMode::Debug;
}

std::string default_output_path(const BuildRequest& request, const Settings& settings) {
    const fs::path file(request.file);
    const fs::path base = request.workspace_root.empty() ? file.parent_path() : fs::path(request.workspace_root);
    const fs::path dir = base / settings.get_or("build", "output-dir", "build");
    return (dir / with_exe_suffix(file.stem().string())).string();
}

BuildPlan plan_build(const BuildRequest& request, const Settings& settings, const Toolchain& toolchain) {
    BuildPlan plan;
    plan.language = language_for_path(request.file);
    if (!plan.language) {
        const std::string ext = fs::path(request.file).extension().string();
        plan.error = "Canopy doesn't know how to build '" + (ext.empty() ? request.file : ext) + "' files yet";
        return plan;
    }
    const Language& lang = *plan.language;
    if (lang.build == BuildKind::None) {
        plan.error = lang.name + " files aren't built on their own; build the .grove document that uses this file";
        return plan;
    }
    if (is_header_path(lang, request.file)) {
        plan.error = fs::path(request.file).filename().string() + " is a header; build a file that includes it";
        return plan;
    }

    const std::string section = "language." + lang.key;
    const BuildMode mode = build_mode(settings, request.mode);
    const std::vector<std::string> flags = split_flags(settings.get_or(section, "flags", ""));
    const std::vector<std::string> mode_flags = split_flags(settings.get_or(section, mode == BuildMode::Release ? "release-flags" : "debug-flags", ""));
    plan.output = request.output.empty() ? default_output_path(request, settings) : request.output;

    switch (lang.build) {
    case BuildKind::CCompiler:
    case BuildKind::CppCompiler: {
        const std::string key = lang.build == BuildKind::CCompiler ? "c" : "cpp";
        const Tool* tool = toolchain.get(key);
        if (!tool) {
            plan.error = missing_tool(toolchain, key);
            return plan;
        }
        const std::string standard = settings.get_or(section, "standard", "");
        plan.command.push_back(tool->path);
        if (tool->family == CompilerFamily::Msvc) {
            plan.command.push_back("/nologo");
            if (!standard.empty()) plan.command.push_back("/std:" + standard);
            if (lang.build == BuildKind::CppCompiler) plan.command.push_back("/EHsc");
            plan.command.push_back(mode == BuildMode::Release ? "/O2" : "/Zi");
            append(plan.command, msvc_flags(flags));
            plan.command.push_back(request.file);
            plan.command.push_back("/Fe:" + plan.output);
        } else {
            if (!standard.empty()) plan.command.push_back("-std=" + standard);
            append(plan.command, flags);
            append(plan.command, mode_flags);
            plan.command.push_back(request.file);
            plan.command.push_back("-o");
            plan.command.push_back(plan.output);
        }
        break;
    }
    case BuildKind::Walnut: {
        const Tool* walnut = toolchain.get("walnut");
        if (!walnut) {
            plan.error = missing_tool(toolchain, "walnut");
            return plan;
        }
        const Tool* cpp = toolchain.get("cpp");
        if (!cpp) {
            plan.error = "Walnut builds through a C++ compiler: " + missing_tool(toolchain, "cpp");
            return plan;
        }
        plan.command = { walnut->path, "build", request.file, "-o", plan.output, "--cxx", cpp->path };
        append(plan.command, mode_flags);
        append(plan.command, flags);
        break;
    }
    case BuildKind::Grove: {
        const Tool* grove = toolchain.get("grove");
        if (!grove) {
            plan.error = missing_tool(toolchain, "grove");
            return plan;
        }
        plan.command = { grove->path, "build", request.file, "-o", plan.output };
        append(plan.command, flags);
        break;
    }
    case BuildKind::None:
        break;
    }
    return plan;
}

int run_command(const std::vector<std::string>& command) {
    if (command.empty()) return -1;
#if defined(_WIN32)
    const std::string line = "\"" + command_line(command) + "\"";
    return std::system(line.c_str());
#else
    const int status = std::system(command_line(command).c_str());
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}

int run_command_capture(const std::vector<std::string>& command, std::string& output) {
    if (command.empty()) return -1;
#if defined(_WIN32)
    const std::string line = "\"" + command_line(command) + " 2>&1\"";
    FILE* pipe = _popen(line.c_str(), "r");
#else
    const std::string line = command_line(command) + " 2>&1";
    FILE* pipe = popen(line.c_str(), "r");
#endif
    if (!pipe) return -1;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, n);
#if defined(_WIN32)
    return _pclose(pipe);
#else
    const int status = pclose(pipe);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}

int run_command_capture_in(const std::vector<std::string>& command, const std::string& working_dir, std::string& output) {
    if (working_dir.empty()) return run_command_capture(command, output);
    if (command.empty()) return -1;
#if defined(_WIN32)
    const std::string line = "\"cd /d " + quote_argument(working_dir) + " && " + command_line(command) + " 2>&1\"";
    FILE* pipe = _popen(line.c_str(), "r");
#else
    const std::string line = "cd " + quote_argument(working_dir) + " && " + command_line(command) + " 2>&1";
    FILE* pipe = popen(line.c_str(), "r");
#endif
    if (!pipe) return -1;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, n);
#if defined(_WIN32)
    return _pclose(pipe);
#else
    const int status = pclose(pipe);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}

int run_command_capture_input(const std::vector<std::string>& command, const std::string& input, std::string& output) {
    if (command.empty()) return -1;
    static std::atomic<unsigned> serial{ 0 };
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec) / ("canopy-input-" + std::to_string(std::hash<std::thread::id>()(std::this_thread::get_id())) + "-" + std::to_string(++serial) + ".txt");
    {
        std::FILE* f = std::fopen(temp.string().c_str(), "wb");
        if (!f) return -1;
        std::fwrite(input.data(), 1, input.size(), f);
        std::fclose(f);
    }
#if defined(_WIN32)
    const std::string line = "\"" + command_line(command) + " < " + quote_argument(temp.string()) + " 2>NUL\"";
    FILE* pipe = _popen(line.c_str(), "r");
#else
    const std::string line = command_line(command) + " < " + quote_argument(temp.string()) + " 2>/dev/null";
    FILE* pipe = popen(line.c_str(), "r");
#endif
    if (!pipe) {
        fs::remove(temp, ec);
        return -1;
    }
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) output.append(buffer, n);
#if defined(_WIN32)
    const int code = _pclose(pipe);
#else
    const int status = pclose(pipe);
    const int code = status == -1 ? -1 : WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
    fs::remove(temp, ec);
    return code;
}

namespace {

std::optional<CompilerMessage> walnut_message(const std::vector<std::string>& lines, std::size_t i) {
    const std::string& line = lines[i];
    if (line.empty() || line[0] != '[') return std::nullopt;
    const std::size_t close = line.find("] ");
    if (close == std::string::npos) return std::nullopt;
    const std::string phase = line.substr(1, close - 1);
    const std::string rest = line.substr(close + 2);
    for (std::size_t colon = rest.find(':'); colon != std::string::npos; colon = rest.find(':', colon + 1)) {
        std::size_t p = colon + 1;
        while (p < rest.size() && std::isdigit(static_cast<unsigned char>(rest[p]))) ++p;
        if (p == colon + 1 || p + 1 >= rest.size() || rest[p] != ':' || rest[p + 1] != ' ') continue;
        CompilerMessage m;
        m.file = rest.substr(0, colon);
        m.line = static_cast<std::size_t>(std::stoul(rest.substr(colon + 1, p - colon - 1)));
        m.severity = phase.find("warning") != std::string::npos ? "warning" : "error";
        m.message = rest.substr(p + 2);
        const std::size_t on = m.message.find(" on line ");
        if (on != std::string::npos) {
            std::size_t q = on + 9;
            while (q < m.message.size() && std::isdigit(static_cast<unsigned char>(m.message[q]))) ++q;
            if (q < m.message.size() && m.message[q] == ':') m.message.erase(on, q - on);
        }
        for (std::size_t k = i + 1; k < lines.size() && k <= i + 3; ++k) {
            const std::string& caret = lines[k];
            const std::size_t bar = caret.find('|');
            const std::size_t mark = caret.find('^');
            if (bar == std::string::npos || mark == std::string::npos || mark < bar + 2) continue;
            bool only = true;
            for (std::size_t c = 0; c < bar; ++c) only = only && caret[c] == ' ';
            if (!only) continue;
            m.column = mark - bar - 1;
            break;
        }
        return m;
    }
    return std::nullopt;
}

} // namespace

std::vector<CompilerMessage> parse_compiler_messages(const std::string& output) {
    std::vector<CompilerMessage> out;
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < output.size()) {
        std::size_t end = output.find('\n', start);
        if (end == std::string::npos) end = output.size();
        std::string line = output.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (std::optional<CompilerMessage> walnut = walnut_message(lines, i)) {
            out.push_back(std::move(*walnut));
            continue;
        }
        for (const char* severity : { ": error: ", ": warning: ", ": fatal error: ", ": note: " }) {
            const std::size_t at = line.find(severity);
            if (at == std::string::npos) continue;
            std::string where = line.substr(0, at);
            CompilerMessage m;
            m.severity = std::string(severity).substr(2, std::string(severity).size() - 4);
            if (m.severity == "fatal error") m.severity = "error";
            m.message = line.substr(at + std::string(severity).size());
            std::vector<std::string> parts;
            std::size_t cut = where.size();
            for (int k = 0; k < 2; ++k) {
                const std::size_t colon = where.rfind(':', cut - 1);
                if (colon == std::string::npos || colon == 0) break;
                const std::string number = where.substr(colon + 1, cut - colon - 1);
                if (number.empty() || number.find_first_not_of("0123456789") != std::string::npos) break;
                parts.insert(parts.begin(), number);
                cut = colon;
            }
            if (parts.empty()) break;
            m.file = where.substr(0, cut);
            m.line = static_cast<std::size_t>(std::stoul(parts[0]));
            if (parts.size() > 1) m.column = static_cast<std::size_t>(std::stoul(parts[1]));
            out.push_back(std::move(m));
            break;
        }
    }
    return out;
}

struct BackgroundJob::State {
    std::mutex mutex;
    std::string output;
    std::optional<int> result;
    std::atomic<bool> running{ false };
    std::thread thread;
};

BackgroundJob::BackgroundJob() : m_state(std::make_shared<State>()) {}

BackgroundJob::~BackgroundJob() {
    if (m_state && m_state->thread.joinable()) m_state->thread.detach();
}

bool BackgroundJob::start(std::vector<std::vector<std::string>> steps) {
    return start(std::move(steps), std::string());
}

bool BackgroundJob::start(std::vector<std::vector<std::string>> steps, const std::string& working_dir) {
    if (m_state->running) return false;
    if (m_state->thread.joinable()) m_state->thread.join();
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->result.reset();
    }
    m_state->running = true;
    std::shared_ptr<State> state = m_state;
    state->thread = std::thread([state, steps = std::move(steps), working_dir] {
        int code = 0;
        for (const std::vector<std::string>& step : steps) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->output += "> " + command_line(step) + "\n";
            }
            std::string text;
            code = run_command_capture_in(step, working_dir, text);
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->output += text;
                if (!text.empty() && text.back() != '\n') state->output += '\n';
            }
            if (code != 0) break;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->result = code;
        }
        state->running = false;
    });
    return true;
}

bool BackgroundJob::running() const { return m_state->running; }

std::string BackgroundJob::take_output() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::string out = std::move(m_state->output);
    m_state->output.clear();
    return out;
}

std::optional<int> BackgroundJob::take_result() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::optional<int> r = m_state->result;
    m_state->result.reset();
    return r;
}

} // namespace canopy
