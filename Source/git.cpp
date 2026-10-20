#include "canopy/git.hpp"

#include "canopy/build.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace canopy {

namespace {

bool is_git(const std::vector<std::string>& command) {
    if (command.empty()) return false;
    const std::string name = fs::path(command.front()).stem().string();
    return name == "git";
}

std::string folder_prefix(const std::string& folder) {
    if (folder.empty()) return std::string();
#if defined(_WIN32)
    return "cd /d " + quote_argument(folder) + " && ";
#else
    return "cd " + quote_argument(folder) + " && ";
#endif
}

std::mutex& askpass_mutex() {
    static std::mutex m;
    return m;
}

std::string& askpass_program() {
    static std::string program;
    return program;
}

std::string git_environment(bool interactive) {
    std::string askpass;
    if (interactive) {
        std::lock_guard<std::mutex> lock(askpass_mutex());
        askpass = askpass_program();
    }
#if defined(_WIN32)
    std::string out = "set \"GIT_TERMINAL_PROMPT=0\" && set \"GIT_OPTIONAL_LOCKS=0\" && ";
    if (!askpass.empty()) out += "set \"GIT_ASKPASS=" + askpass + "\" && set \"SSH_ASKPASS=" + askpass + "\" && set \"SSH_ASKPASS_REQUIRE=prefer\" && set \"CANOPY_ASKPASS=1\" && ";
    return out;
#else
    std::string out = "GIT_TERMINAL_PROMPT=0 GIT_OPTIONAL_LOCKS=0 ";
    if (!askpass.empty()) out += "GIT_ASKPASS=" + quote_argument(askpass) + " SSH_ASKPASS=" + quote_argument(askpass) + " SSH_ASKPASS_REQUIRE=prefer CANOPY_ASKPASS=1 ";
    return out;
#endif
}

std::string error_redirect(bool merge_errors) {
#if defined(_WIN32)
    return merge_errors ? " 2>&1" : " 2>NUL";
#else
    return merge_errors ? " 2>&1 </dev/null" : " 2>/dev/null </dev/null";
#endif
}

std::string wrap_line(const std::string& line) {
#if defined(_WIN32)
    return "\"" + line + "\"";
#else
    return line;
#endif
}

template <typename Sink>
int run_line(const std::string& line, Sink&& sink) {
#if defined(_WIN32)
    FILE* pipe = _popen(line.c_str(), "r");
#else
    FILE* pipe = popen(line.c_str(), "r");
#endif
    if (!pipe) return -1;
    char buffer[4096];
#if defined(_WIN32)
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) sink(buffer, n);
    return _pclose(pipe);
#else
    const int fd = fileno(pipe);
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof(buffer));
        if (n > 0) {
            sink(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
    const int status = pclose(pipe);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
#endif
}

std::string shell_line(const std::string& line, const std::string& folder) {
#if defined(_WIN32)
    return wrap_line(folder_prefix(folder) + line + " 2>&1");
#else
    return folder_prefix(folder) + "( " + line + " ) 2>&1 </dev/null";
#endif
}

std::vector<std::string> split_on(const std::string& text, char separator) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = text.find(separator, start);
        if (at == std::string::npos) {
            out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    return out;
}

std::string strip_cr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

long long to_number(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    return end == s.c_str() ? 0 : v;
}

std::string after_fields(const std::string& line, int fields) {
    std::size_t at = 0;
    for (int i = 0; i < fields; ++i) {
        at = line.find(' ', at);
        if (at == std::string::npos) return std::string();
        ++at;
    }
    return line.substr(at);
}

std::vector<std::string> split_text_lines(const std::string& text) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t at = text.find('\n', start);
        if (at == std::string::npos) {
            out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    return out;
}

void parse_hunk_header(const std::string& line, long long& old_start, long long& old_count, long long& new_start, long long& new_count) {
    old_start = new_start = 0;
    old_count = new_count = 1;
    const std::size_t minus = line.find('-');
    const std::size_t plus = line.find('+', minus == std::string::npos ? 0 : minus);
    auto read_pair = [&](std::size_t at, long long& start, long long& count) {
        if (at == std::string::npos) return;
        std::size_t end = at + 1;
        while (end < line.size() && (std::isdigit(static_cast<unsigned char>(line[end])) || line[end] == ',')) ++end;
        const std::string part = line.substr(at + 1, end - at - 1);
        const std::size_t comma = part.find(',');
        if (comma == std::string::npos) {
            start = to_number(part);
            count = 1;
        } else {
            start = to_number(part.substr(0, comma));
            count = to_number(part.substr(comma + 1));
        }
    };
    read_pair(minus, old_start, old_count);
    read_pair(plus, new_start, new_count);
}

} // namespace

void set_git_askpass(const std::string& program) {
    std::lock_guard<std::mutex> lock(askpass_mutex());
    askpass_program() = program;
}

std::string git_askpass() {
    std::lock_guard<std::mutex> lock(askpass_mutex());
    return askpass_program();
}

std::string git_command_line(const std::vector<std::string>& command, const std::string& folder, bool merge_errors) {
    std::string line = folder_prefix(folder);
    if (is_git(command)) line += git_environment(merge_errors);
    line += command_line(command);
    line += error_redirect(merge_errors);
    return wrap_line(line);
}

CommandOutput run_in_folder(const std::vector<std::string>& command, const std::string& folder, bool merge_errors) {
    CommandOutput out;
    if (command.empty()) return out;
    out.code = run_line(git_command_line(command, folder, merge_errors), [&](const char* data, std::size_t n) { out.text.append(data, n); });
    return out;
}

CommandOutput run_shell_in_folder(const std::string& line, const std::string& folder) {
    CommandOutput out;
    if (line.empty()) return out;
    out.code = run_line(shell_line(line, folder), [&](const char* data, std::size_t n) { out.text.append(data, n); });
    return out;
}

struct CommandBatch::State {
    std::mutex mutex;
    std::string output;
    std::optional<std::vector<CommandOutput>> results;
    std::vector<std::string> keys;
    std::atomic<bool> running{ false };
    std::thread thread;
};

CommandBatch::CommandBatch() : m_state(std::make_shared<State>()) {}

CommandBatch::~CommandBatch() {
    if (m_state && m_state->thread.joinable()) m_state->thread.detach();
}

bool CommandBatch::start(std::vector<Step> steps, const std::string& folder, bool stop_on_error, bool merge_errors) {
    if (m_state->running) return false;
    if (m_state->thread.joinable()) m_state->thread.join();
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->results.reset();
        m_state->keys.clear();
        for (const Step& s : steps) m_state->keys.push_back(s.key);
    }
    m_state->running = true;
    std::shared_ptr<State> state = m_state;
    state->thread = std::thread([state, steps = std::move(steps), folder, stop_on_error, merge_errors] {
        std::vector<CommandOutput> results;
        for (const Step& step : steps) {
            CommandOutput result;
            if (!step.quiet) {
                std::string title = step.label;
                if (title.empty()) title = step.internal ? std::string("(internal)") : !step.shell.empty() ? step.shell : command_line(step.command);
                std::lock_guard<std::mutex> lock(state->mutex);
                state->output += "> " + title + "\n";
            }
            auto sink = [&](const char* data, std::size_t n) {
                result.text.append(data, n);
                if (step.quiet) return;
                std::lock_guard<std::mutex> lock(state->mutex);
                state->output.append(data, n);
            };
            if (step.internal) {
                std::string text;
                bool ok = false;
                try {
                    ok = step.internal(text);
                } catch (const std::exception& e) {
                    text += e.what();
                    ok = false;
                }
                if (!text.empty() && text.back() != '\n') text += '\n';
                sink(text.data(), text.size());
                result.code = ok ? 0 : 1;
            } else if (!step.shell.empty()) {
                result.code = run_line(shell_line(step.shell, folder), sink);
            } else if (!step.command.empty()) {
                result.code = run_line(git_command_line(step.command, folder, merge_errors || !step.quiet), sink);
            } else {
                result.code = 0;
            }
            if (!step.quiet && !result.text.empty() && result.text.back() != '\n') {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->output += '\n';
            }
            const bool failed = result.code != 0 && !step.may_fail;
            results.push_back(std::move(result));
            if (failed && stop_on_error) break;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->results = std::move(results);
        }
        state->running = false;
    });
    return true;
}

bool CommandBatch::running() const { return m_state->running; }

std::string CommandBatch::take_output() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::string out = std::move(m_state->output);
    m_state->output.clear();
    return out;
}

std::optional<std::vector<CommandOutput>> CommandBatch::take_results() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::optional<std::vector<CommandOutput>> r = std::move(m_state->results);
    m_state->results.reset();
    return r;
}

std::vector<std::string> CommandBatch::keys() const {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    return m_state->keys;
}

std::string git_log_format() {
    return "%x1e%H%x1f%h%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%D%x1f%s";
}

GitStatus parse_git_status(const std::string& text) {
    GitStatus status;
    if (text.empty()) return status;
    status.repo = true;
    const std::vector<std::string> entries = split_on(text, '\0');
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::string& e = entries[i];
        if (e.empty()) continue;
        if (e.rfind("# ", 0) == 0) {
            if (e.rfind("# branch.oid ", 0) == 0) {
                status.head = e.substr(13);
                if (status.head == "(initial)") {
                    status.initial = true;
                    status.head.clear();
                }
            } else if (e.rfind("# branch.head ", 0) == 0) {
                status.branch = e.substr(14);
                if (status.branch == "(detached)") {
                    status.detached = true;
                    status.branch.clear();
                }
            } else if (e.rfind("# branch.upstream ", 0) == 0) {
                status.upstream = e.substr(18);
            } else if (e.rfind("# branch.ab ", 0) == 0) {
                const std::vector<std::string> parts = split_on(e.substr(12), ' ');
                for (const std::string& p : parts) {
                    if (p.size() > 1 && p[0] == '+') status.ahead = static_cast<int>(to_number(p.substr(1)));
                    if (p.size() > 1 && p[0] == '-') status.behind = static_cast<int>(to_number(p.substr(1)));
                }
            }
            continue;
        }
        GitFile file;
        const char kind = e[0];
        if (kind == '?') {
            if (e.size() < 3) continue;
            file.path = e.substr(2);
            file.untracked = true;
            file.worktree = '?';
        } else if (kind == '!') {
            if (e.size() < 3) continue;
            std::string path = e.substr(2);
            if (!path.empty() && path.back() == '/') path.pop_back();
            if (!path.empty()) status.ignored.push_back(path);
            continue;
        } else if (kind == '1' || kind == '2' || kind == 'u') {
            if (e.size() < 4) continue;
            file.index = e[2];
            file.worktree = e[3];
            const int skip = kind == '1' ? 8 : kind == '2' ? 9 : 10;
            file.path = after_fields(e, skip);
            if (kind == 'u') file.conflicted = true;
            if (kind == '2' && i + 1 < entries.size()) file.old_path = entries[++i];
        } else {
            continue;
        }
        if (!file.path.empty()) status.files.push_back(std::move(file));
    }
    return status;
}

std::vector<GitCommit> parse_git_log(const std::string& text) {
    std::vector<GitCommit> out;
    for (const std::string& record : split_on(text, '\x1e')) {
        if (record.find('\x1f') == std::string::npos) continue;
        const std::size_t newline = record.find('\n');
        const std::string head = strip_cr(record.substr(0, newline));
        const std::vector<std::string> f = split_on(head, '\x1f');
        if (f.size() < 8) continue;
        GitCommit c;
        c.hash = f[0];
        c.short_hash = f[1];
        if (!f[2].empty()) c.parents = split_on(f[2], ' ');
        c.author = f[3];
        c.email = f[4];
        c.time = to_number(f[5]);
        c.refs = f[6];
        c.subject = f[7];
        for (std::size_t k = 8; k < f.size(); ++k) c.subject += "\x1f" + f[k];
        if (newline != std::string::npos) {
            for (const std::string& raw : split_text_lines(record.substr(newline + 1))) {
                const std::string line = strip_cr(raw);
                if (line.empty()) continue;
                const std::vector<std::string> parts = split_on(line, '\t');
                if (parts.size() < 2 || parts[0].empty()) continue;
                c.status = std::string(1, parts[0][0]);
                c.path = parts.back();
            }
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<GitBranch> parse_git_branches(const std::string& text) {
    std::vector<GitBranch> out;
    for (const std::string& raw : split_text_lines(text)) {
        const std::string line = strip_cr(raw);
        const std::vector<std::string> f = split_on(line, '\x1f');
        if (f.empty() || f[0].empty()) continue;
        GitBranch b;
        const std::string& ref = f[0];
        if (ref.rfind("refs/heads/", 0) == 0) {
            b.name = ref.substr(11);
        } else if (ref.rfind("refs/remotes/", 0) == 0) {
            b.name = ref.substr(13);
            b.remote = true;
            if (b.name.size() >= 5 && b.name.compare(b.name.size() - 5, 5, "/HEAD") == 0) continue;
        } else {
            b.name = ref;
        }
        if (f.size() > 1) b.current = f[1] == "*";
        if (f.size() > 2) b.upstream = f[2];
        if (f.size() > 3) b.time = to_number(f[3]);
        if (f.size() > 4) b.track = f[4];
        out.push_back(std::move(b));
    }
    return out;
}

std::string git_branch_format() {
    return "%(refname)%1f%(HEAD)%1f%(upstream:short)%1f%(committerdate:unix)%1f%(upstream:track)";
}

std::string git_tag_format() {
    return "%(refname:short)%1f%(objectname:short)%1f%(creatordate:unix)%1f%(subject)";
}

std::vector<GitTag> parse_git_tags(const std::string& text) {
    std::vector<GitTag> out;
    for (const std::string& raw : split_text_lines(text)) {
        const std::vector<std::string> f = split_on(strip_cr(raw), '\x1f');
        if (f.empty() || f[0].empty()) continue;
        GitTag t;
        t.name = f[0];
        if (f.size() > 1) t.hash = f[1];
        if (f.size() > 2) t.time = to_number(f[2]);
        if (f.size() > 3) t.subject = f[3];
        out.push_back(std::move(t));
    }
    return out;
}

std::size_t diff_hunk_count(const std::vector<DiffLine>& lines) {
    std::size_t n = 0;
    for (const DiffLine& d : lines) n += d.kind == DiffLine::Kind::Hunk ? 1 : 0;
    return n;
}

namespace {

bool find_hunk(const std::vector<DiffLine>& lines, std::size_t index, std::size_t& begin, std::size_t& end) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].kind != DiffLine::Kind::Hunk) continue;
        if (seen++ != index) continue;
        begin = i;
        end = i + 1;
        while (end < lines.size() && lines[end].kind != DiffLine::Kind::Hunk && lines[end].kind != DiffLine::Kind::File) ++end;
        return true;
    }
    return false;
}

bool ends_with_newline(const std::string& text) {
    return !text.empty() && text.back() == '\n';
}

} // namespace

std::optional<DiffHunk> diff_hunk(const std::vector<DiffLine>& lines, std::size_t index) {
    std::size_t begin = 0, end = 0;
    if (!find_hunk(lines, index, begin, end)) return std::nullopt;
    DiffHunk h;
    parse_hunk_header(lines[begin].text, h.old_start, h.old_count, h.new_start, h.new_count);
    return h;
}

std::string hunk_patch(const std::string& path, const std::vector<DiffLine>& lines, std::size_t index, const std::string& before, const std::string& after) {
    std::size_t begin = 0, end = 0;
    if (!find_hunk(lines, index, begin, end)) return std::string();
    const std::vector<std::string> old_lines = split_text_lines(before);
    const std::vector<std::string> new_lines = split_text_lines(after);
    const long long old_total = static_cast<long long>(old_lines.size());
    const long long new_total = static_cast<long long>(new_lines.size());
    auto old_text = [&](const DiffLine& d) { return d.old_line > 0 && d.old_line <= old_total ? old_lines[static_cast<std::size_t>(d.old_line - 1)] : d.text; };
    auto new_text = [&](const DiffLine& d) { return d.new_line > 0 && d.new_line <= new_total ? new_lines[static_cast<std::size_t>(d.new_line - 1)] : d.text; };
    const bool old_eol = before.empty() || ends_with_newline(before);
    const bool new_eol = after.empty() || ends_with_newline(after);
    std::string out = "diff --git a/" + path + " b/" + path + "\n--- a/" + path + "\n+++ b/" + path + "\n" + lines[begin].text + "\n";
    for (std::size_t i = begin + 1; i < end; ++i) {
        const DiffLine& d = lines[i];
        if (d.kind == DiffLine::Kind::Added) {
            out += "+" + new_text(d) + "\n";
            if (d.new_line == new_total && !new_eol) out += "\\ No newline at end of file\n";
        } else if (d.kind == DiffLine::Kind::Removed) {
            out += "-" + old_text(d) + "\n";
            if (d.old_line == old_total && !old_eol) out += "\\ No newline at end of file\n";
        } else if (d.kind == DiffLine::Kind::Context) {
            out += " " + old_text(d) + "\n";
            if (d.old_line == old_total && !old_eol) out += "\\ No newline at end of file\n";
        }
    }
    return out;
}

std::optional<std::string> revert_hunk(const std::vector<DiffLine>& lines, std::size_t index, const std::string& before, const std::string& after) {
    const std::optional<DiffHunk> h = diff_hunk(lines, index);
    if (!h) return std::nullopt;
    const std::vector<std::string> a = split_text_lines(before);
    const std::vector<std::string> b = split_text_lines(after);
    const std::size_t new_from = static_cast<std::size_t>(h->new_count ? h->new_start - 1 : h->new_start);
    const std::size_t old_from = static_cast<std::size_t>(h->old_count ? h->old_start - 1 : h->old_start);
    const std::size_t new_n = static_cast<std::size_t>(h->new_count);
    const std::size_t old_n = static_cast<std::size_t>(h->old_count);
    if (new_from + new_n > b.size() || old_from + old_n > a.size()) return std::nullopt;
    std::vector<std::string> out(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(new_from));
    out.insert(out.end(), a.begin() + static_cast<std::ptrdiff_t>(old_from), a.begin() + static_cast<std::ptrdiff_t>(old_from + old_n));
    out.insert(out.end(), b.begin() + static_cast<std::ptrdiff_t>(new_from + new_n), b.end());
    const bool touches_end = new_from + new_n == b.size();
    const bool eol = touches_end ? (before.empty() ? ends_with_newline(after) : ends_with_newline(before)) : (after.empty() || ends_with_newline(after));
    const bool crlf = after.find("\r\n") != std::string::npos;
    std::string text;
    for (std::size_t i = 0; i < out.size(); ++i) {
        std::string line = out[i];
        if (!line.empty() && line.back() == '\r') line.pop_back();
        text += line;
        if (i + 1 < out.size() || eol) text += crlf ? "\r\n" : "\n";
    }
    return text;
}

std::string repo_name_from_url(const std::string& url_in) {
    std::string url = url_in;
    while (!url.empty() && (url.back() == '/' || url.back() == '\\' || url.back() == ' ')) url.pop_back();
    const std::size_t cut = url.find_last_of("/\\:");
    std::string name = cut == std::string::npos ? url : url.substr(cut + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".git") == 0) name.erase(name.size() - 4);
    return name.empty() ? std::string("repository") : name;
}

std::vector<std::string> parse_lines(const std::string& text) {
    std::vector<std::string> out;
    for (const std::string& raw : split_text_lines(text)) {
        std::string line = strip_cr(raw);
        const std::size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        const std::size_t b = line.find_last_not_of(" \t");
        out.push_back(line.substr(a, b - a + 1));
    }
    return out;
}

std::vector<DiffLine> parse_unified_diff(const std::string& text) {
    std::vector<DiffLine> out;
    long long old_line = 0;
    long long new_line = 0;
    long long old_left = 0;
    long long new_left = 0;
    for (const std::string& raw : split_text_lines(text)) {
        const std::string line = strip_cr(raw);
        DiffLine d;
        const bool in_hunk = old_left > 0 || new_left > 0;
        if (in_hunk && !line.empty() && (line[0] == '+' || line[0] == '-' || line[0] == ' ')) {
            d.text = line.substr(1);
            if (line[0] == '+') {
                d.kind = DiffLine::Kind::Added;
                d.new_line = new_line++;
                --new_left;
            } else if (line[0] == '-') {
                d.kind = DiffLine::Kind::Removed;
                d.old_line = old_line++;
                --old_left;
            } else {
                d.kind = DiffLine::Kind::Context;
                d.old_line = old_line++;
                d.new_line = new_line++;
                --old_left;
                --new_left;
            }
        } else if (in_hunk && line.empty()) {
            d.kind = DiffLine::Kind::Context;
            d.old_line = old_line++;
            d.new_line = new_line++;
            --old_left;
            --new_left;
        } else if (line.rfind("@@", 0) == 0) {
            long long os = 0, oc = 0, ns = 0, nc = 0;
            parse_hunk_header(line, os, oc, ns, nc);
            old_line = os;
            new_line = ns;
            old_left = oc;
            new_left = nc;
            d.kind = DiffLine::Kind::Hunk;
            d.text = line;
        } else if (line.rfind("diff --git ", 0) == 0) {
            old_left = new_left = 0;
            d.kind = DiffLine::Kind::File;
            d.text = line;
            const std::size_t b = line.rfind(" b/");
            if (b != std::string::npos) d.text = line.substr(b + 3);
        } else {
            d.kind = DiffLine::Kind::Meta;
            d.text = line;
        }
        out.push_back(std::move(d));
    }
    while (!out.empty() && out.back().kind == DiffLine::Kind::Meta && out.back().text.empty()) out.pop_back();
    return out;
}

std::vector<DiffLine> diff_texts(const std::string& before, const std::string& after, std::size_t context) {
    std::vector<DiffLine> out;
    if (before == after) return out;
    const std::vector<std::string> a = split_text_lines(before);
    const std::vector<std::string> b = split_text_lines(after);

    std::size_t prefix = 0;
    while (prefix < a.size() && prefix < b.size() && a[prefix] == b[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < a.size() - prefix && suffix < b.size() - prefix && a[a.size() - 1 - suffix] == b[b.size() - 1 - suffix]) ++suffix;

    const long long n = static_cast<long long>(a.size() - prefix - suffix);
    const long long m = static_cast<long long>(b.size() - prefix - suffix);

    enum Op : char { Keep, Del, Add };
    std::vector<Op> ops;
    ops.reserve(static_cast<std::size_t>(prefix + suffix + n + m));
    for (std::size_t i = 0; i < prefix; ++i) ops.push_back(Keep);

    const long long max_d = std::min<long long>(n + m, 3000);
    bool solved = false;
    if (n == 0 || m == 0) {
        for (long long i = 0; i < n; ++i) ops.push_back(Del);
        for (long long i = 0; i < m; ++i) ops.push_back(Add);
        solved = true;
    } else {
        const long long offset = max_d + 1;
        std::vector<long long> v(static_cast<std::size_t>(2 * offset + 1), 0);
        std::vector<std::vector<long long>> trace;
        long long found_d = -1;
        auto A = [&](long long i) -> const std::string& { return a[prefix + static_cast<std::size_t>(i)]; };
        auto B = [&](long long j) -> const std::string& { return b[prefix + static_cast<std::size_t>(j)]; };
        for (long long d = 0; d <= max_d && found_d < 0; ++d) {
            trace.emplace_back(v.begin() + (offset - d), v.begin() + (offset + d + 1));
            for (long long k = -d; k <= d; k += 2) {
                long long x;
                if (k == -d || (k != d && v[offset + k - 1] < v[offset + k + 1])) x = v[offset + k + 1];
                else x = v[offset + k - 1] + 1;
                long long y = x - k;
                while (x < n && y < m && A(x) == B(y)) {
                    ++x;
                    ++y;
                }
                v[offset + k] = x;
                if (x >= n && y >= m) {
                    found_d = d;
                    break;
                }
            }
        }
        if (found_d >= 0) {
            std::vector<Op> rev;
            long long x = n;
            long long y = m;
            for (long long d = found_d; d > 0; --d) {
                const std::vector<long long>& pv = trace[static_cast<std::size_t>(d)];
                auto at = [&](long long k) { return pv[static_cast<std::size_t>(k + d)]; };
                const long long k = x - y;
                long long prev_k;
                if (k == -d || (k != d && at(k - 1) < at(k + 1))) prev_k = k + 1;
                else prev_k = k - 1;
                const long long prev_x = at(prev_k);
                const long long prev_y = prev_x - prev_k;
                while (x > prev_x && y > prev_y) {
                    rev.push_back(Keep);
                    --x;
                    --y;
                }
                if (x == prev_x) rev.push_back(Add);
                else rev.push_back(Del);
                x = prev_x;
                y = prev_y;
            }
            while (x > 0 && y > 0) {
                rev.push_back(Keep);
                --x;
                --y;
            }
            ops.insert(ops.end(), rev.rbegin(), rev.rend());
            solved = true;
        }
    }
    if (!solved) {
        for (long long i = 0; i < n; ++i) ops.push_back(Del);
        for (long long i = 0; i < m; ++i) ops.push_back(Add);
    }
    for (std::size_t i = 0; i < suffix; ++i) ops.push_back(Keep);

    struct Row {
        Op op;
        std::size_t ai;
        std::size_t bi;
    };
    std::vector<Row> rows;
    rows.reserve(ops.size());
    std::size_t ai = 0, bi = 0;
    for (Op op : ops) {
        rows.push_back(Row{ op, ai, bi });
        if (op != Add) ++ai;
        if (op != Del) ++bi;
    }

    std::size_t i = 0;
    while (i < rows.size()) {
        while (i < rows.size() && rows[i].op == Keep) ++i;
        if (i >= rows.size()) break;
        std::size_t start = i >= context ? i - context : 0;
        std::size_t last = i;
        std::size_t end = i;
        while (end < rows.size()) {
            if (rows[end].op != Keep) last = end;
            else if (end - last > context * 2) break;
            ++end;
        }
        const std::size_t stop = std::min(rows.size(), last + 1 + context);
        long long old_count = 0, new_count = 0;
        for (std::size_t r = start; r < stop; ++r) {
            if (rows[r].op != Add) ++old_count;
            if (rows[r].op != Del) ++new_count;
        }
        DiffLine header;
        header.kind = DiffLine::Kind::Hunk;
        header.text = "@@ -" + std::to_string(old_count ? rows[start].ai + 1 : rows[start].ai) + "," + std::to_string(old_count) + " +" + std::to_string(new_count ? rows[start].bi + 1 : rows[start].bi) + "," + std::to_string(new_count) + " @@";
        out.push_back(std::move(header));
        for (std::size_t r = start; r < stop; ++r) {
            DiffLine d;
            const Row& row = rows[r];
            if (row.op == Keep) {
                d.kind = DiffLine::Kind::Context;
                d.text = strip_cr(a[row.ai]);
                d.old_line = static_cast<long long>(row.ai + 1);
                d.new_line = static_cast<long long>(row.bi + 1);
            } else if (row.op == Del) {
                d.kind = DiffLine::Kind::Removed;
                d.text = strip_cr(a[row.ai]);
                d.old_line = static_cast<long long>(row.ai + 1);
            } else {
                d.kind = DiffLine::Kind::Added;
                d.text = strip_cr(b[row.bi]);
                d.new_line = static_cast<long long>(row.bi + 1);
            }
            out.push_back(std::move(d));
        }
        i = stop;
    }
    return out;
}

std::string relative_time(long long then, long long now) {
    if (then <= 0) return std::string();
    long long delta = now - then;
    if (delta < 0) delta = 0;
    auto plural = [](long long n, const char* unit) {
        return std::to_string(n) + " " + unit + (n == 1 ? "" : "s") + " ago";
    };
    if (delta < 45) return "just now";
    if (delta < 3600) return plural(std::max<long long>(1, delta / 60), "minute");
    if (delta < 86400) return plural(delta / 3600, "hour");
    if (delta < 2 * 86400) return "yesterday";
    if (delta < 14 * 86400) return plural(delta / 86400, "day");
    if (delta < 60 * 86400) return plural(delta / (7 * 86400), "week");
    if (delta < 365 * 86400) return plural(delta / (30 * 86400), "month");
    return plural(delta / (365 * 86400), "year");
}

std::string short_ref_names(const std::string& refs) {
    std::string out;
    for (std::string part : split_on(refs, ',')) {
        const std::size_t a = part.find_first_not_of(' ');
        if (a == std::string::npos) continue;
        part = part.substr(a);
        if (part.rfind("HEAD -> ", 0) == 0) part = part.substr(8);
        else if (part.rfind("tag: ", 0) == 0) part = part.substr(5);
        if (part.empty()) continue;
        if (!out.empty()) out += ", ";
        out += part;
    }
    return out;
}

char git_status_letter(const GitFile& file) {
    if (file.conflicted) return '!';
    if (file.untracked) return 'U';
    const char c = file.worktree != '.' ? file.worktree : file.index;
    return c == 'T' ? 'M' : c;
}

std::string git_status_word(const GitFile& file) {
    switch (git_status_letter(file)) {
    case '!': return "conflict";
    case 'U': return "untracked";
    case 'A': return "added";
    case 'D': return "deleted";
    case 'R': return "renamed";
    case 'C': return "copied";
    case 'M': return "modified";
    default: return "changed";
    }
}

std::string relative_to(const std::string& path, const std::string& root) {
    if (root.empty() || path.empty()) return path;
    std::error_code ec;
    const fs::path rel = fs::path(path).lexically_relative(fs::path(root));
    const std::string text = rel.generic_string();
    if (text.empty() || text == "." || text.rfind("..", 0) == 0) return fs::path(path).generic_string();
    return text;
}

} // namespace canopy
