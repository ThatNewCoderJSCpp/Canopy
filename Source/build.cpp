#include <canopy/build.hpp>

#include <cstdlib>
#include <filesystem>

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

} // namespace canopy
