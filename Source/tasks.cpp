#include "canopy/tasks.hpp"

#include "canopy/build.hpp"
#include "canopy/settings.hpp"
#include "canopy/workspace.hpp"

#include <grove/bark/parser.hpp>
#include <grove/bark/stylesheet.hpp>

#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string value_text(const grove::bark::Value& v) {
    if (v.is_string() || v.is_ident()) return v.text;
    if (v.is_number()) {
        if (std::floor(v.number) == v.number && std::fabs(v.number) < 1e15) return std::to_string(static_cast<long long>(v.number)) + v.unit;
        return grove::bark::to_string(v);
    }
    return grove::bark::to_string(v);
}

std::vector<std::string> value_texts(const grove::bark::ValueList& values) {
    std::vector<std::string> out;
    for (const grove::bark::Value& v : values) out.push_back(value_text(v));
    return out;
}

std::string joined_command(const grove::bark::ValueList& values) {
    if (values.size() == 1) return value_text(values.front());
    std::string out;
    for (const grove::bark::Value& v : values) {
        std::string t = value_text(v);
        if (v.is_string() && (t.empty() || t.find_first_of(" \t\"'") != std::string::npos)) {
            std::string q = "\"";
            for (char c : t) {
                if (c == '"' || c == '\\') q += '\\';
                q += c;
            }
            t = q + "\"";
        }
        if (!out.empty()) out += ' ';
        out += t;
    }
    return out;
}

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool truthy(const std::string& s) {
    return s == "true" || s == "yes" || s == "on" || s == "1";
}

struct VersionSpot {
    std::size_t at = 0;
    std::size_t length = 0;
};

std::optional<VersionSpot> locate_version(const std::string& text) {
    static const std::regex cmake(R"(project\s*\([^)]*?\bVERSION\s+([0-9]+(?:\.[0-9]+)*))", std::regex::icase);
    static const std::regex json(R"re("version"\s*:\s*"([^"]+)")re");
    static const std::regex toml(R"re((?:^|\n)[ \t]*version[ \t]*=[ \t]*"([^"]+)")re");
    static const std::regex bare(R"(^\s*v?([0-9]+(?:\.[0-9]+)+(?:[-+][0-9A-Za-z.\-]+)?)\s*$)");
    for (const std::regex* re : { &cmake, &json, &toml }) {
        std::smatch m;
        if (std::regex_search(text, m, *re)) return VersionSpot{ static_cast<std::size_t>(m.position(1)), static_cast<std::size_t>(m.length(1)) };
    }
    if (text.size() < 200) {
        std::smatch m;
        if (std::regex_match(text, m, bare)) return VersionSpot{ static_cast<std::size_t>(m.position(1)), static_cast<std::size_t>(m.length(1)) };
    }
    return std::nullopt;
}

std::string today() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return buffer;
}

template <typename Wrap>
std::string substitute_with(const std::string& text, const std::map<std::string, std::string>& values, Wrap&& wrap) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            const std::size_t close = text.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string name = text.substr(i + 1, close - i - 1);
                const auto it = values.find(name);
                if (it != values.end()) {
                    out += wrap(it->second);
                    i = close + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

} // namespace

const Task* TaskFile::find(const std::string& id) const {
    for (const Task& t : tasks) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const Task* TaskFile::hooked_build(TaskHook hook) const {
    for (const Task& t : tasks) {
        if (t.on_build == hook) return &t;
    }
    return nullptr;
}

const Task* TaskFile::hooked_run(TaskHook hook) const {
    for (const Task& t : tasks) {
        if (t.on_run == hook) return &t;
    }
    return nullptr;
}

std::string tasks_path(const std::string& workspace_root) {
    return workspace_config_file(workspace_root, "tasks.bark");
}

std::string tasks_template() {
    return R"(release {
    label: "Release";
    description: "Commit dev, fast-forward main, tag the version and push everything";
    ask: message "Commit message";
    ask: version "Version" "{next-patch}";
    confirm: "Release v{version} from {branch} to main?";
    set-version: "CMakeLists.txt";
    run: "git add -A";
    try: "git commit -m {message}";
    run: "git push";
    run: "git checkout main";
    run: "git merge --ff-only dev";
    run: "git push";
    run: "git tag v{version}";
    run: "git push origin v{version}";
    run: "git checkout dev";
}

commit {
    label: "Commit & Push";
    description: "Stage everything, commit and push the current branch";
    ask: message "Commit message";
    run: "git add -A";
    run: "git commit -m {message}";
    run: "git push";
}

cmake {
    label: "CMake Build";
    description: "Configure and build with CMake";
    run: "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release";
    run: "cmake --build build -j";
}
)";
}

TaskHook parse_task_hook(const std::string& text) {
    if (text == "before") return TaskHook::Before;
    if (text == "after") return TaskHook::After;
    if (text == "instead") return TaskHook::Instead;
    return TaskHook::None;
}

std::string task_hook_name(TaskHook hook) {
    switch (hook) {
    case TaskHook::Before: return "before";
    case TaskHook::After: return "after";
    case TaskHook::Instead: return "instead";
    case TaskHook::None: break;
    }
    return "none";
}

TaskFile parse_tasks(const std::string& text, const std::string& path) {
    TaskFile file;
    file.path = path;
    file.exists = true;
    grove::Diagnostics diagnostics;
    const grove::bark::Stylesheet sheet = grove::bark::parse_stylesheet(text, path, diagnostics);
    for (const grove::Diagnostic& d : diagnostics.items()) {
        if (d.severity != grove::Severity::Error) continue;
        file.problems.push_back("line " + std::to_string(d.span.begin.line) + ": " + d.message);
    }
    for (const grove::bark::Rule& rule : sheet.rules()) {
        for (const grove::bark::Selector& selector : rule.selectors) {
            Task task;
            task.id = selector.to_string();
            task.line = selector.span.begin.line;
            for (const grove::bark::Declaration& d : rule.declarations) {
                const std::vector<std::string> parts = value_texts(d.values);
                const std::string first = parts.empty() ? std::string() : parts.front();
                const std::string where = task.id + " (line " + std::to_string(d.span.begin.line) + ")";
                if (d.property == "label") {
                    task.label = first;
                } else if (d.property == "description") {
                    task.description = first;
                } else if (d.property == "ask") {
                    if (parts.empty()) {
                        file.problems.push_back(where + ": ask needs a name");
                        continue;
                    }
                    TaskAsk ask;
                    ask.name = parts[0];
                    ask.prompt = parts.size() > 1 ? parts[1] : parts[0];
                    ask.value = parts.size() > 2 ? parts[2] : std::string();
                    task.asks.push_back(std::move(ask));
                } else if (d.property == "confirm") {
                    task.confirm = first;
                } else if (d.property == "run" || d.property == "try") {
                    const std::string command = joined_command(d.values);
                    if (trim(command).empty()) {
                        file.problems.push_back(where + ": " + d.property + " needs a command");
                        continue;
                    }
                    task.steps.push_back(TaskStep{ d.property == "run" ? TaskStep::Kind::Run : TaskStep::Kind::Try, command });
                } else if (d.property == "shell") {
                    task.steps.push_back(TaskStep{ TaskStep::Kind::Shell, first });
                } else if (d.property == "set-version") {
                    task.steps.push_back(TaskStep{ TaskStep::Kind::SetVersion, joined_command(d.values) });
                } else if (d.property == "version-file") {
                    task.version_file = first;
                } else if (d.property == "cwd") {
                    task.cwd = first;
                } else if (d.property == "on-build") {
                    task.on_build = parse_task_hook(first);
                    if (task.on_build == TaskHook::None && first != "none") file.problems.push_back(where + ": on-build is before, after or instead");
                } else if (d.property == "on-run") {
                    task.on_run = parse_task_hook(first);
                    if (task.on_run == TaskHook::None && first != "none") file.problems.push_back(where + ": on-run is before, after or instead");
                } else if (d.property == "keep-going") {
                    task.keep_going = truthy(first);
                } else {
                    file.problems.push_back(where + ": unknown task property '" + d.property + "'");
                }
            }
            file.tasks.push_back(std::move(task));
        }
    }
    return file;
}

TaskFile load_tasks(const std::string& workspace_root) {
    TaskFile file;
    if (workspace_root.empty()) return file;
    const std::string path = tasks_path(workspace_root);
    file.path = path;
    const std::optional<std::string> text = read_text_file(path);
    if (!text) return file;
    return parse_tasks(*text, path);
}

std::vector<std::string> tokenize_command(const std::string& text) {
    std::vector<std::string> out;
    std::string current;
    bool have = false;
    char quote = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote) {
            if (c == quote) {
                quote = 0;
            } else if (quote == '"' && c == '\\' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\\')) {
                current += text[++i];
            } else {
                current += c;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            have = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (have) out.push_back(current);
            current.clear();
            have = false;
        } else {
            current += c;
            have = true;
        }
    }
    if (have) out.push_back(current);
    return out;
}

std::string substitute(const std::string& text, const std::map<std::string, std::string>& values) {
    return substitute_with(text, values, [](const std::string& v) { return v; });
}

std::string substitute_quoted(const std::string& text, const std::map<std::string, std::string>& values) {
    return substitute_with(text, values, [](const std::string& v) { return quote_argument(v); });
}

std::optional<std::string> find_version(const std::string& text) {
    const std::optional<VersionSpot> spot = locate_version(text);
    if (!spot) return std::nullopt;
    return text.substr(spot->at, spot->length);
}

std::optional<std::string> replace_version(const std::string& text, const std::string& version) {
    const std::optional<VersionSpot> spot = locate_version(text);
    if (!spot) return std::nullopt;
    std::string v = version;
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::string out = text;
    out.replace(spot->at, spot->length, v);
    return out;
}

std::string bump_version(const std::string& version, const std::string& part) {
    std::string core = version;
    std::string lead;
    if (!core.empty() && (core[0] == 'v' || core[0] == 'V')) {
        lead = core.substr(0, 1);
        core.erase(0, 1);
    }
    const std::size_t extra = core.find_first_of("-+");
    if (extra != std::string::npos) core = core.substr(0, extra);
    std::vector<long long> n;
    std::size_t start = 0;
    while (start <= core.size()) {
        const std::size_t dot = core.find('.', start);
        const std::string piece = core.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        long long value = 0;
        for (char c : piece) {
            if (!std::isdigit(static_cast<unsigned char>(c))) break;
            value = value * 10 + (c - '0');
        }
        n.push_back(value);
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    while (n.size() < 3) n.push_back(0);
    if (part == "major") {
        ++n[0];
        n[1] = n[2] = 0;
    } else if (part == "minor") {
        ++n[1];
        n[2] = 0;
    } else {
        ++n[2];
    }
    for (std::size_t i = 3; i < n.size(); ++i) n[i] = 0;
    std::string out = lead;
    for (std::size_t i = 0; i < n.size(); ++i) {
        if (i) out += '.';
        out += std::to_string(n[i]);
    }
    return out;
}

std::string task_folder(const Task& task, const std::string& root) {
    if (task.cwd.empty()) return root;
    const fs::path p(task.cwd);
    if (p.is_absolute()) return p.string();
    return (fs::path(root) / p).lexically_normal().string();
}

std::string task_version_file(const Task& task, const std::string& root) {
    std::string name = task.version_file;
    if (name.empty()) {
        for (const TaskStep& s : task.steps) {
            if (s.kind != TaskStep::Kind::SetVersion) continue;
            const std::vector<std::string> files = tokenize_command(s.text);
            if (!files.empty()) {
                name = files.front();
                break;
            }
        }
    }
    if (name.empty()) {
        for (const char* candidate : { "CMakeLists.txt", "package.json", "Cargo.toml", "pyproject.toml", "VERSION" }) {
            std::error_code ec;
            if (fs::exists(fs::path(root) / candidate, ec)) {
                name = candidate;
                break;
            }
        }
    }
    if (name.empty()) return std::string();
    const fs::path p(name);
    return p.is_absolute() ? p.string() : (fs::path(root) / p).string();
}

std::optional<std::string> current_version(const Task& task, const std::string& root) {
    const std::string file = task_version_file(task, root);
    if (file.empty()) return std::nullopt;
    const std::optional<std::string> text = read_text_file(file);
    if (!text) return std::nullopt;
    return find_version(*text);
}

std::map<std::string, std::string> task_values(const Task& task, const TaskContext& context) {
    std::map<std::string, std::string> v;
    v["root"] = context.root;
    v["branch"] = context.branch;
    v["date"] = today();
    v["file"] = context.file;
    if (!context.file.empty()) {
        const fs::path p(context.file);
        v["file-dir"] = p.parent_path().string();
        v["file-name"] = p.filename().string();
        v["file-stem"] = p.stem().string();
    } else {
        v["file-dir"] = v["file-name"] = v["file-stem"] = std::string();
    }
    const std::string version = current_version(task, context.root).value_or("0.0.0");
    v["current-version"] = version;
    v["next-patch"] = bump_version(version, "patch");
    v["next-minor"] = bump_version(version, "minor");
    v["next-major"] = bump_version(version, "major");
    v["version"] = version;
    for (const auto& [key, value] : context.values) v[key] = value;
    return v;
}

std::vector<CommandBatch::Step> task_steps(const Task& task, const TaskContext& context) {
    const std::map<std::string, std::string> values = task_values(task, context);
    const std::string root = context.root;
    std::vector<CommandBatch::Step> out;
    for (const TaskStep& step : task.steps) {
        CommandBatch::Step s;
        s.key = task.id;
        switch (step.kind) {
        case TaskStep::Kind::Run:
        case TaskStep::Kind::Try: {
            for (const std::string& token : tokenize_command(step.text)) s.command.push_back(substitute(token, values));
            if (s.command.empty()) continue;
            s.label = command_line(s.command);
            s.may_fail = step.kind == TaskStep::Kind::Try || task.keep_going;
            break;
        }
        case TaskStep::Kind::Shell:
            s.shell = substitute_quoted(step.text, values);
            s.label = s.shell;
            s.may_fail = task.keep_going;
            break;
        case TaskStep::Kind::SetVersion: {
            std::vector<std::string> files;
            for (const std::string& token : tokenize_command(step.text)) files.push_back(substitute(token, values));
            if (files.empty()) {
                const std::string fallback = task_version_file(task, root);
                if (!fallback.empty()) files.push_back(fallback);
            }
            const std::string version = values.at("version");
            s.label = "set version " + version + (files.empty() ? std::string() : " in " + command_line(files));
            s.may_fail = task.keep_going;
            s.internal = [files, version, root](std::string& output) {
                if (files.empty()) {
                    output += "there is no version file to update";
                    return false;
                }
                bool ok = true;
                for (const std::string& name : files) {
                    const fs::path p(name);
                    const std::string path = p.is_absolute() ? p.string() : (fs::path(root) / p).string();
                    const std::optional<std::string> text = read_text_file(path);
                    if (!text) {
                        output += "can't read " + name + "\n";
                        ok = false;
                        continue;
                    }
                    const std::optional<std::string> old = find_version(*text);
                    const std::optional<std::string> updated = replace_version(*text, version);
                    if (!updated || !old) {
                        output += "no version found in " + name + "\n";
                        ok = false;
                        continue;
                    }
                    if (*updated == *text) {
                        output += name + " is already " + *old + "\n";
                        continue;
                    }
                    if (!write_text_file(path, *updated)) {
                        output += "can't write " + name + "\n";
                        ok = false;
                        continue;
                    }
                    output += name + ": " + *old + " -> " + *find_version(*updated) + "\n";
                }
                return ok;
            };
            break;
        }
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::string task_summary(const Task& task) {
    if (!task.description.empty()) return task.description;
    std::string out;
    for (const TaskStep& s : task.steps) {
        std::string piece = s.kind == TaskStep::Kind::SetVersion ? std::string("set version") : s.text;
        if (piece.rfind("git ", 0) == 0) piece = piece.substr(4);
        if (!out.empty()) out += " \xE2\x86\x92 ";
        out += piece;
        if (out.size() > 90) {
            out = out.substr(0, 87) + "...";
            break;
        }
    }
    return out;
}

} // namespace canopy
