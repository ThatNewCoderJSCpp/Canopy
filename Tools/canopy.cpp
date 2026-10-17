#include <canopy/build.hpp>
#include <canopy/languages.hpp>
#include <canopy/settings.hpp>
#include <canopy/shell.hpp>
#include <canopy/toolchain.hpp>
#include <canopy/version.hpp>
#include <canopy/workspace.hpp>

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void print_usage(std::ostream& os) {
    os << "usage: canopy [command] [options]\n"
          "\n"
          "commands:\n"
          "  open [folder]          open the editor on a folder (the default command)\n"
          "  doctor [folder]        show which compilers and tools Canopy will use, and what's missing\n"
          "  settings [folder]      list every setting in effect and the file and line it came from\n"
          "      --path             print where your settings file lives\n"
          "      --create           create your settings file with every option listed, commented out\n"
          "  build <file>           build one C, C++, Walnut or Grove file\n"
          "  run <file> [-- args]   build one file and run it\n"
          "  languages              list the languages Canopy knows\n"
          "  dump [folder]          print the editor's element tree instead of opening a window\n"
          "  version                print the Canopy version\n"
          "\n"
          "build and run options:\n"
          "  -o <path>              where to write the program\n"
          "  --debug, --release     override build { mode: ...; }\n"
          "  -v, --verbose          print the compiler command\n";
}

void print_problems(const canopy::Workspace& w) {
    for (const canopy::ToolProblem& p : w.toolchain.problems) std::cerr << "canopy: " << p.message << '\n';
    w.diagnostics.print(std::cerr);
}

int doctor(const canopy::Workspace& w) {
    std::cout << "Canopy " << CANOPY_VERSION_STRING << "\n"
              << "workspace:      " << w.root << "\n"
              << "user settings:  " << canopy::user_settings_path() << (fs::exists(canopy::user_settings_path()) ? "" : "  (not created; using built-in values)") << "\n"
              << "workspace file: " << canopy::workspace_settings_path(w.root) << (fs::exists(canopy::workspace_settings_path(w.root)) ? "" : "  (none)") << "\n\n";

    const std::vector<std::pair<std::string, std::string>> tools = { { "c", "C" }, { "cpp", "C++" }, { "walnut", "Walnut" }, { "grove", "Grove" } };
    for (const auto& t : tools) {
        const canopy::Tool* tool = w.toolchain.get(t.first);
        std::cout << "  " << std::left << std::setw(8) << t.second;
        if (tool) std::cout << "ok       " << tool->path << "  (" << canopy::to_string(tool->origin) << ")\n";
        else std::cout << "MISSING\n";
    }
    if (!w.toolchain.problems.empty() || !w.diagnostics.empty()) {
        std::cout << "\nproblems:\n";
        for (const canopy::ToolProblem& p : w.toolchain.problems) std::cout << "  " << p.message << '\n';
        for (const grove::Diagnostic& d : w.diagnostics.items()) std::cout << "  " << d.file << ':' << d.span.begin.line << ": " << d.message << '\n';
    }
    const bool ready = w.toolchain.c && w.toolchain.cpp && w.toolchain.walnut && w.toolchain.grove;
    std::cout << '\n' << (ready ? "Everything's ready." : "Some languages can't be built until the problems above are fixed.") << '\n';
    return 0;
}

int settings(const canopy::Workspace& w, const std::vector<std::string>& args) {
    const std::string path = canopy::user_settings_path();
    for (const std::string& a : args) {
        if (a == "--path") {
            std::cout << path << '\n';
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
            out << canopy::user_settings_template();
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
    for (const canopy::Setting& s : w.settings.all()) {
        if (s.section != section) {
            section = s.section;
            std::cout << '\n' << section << '\n';
        }
        std::cout << "  " << std::left << std::setw(18) << s.key << std::setw(30) << (s.quoted ? "\"" + s.value + "\"" : s.value);
        if (s.source == canopy::SettingSource::Default) std::cout << "default\n";
        else std::cout << canopy::to_string(s.source) << "  " << s.file << ':' << s.line << '\n';
    }
    return 0;
}

int build(const std::vector<std::string>& args, bool run) {
    canopy::BuildRequest request;
    bool verbose = false;
    std::vector<std::string> program_args;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--") {
            program_args.assign(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
            break;
        }
        if ((a == "-o" || a == "--output") && i + 1 < args.size()) request.output = args[++i];
        else if (a == "--debug") request.mode = canopy::BuildMode::Debug;
        else if (a == "--release") request.mode = canopy::BuildMode::Release;
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
    const canopy::Workspace w = canopy::open_workspace(request.file);
    request.workspace_root = w.root;
    w.diagnostics.print(std::cerr);

    const canopy::BuildPlan plan = canopy::plan_build(request, w.settings, w.toolchain);
    if (!plan.ok()) {
        std::cerr << "canopy: " << plan.error << '\n';
        return 1;
    }
    fs::create_directories(fs::path(plan.output).parent_path(), ec);
    if (verbose) std::cerr << canopy::command_line(plan.command) << '\n';
    const int status = canopy::run_command(plan.command);
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
    return canopy::run_command(command);
}

int languages() {
    for (const canopy::Language& l : canopy::builtin_languages()) {
        std::cout << std::left << std::setw(8) << l.name << std::setw(8) << l.key;
        for (const std::string& e : l.extensions) std::cout << e << ' ';
        std::cout << '\n';
    }
    return 0;
}

std::string folder_argument(const std::vector<std::string>& args) {
    for (const std::string& a : args) {
        if (a.empty() || a[0] != '-') return a;
    }
    return std::string();
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
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
        if (first == "open" || first == "doctor" || first == "settings" || first == "build" || first == "run" || first == "languages" || first == "dump") {
            command = first;
            args.erase(args.begin());
        }
    }

    if (command == "build") return build(args, false);
    if (command == "run") return build(args, true);
    if (command == "languages") return languages();

    const canopy::Workspace w = canopy::open_workspace(folder_argument(args));
    if (command == "doctor") return doctor(w);
    if (command == "settings") return settings(w, args);
    if (command == "dump") return canopy::dump_shell(w, std::cout);

    print_problems(w);
    return canopy::run_shell(w);
}
