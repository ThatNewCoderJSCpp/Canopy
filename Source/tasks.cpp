#include "canopy/tasks.hpp"

#include "canopy/build.hpp"
#include "canopy/settings.hpp"
#include "canopy/workspace.hpp"

#include <grove/bark/actions.hpp>
#include <grove/bark/parser.hpp>
#include <grove/bark/stylesheet.hpp>

#include <cctype>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <set>
#include <regex>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string value_text(const grove::bark::Value& v) {
    return grove::bark::text_of(v);
}

std::vector<std::string> value_texts(const grove::bark::ValueList& values) {
    return grove::bark::texts_of(values);
}

std::string joined_command(const grove::bark::ValueList& values) {
    return grove::bark::joined_text(values);
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
    return grove::bark::interpolate_with(text, [&values](const std::string& name) -> std::optional<std::string> {
        const auto it = values.find(name);
        if (it == values.end()) return std::nullopt;
        return it->second;
    }, [&wrap](const std::string& v) { return wrap(v); }).text;
}

bool is_word(const grove::bark::Value& v, std::initializer_list<const char*> words) {
    if (!v.is_ident()) return false;
    for (const char* w : words) {
        if (v.text == w) return true;
    }
    return false;
}

std::string ask_type(const std::string& word) {
    if (word == "integer" || word == "int" || word == "whole") return "integer";
    if (word == "number" || word == "float") return "number";
    if (word == "bool" || word == "boolean" || word == "yes-no" || word == "switch") return "bool";
    if (word == "choice") return "choice";
    return "text";
}

bool whole_number(double n) {
    return std::floor(n) == n && std::fabs(n) < 1e15;
}

TaskAsk parse_ask(const grove::bark::ValueList& values, std::vector<std::string>& problems, const std::string& where) {
    TaskAsk ask;
    ask.name = value_text(values.front());
    bool typed = false;
    std::size_t positional = 0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        const grove::bark::Value& v = values[i];
        if (v.is_function() && (v.text == "range" || v.text == "between")) {
            std::vector<std::string> bounds;
            for (const grove::bark::Value& a : v.args) {
                if (!a.is_ident(",")) bounds.push_back(value_text(a));
            }
            if (bounds.size() != 2) {
                problems.push_back(where + ": range needs a smallest and a largest value, like range(1, 64)");
                continue;
            }
            ask.min = bounds[0];
            ask.max = bounds[1];
            if (!typed) ask.type = "integer";
            continue;
        }
        if (v.is_function() && (v.text == "choice" || v.text == "one-of")) {
            for (const grove::bark::Value& a : v.args) {
                if (!a.is_ident(",")) ask.choices.push_back(value_text(a));
            }
            ask.type = "choice";
            typed = true;
            continue;
        }
        if (is_word(v, { "text", "string", "integer", "int", "whole", "number", "float", "bool", "boolean", "yes-no", "switch" })) {
            ask.type = ask_type(v.text);
            typed = true;
            continue;
        }
        if (is_word(v, { "required" })) {
            ask.required = true;
            continue;
        }
        if (is_word(v, { "optional" })) {
            ask.required = false;
            continue;
        }
        if (positional == 0) {
            ask.prompt = value_text(v);
        } else if (positional == 1) {
            ask.value = value_text(v);
            if (!typed && v.is_number() && v.unit.empty()) ask.type = whole_number(v.number) ? "integer" : "number";
        } else {
            problems.push_back(where + ": ask takes a name, a prompt and a default; '" + value_text(v) + "' is extra");
        }
        ++positional;
    }
    if (ask.prompt.empty()) ask.prompt = ask.name;
    if (ask.type == "choice" && ask.choices.empty()) problems.push_back(where + ": a choice ask needs choice(\"one\", \"two\")");
    return ask;
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

std::string local_tasks_path(const std::string& workspace_root) {
    return workspace_config_file(workspace_root, "tasks.local.bark");
}

bool keep_local_tasks_out_of_git(const std::string& workspace_root) {
    const fs::path ignore = fs::path(workspace_config_file(workspace_root, ".gitignore"));
    std::string text = read_text_file(ignore.string()).value_or("");
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line == "*.local.bark") return true;
        start = end + 1;
    }
    std::error_code ec;
    fs::create_directories(ignore.parent_path(), ec);
    if (!text.empty() && text.back() != '\n') text += '\n';
    text += "*.local.bark\n";
    return write_text_file(ignore.string(), text);
}

std::string tasks_template() {
    return R"bark(release {
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
    ask: jobs "Parallel jobs (-j)" 8 range(1, 256);
    run: "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release";
    run: "cmake --build build -j {jobs}";
}
)bark";
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

const grove::bark::Actions& task_actions() {
    static const grove::bark::Actions actions = [] {
        grove::bark::Actions a;
        a.setting("label", 1, 1, "the name on the task's button");
        a.setting("description", 1, 1, "what the task does, shown under its name");
        a.setting("confirm", 1, 1, "a question to answer before the steps run");
        a.setting("version-file", 1, 1, "the file {version} is read from");
        a.setting("cwd", 1, 1, "the folder the steps run in");
        a.setting("on-build", 1, 1, "before, after or instead of Build");
        a.setting("on-run", 1, 1, "before, after or instead of Run");
        a.setting("keep-going", 1, 1, "true keeps going after a step fails");
        grove::bark::ActionInfo ask;
        ask.name = "ask";
        ask.summary = "ask: name \"Prompt\" default; asks for a value before the steps run";
        ask.min_args = 1;
        ask.interpolate = false;
        a.add(ask, {});
        grove::bark::ActionInfo run;
        run.name = "run";
        run.summary = "run: \"command\"; runs a program";
        run.min_args = 1;
        a.add(run, {});
        run.name = "try";
        run.summary = "try: \"command\"; runs a program, and a failure doesn't stop the task";
        a.add(run, {});
        grove::bark::ActionInfo shell;
        shell.name = "shell";
        shell.summary = "shell: \"command\"; runs a line in the system shell";
        shell.min_args = 1;
        shell.max_args = 1;
        a.add(shell, {});
        grove::bark::ActionInfo version;
        version.name = "set-version";
        version.summary = "set-version: \"file\"; writes {version} into the file";
        a.add(version, {});
        grove::bark::ActionInfo set;
        set.name = "set";
        set.summary = "set: name value; gives a value a name for the task";
        set.min_args = 1;
        a.add(set, {});
        set.name = "default";
        set.summary = "default: name value; like set, unless an ask or $variable already gives it a value";
        a.add(set, {});
        return a;
    }();
    return actions;
}

std::vector<std::string> task_builtin_values() {
    return { "root", "branch", "date", "file", "file-dir", "file-name", "file-stem", "current-version", "next-patch", "next-minor", "next-major", "version" };
}

std::string task_ask_input(const TaskAsk& ask) {
    if (ask.type == "integer") return "integer";
    if (ask.type == "number") return "float";
    if (ask.type == "bool") return "bool";
    if (ask.type == "choice") return "choice";
    return "string";
}

TaskAnswer check_task_answer(const TaskAsk& ask, const std::string& answer_in, const std::string& fallback) {
    TaskAnswer out;
    std::string answer = trim(answer_in);
    if (answer.empty()) answer = trim(fallback);
    if (answer.empty() && (ask.required || ask.name == "message")) {
        out.ok = false;
        out.problem = (ask.name == "message" ? std::string("A commit message") : ask.prompt) + " is needed";
        return out;
    }
    if (ask.type == "integer" || ask.type == "number") {
        if (answer.empty()) {
            out.value = answer;
            return out;
        }
        char* end = nullptr;
        const double n = std::strtod(answer.c_str(), &end);
        const bool parsed = end && *end == '\0' && end != answer.c_str();
        if (!parsed || (ask.type == "integer" && !whole_number(n))) {
            out.ok = false;
            out.problem = ask.prompt + " needs " + (ask.type == "integer" ? "a whole number" : "a number") + ", not '" + answer + "'";
            return out;
        }
        const double lo = ask.min.empty() ? -1e300 : std::strtod(ask.min.c_str(), nullptr);
        const double hi = ask.max.empty() ? 1e300 : std::strtod(ask.max.c_str(), nullptr);
        if (n < lo || n > hi) {
            out.ok = false;
            out.problem = ask.prompt + " goes from " + (ask.min.empty() ? std::string("any") : ask.min) + " to " + (ask.max.empty() ? std::string("any") : ask.max);
            return out;
        }
        out.value = ask.type == "integer" ? std::to_string(static_cast<long long>(n)) : answer;
        return out;
    }
    if (ask.type == "choice" && !ask.choices.empty() && std::find(ask.choices.begin(), ask.choices.end(), answer) == ask.choices.end()) {
        out.ok = false;
        out.problem = ask.prompt + " is one of " + command_line(ask.choices);
        return out;
    }
    out.value = answer;
    return out;
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
    std::map<std::string, std::string> variables;
    for (const auto& [name, list] : sheet.variables()) variables[name] = grove::bark::joined_text(list);
    const grove::bark::Actions& actions = task_actions();
    for (const grove::bark::Rule& rule : sheet.rules()) {
        std::vector<grove::bark::ActionProblem> rule_problems = actions.check(rule);
        for (const grove::bark::Selector& selector : rule.selectors) {
            Task task;
            task.id = selector.to_string();
            task.line = selector.span.begin.line;
            task.variables = variables;
            for (const grove::bark::ActionProblem& problem : rule_problems) {
                file.problems.push_back(task.id + " (line " + std::to_string(problem.span.begin.line) + "): " + problem.message);
            }
            std::set<std::string> said;
            for (const grove::bark::Declaration& d : rule.declarations) {
                if (!actions.has(d.property)) continue;
                const std::vector<std::string> parts = value_texts(d.values);
                const std::string first = parts.empty() ? std::string() : parts.front();
                const std::string where = task.id + " (line " + std::to_string(d.span.begin.line) + ")";
                const bool repeat = actions.is_setting(d.property) && !said.insert(d.property).second;
                if (repeat) continue;
                if (d.property == "label") {
                    task.label = first;
                } else if (d.property == "description") {
                    task.description = first;
                } else if (d.property == "ask") {
                    if (parts.empty()) continue;
                    task.asks.push_back(parse_ask(d.values, file.problems, where));
                } else if (d.property == "set" || d.property == "default") {
                    if (parts.empty()) continue;
                    TaskSet set;
                    set.name = parts[0];
                    set.only_default = d.property == "default";
                    grove::bark::ValueList rest(d.values.begin() + 1, d.values.end());
                    set.value = rest.empty() ? std::string() : grove::bark::joined_text(rest);
                    task.sets.push_back(std::move(set));
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
                }
            }
            std::set<std::string> known;
            for (const std::string& n : task_builtin_values()) known.insert(n);
            for (const auto& [name, value] : task.variables) known.insert(name);
            for (const TaskAsk& a : task.asks) known.insert(a.name);
            for (const TaskSet& s : task.sets) known.insert(s.name);
            std::vector<std::string> texts = { task.label, task.description, task.confirm, task.cwd, task.version_file };
            for (const TaskAsk& a : task.asks) texts.push_back(a.value);
            for (const TaskSet& s : task.sets) texts.push_back(s.value);
            for (const TaskStep& s : task.steps) texts.push_back(s.text);
            std::set<std::string> reported;
            for (const std::string& t : texts) {
                const grove::bark::Interpolation result = grove::bark::interpolate_with(t, [&known](const std::string& name) -> std::optional<std::string> {
                    if (known.count(name)) return std::string();
                    return std::nullopt;
                });
                for (const std::string& name : result.unknown) {
                    if (!reported.insert(name).second) continue;
                    file.problems.push_back(task.id + " (line " + std::to_string(task.line) + "): {" + name + "} isn't set by an ask, set, $variable or Canopy, so it stays as typed (write {" + name + ":value} to give it a default)");
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
    if (const std::optional<std::string> text = read_text_file(path)) {
        file = parse_tasks(*text, path);
        file.shared_exists = true;
    }
    for (Task& t : file.tasks) t.file = path;
    file.local_path = local_tasks_path(workspace_root);
    if (const std::optional<std::string> text = read_text_file(file.local_path)) {
        TaskFile mine = parse_tasks(*text, file.local_path);
        file.local_exists = true;
        file.exists = true;
        for (const std::string& problem : mine.problems) file.problems.push_back("tasks.local.bark " + problem);
        for (Task& t : mine.tasks) {
            t.file = file.local_path;
            t.personal = true;
            auto same = std::find_if(file.tasks.begin(), file.tasks.end(), [&](const Task& o) { return o.id == t.id; });
            if (same != file.tasks.end()) *same = std::move(t);
            else file.tasks.push_back(std::move(t));
        }
    }
    return file;
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

grove::bark::Values task_scope(const Task& task, const TaskContext& context) {
    grove::bark::Values v;
    v.set("root", context.root);
    v.set("branch", context.branch);
    v.provide("date", [] { return today(); });
    v.set("file", context.file);
    if (!context.file.empty()) {
        const fs::path p(context.file);
        v.set("file-dir", p.parent_path().string());
        v.set("file-name", p.filename().string());
        v.set("file-stem", p.stem().string());
    } else {
        v.set("file-dir", std::string());
        v.set("file-name", std::string());
        v.set("file-stem", std::string());
    }
    auto version = std::make_shared<std::optional<std::string>>();
    auto current = [version, task, root = context.root]() {
        if (!*version) *version = current_version(task, root).value_or("0.0.0");
        return **version;
    };
    v.provide("current-version", current);
    v.provide("next-patch", [current] { return bump_version(current(), "patch"); });
    v.provide("next-minor", [current] { return bump_version(current(), "minor"); });
    v.provide("next-major", [current] { return bump_version(current(), "major"); });
    v.provide("version", current);
    for (const auto& [name, value] : task.variables) v.set(name, value);
    for (const auto& [key, value] : context.values) v.set(key, value);
    for (const TaskSet& s : task.sets) {
        if (context.values.count(s.name)) continue;
        if (s.only_default && (task.variables.count(s.name) || context.values.count(s.name))) continue;
        v.set(s.name, grove::bark::interpolate(s.value, v));
    }
    return v;
}

std::map<std::string, std::string> task_values(const Task& task, const TaskContext& context) {
    return task_scope(task, context).resolved();
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
