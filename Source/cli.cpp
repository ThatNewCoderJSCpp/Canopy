#include <canopy/cli.hpp>

#include <canopy/askpass.hpp>

#include <canopy/build.hpp>
#include <canopy/desktop.hpp>
#include <canopy/languages.hpp>
#include <canopy/settings.hpp>
#include <canopy/shell.hpp>
#include <canopy/toolchain.hpp>
#include <canopy/version.hpp>
#include <canopy/viewers.hpp>
#include <canopy/workspace.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace canopy {

namespace {

void print_usage(std::ostream& os) {
    os << "usage: canopy [command] [options]\n"
          "\n"
          "commands:\n"
          "  [folder or files]      open the editor (the default command); returns straight away like `code .`\n"
          "      -w, --wait         stay in the foreground until the editor closes\n"
          "  doctor [folder]        show which compilers and tools Canopy will use, and what's missing\n"
          "  settings [folder]      list every setting in effect and the file and line it came from\n"
          "      --dir              print the folder your settings files (editor.bark, compilers.bark, ...) live in\n"
          "      --path             print where the single-file settings.bark lives (still read if it exists)\n"
          "      --create           create that settings.bark with every option listed, commented out\n"
          "  build <file>           build one C, C++, Walnut or Grove file\n"
          "  run <file> [-- args]   build one file and run it\n"
          "  languages              list the languages Canopy knows\n"
          "  dump [folder]          print the editor's element tree instead of opening a window\n"
          "  install                put the canopy command on your PATH and Canopy in your applications menu\n"
          "      --desktop          also put a shortcut on your desktop\n"
          "      --no-command, --no-menu, --no-context-menu, --no-file-types   skip one of those\n"
          "      --system [--prefix /usr]   for every user (Linux; needs root, used by the .deb and .rpm)\n"
          "  uninstall              undo canopy install (same options)\n"
          "  version                print the Canopy version\n"
          "\n"
          "build and run options:\n"
          "  -o <path>              where to write the program\n"
          "  --debug, --release     override build { mode: ...; }\n"
          "  -v, --verbose          print the compiler command\n";
}

void print_problems(const Workspace& w) {
    for (const ToolProblem& p : w.toolchain.problems) std::cerr << "canopy: " << p.message << '\n';
    w.diagnostics.print(std::cerr);
}

int doctor(const Workspace& w) {
    std::cout << "Canopy " << CANOPY_VERSION_STRING << "\n"
              << "workspace:      " << w.root << "\n"
              << "user settings:  " << user_config_dir() << (user_settings_files().empty() ? "  (nothing changed yet; using built-in values)" : "") << "\n"
              << "workspace file: " << workspace_settings_path(w.root) << (fs::exists(workspace_settings_path(w.root)) ? "" : "  (none)") << "\n\n";

    const std::vector<std::pair<std::string, std::string>> tools = { { "c", "C" }, { "cpp", "C++" }, { "walnut", "Walnut" }, { "grove", "Grove" } };
    for (const auto& t : tools) {
        const Tool* tool = w.toolchain.get(t.first);
        std::cout << "  " << std::left << std::setw(8) << t.second;
        if (tool) std::cout << "ok       " << tool->path << "  (" << to_string(tool->origin) << ")\n";
        else std::cout << "MISSING\n";
    }
    for (const auto& v : std::vector<std::pair<std::string, std::string>>{ { "pdf", "PDF" }, { "latex", "LaTeX" } }) {
        const std::optional<ViewerTool> tool = find_viewer_tool(w.settings, v.first);
        std::cout << "  " << std::left << std::setw(8) << v.second;
        if (tool) std::cout << "ok       " << tool->path << "  (viewer)\n";
        else std::cout << "none     optional: " << viewer_install_hint(v.first) << "\n";
    }
    if (!w.toolchain.problems.empty() || !w.diagnostics.empty()) {
        std::cout << "\nproblems:\n";
        for (const ToolProblem& p : w.toolchain.problems) std::cout << "  " << p.message << '\n';
        for (const grove::Diagnostic& d : w.diagnostics.items()) std::cout << "  " << d.file << ':' << d.span.begin.line << ": " << d.message << '\n';
    }
    const IntegrationStatus status = integration_status();
    std::cout << "\n  command  " << (status.command_path.empty() ? std::string("not on PATH (run: canopy install)") : status.command_path + (status.command_is_this ? "" : "  (a different copy)")) << '\n'
              << "  menu     " << (status.menu_entry.empty() ? std::string("not installed (run: canopy install)") : status.menu_entry) << '\n'
              << "  desktop  " << (status.desktop_shortcut.empty() ? std::string("no shortcut (run: canopy install --desktop)") : status.desktop_shortcut) << '\n';
    const bool ready = w.toolchain.c && w.toolchain.cpp && w.toolchain.walnut && w.toolchain.grove;
    std::cout << '\n' << (ready ? "Everything's ready." : "Some languages can't be built until the problems above are fixed.") << '\n';
    return 0;
}

int settings(const Workspace& w, const std::vector<std::string>& args) {
    const std::string path = user_settings_path();
    for (const std::string& a : args) {
        if (a == "--path") {
            std::cout << path << '\n';
            return 0;
        }
        if (a == "--dir") {
            std::cout << user_config_dir() << '\n';
            return 0;
        }
        if (a == "--create") {
            if (fs::exists(path)) {
                std::cout << path << " already exists\n";
                return 0;
            }
            std::error_code ec;
            fs::create_directories(fs::path(path).parent_path(), ec);
            std::ofstream out(path);
            out << user_settings_template();
            if (!out) {
                std::cerr << "canopy: can't write " << path << '\n';
                return 1;
            }
            std::cout << "created " << path << '\n';
            return 0;
        }
    }
    w.diagnostics.print(std::cerr);
    std::string section;
    for (const Setting& s : w.settings.all()) {
        if (s.section != section) {
            section = s.section;
            std::cout << '\n' << section << '\n';
        }
        std::cout << "  " << std::left << std::setw(18) << s.key << std::setw(30) << (s.quoted ? "\"" + s.value + "\"" : s.value);
        if (s.source == SettingSource::Default) std::cout << "default\n";
        else std::cout << to_string(s.source) << "  " << s.file << ':' << s.line << '\n';
    }
    return 0;
}

int build(const std::vector<std::string>& args, bool run) {
    BuildRequest request;
    bool verbose = false;
    std::vector<std::string> program_args;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--") {
            program_args.assign(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
            break;
        }
        if ((a == "-o" || a == "--output") && i + 1 < args.size()) request.output = args[++i];
        else if (a == "--debug") request.mode = BuildMode::Debug;
        else if (a == "--release") request.mode = BuildMode::Release;
        else if (a == "-v" || a == "--verbose") verbose = true;
        else if (request.file.empty()) request.file = a;
        else {
            std::cerr << "canopy: unexpected argument '" << a << "'\n";
            return 2;
        }
    }
    if (request.file.empty()) {
        std::cerr << "canopy: " << (run ? "run" : "build") << " needs a file\n";
        return 2;
    }
    std::error_code ec;
    request.file = fs::absolute(request.file, ec).string();
    const Workspace w = open_workspace(request.file);
    request.workspace_root = w.root;
    w.diagnostics.print(std::cerr);

    const BuildPlan plan = plan_build(request, w.settings, w.toolchain);
    if (!plan.ok()) {
        std::cerr << "canopy: " << plan.error << '\n';
        return 1;
    }
    fs::create_directories(fs::path(plan.output).parent_path(), ec);
    if (verbose) std::cerr << command_line(plan.command) << '\n';
    const int status = run_command(plan.command);
    if (status != 0) {
        std::cerr << "canopy: build failed (exit " << status << ")\n";
        return status;
    }
    if (!run) {
        std::cerr << "canopy: built " << plan.output << '\n';
        return 0;
    }
    std::vector<std::string> command = { fs::absolute(plan.output, ec).string() };
    command.insert(command.end(), program_args.begin(), program_args.end());
    return run_command(command);
}

int languages() {
    for (const Language& l : builtin_languages()) {
        std::cout << std::left << std::setw(8) << l.name << std::setw(8) << l.key;
        for (const std::string& e : l.extensions) std::cout << e << ' ';
        std::cout << '\n';
    }
    return 0;
}

int integrate(const std::vector<std::string>& args, bool install) {
    InstallOptions o;
    bool quiet = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--desktop") o.desktop_shortcut = true;
        else if (a == "--no-command") o.command = false;
        else if (a == "--no-menu") o.menu = false;
        else if (a == "--no-context-menu") o.context_menu = false;
        else if (a == "--no-file-types") o.file_types = false;
        else if (a == "--system") o.scope = InstallScope::System;
        else if (a == "--prefix" && i + 1 < args.size()) o.prefix = args[++i];
        else if (a == "-q" || a == "--quiet") quiet = true;
        else {
            std::cerr << "canopy: unknown option '" << a << "'\n";
            return 2;
        }
    }
    const InstallReport report = install ? install_integration(o) : uninstall_integration(o);
    if (!quiet) {
        for (const InstallStep& s : report.steps) {
            std::cout << (s.ok ? "  ok      " : "  FAILED  ") << s.what;
            if (!s.path.empty()) std::cout << "  " << s.path;
            if (!s.note.empty()) std::cout << "  (" << s.note << ")";
            std::cout << '\n';
        }
        for (const std::string& h : report.hints) std::cout << '\n' << h << '\n';
        if (install && report.ok()) std::cout << "\nDone. Type `canopy .` in a folder to open it, or find Canopy in your applications menu.\n";
    }
    return report.ok() ? 0 : 1;
}

struct OpenTargets {
    std::string folder;
    std::vector<std::string> files;
    bool wait = false;
};

OpenTargets open_targets(const std::vector<std::string>& args) {
    OpenTargets t;
    std::error_code ec;
    for (const std::string& a : args) {
        if (a == "-w" || a == "--wait") {
            t.wait = true;
            continue;
        }
        if (a.empty() || a[0] == '-') continue;
        const fs::path p = fs::absolute(a, ec);
        if (fs::is_directory(p, ec)) {
            if (t.folder.empty()) t.folder = p.string();
        } else {
            t.files.push_back(p.string());
        }
    }
    if (t.folder.empty() && !t.files.empty()) t.folder = t.files.front();
    return t;
}

std::string folder_argument(const std::vector<std::string>& args) {
    for (const std::string& a : args) {
        if (a.empty() || a[0] != '-') return a;
    }
    return std::string();
}


} // namespace

int run_cli(std::vector<std::string> args, const CliOptions& cli) {
    if (const char* askpass = std::getenv("CANOPY_ASKPASS"); askpass && *askpass) return canopy::run_askpass(args.empty() ? std::string() : args.front());
    std::string command = "open";
    if (!args.empty()) {
        const std::string& first = args.front();
        if (first == "-h" || first == "--help" || first == "help") {
            print_usage(std::cout);
            return 0;
        }
        if (first == "--version" || first == "version") {
            std::cout << "canopy " << CANOPY_VERSION_STRING << '\n';
            return 0;
        }
        if (first == "open" || first == "doctor" || first == "settings" || first == "build" || first == "run" || first == "languages" || first == "dump" || first == "install" || first == "uninstall") {
            command = first;
            args.erase(args.begin());
        }
    }

    if (command == "build") return build(args, false);
    if (command == "run") return build(args, true);
    if (command == "languages") return languages();
    if (command == "install") return integrate(args, true);
    if (command == "uninstall") return integrate(args, false);

    if (command == "open") {
        const OpenTargets targets = open_targets(args);
        const bool detach = !targets.wait && !cli.gui && has_display() && std::getenv("CANOPY_NO_DETACH") == nullptr;
        if (detach) {
            std::vector<std::string> forwarded{ "--wait" };
            for (const std::string& a : args) {
                if (a != "-w" && a != "--wait") forwarded.push_back(a);
            }
            if (launch_detached(canopy_app_executable(), forwarded)) return 0;
        }
        const canopy::Workspace w = canopy::open_workspace(targets.folder);
        print_problems(w);
        return canopy::run_shell(w, targets.files);
    }

    const canopy::Workspace w = canopy::open_workspace(folder_argument(args));
    if (command == "doctor") return doctor(w);
    if (command == "settings") return settings(w, args);
    if (command == "dump") return canopy::dump_shell(w, std::cout);
    return 2;
}

std::vector<std::string> command_line_arguments(int argc, char** argv) {
#if defined(_WIN32)
    (void)argc;
    (void)argv;
    std::vector<std::string> out;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return out;
    for (int i = 1; i < count; ++i) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? static_cast<std::size_t>(n - 1) : 0, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, s.data(), n, nullptr, nullptr);
        out.push_back(std::move(s));
    }
    LocalFree(wide);
    return out;
#else
    return std::vector<std::string>(argv + 1, argv + argc);
#endif
}

} // namespace canopy
