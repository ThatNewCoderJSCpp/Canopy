#include <canopy/build.hpp>
#include <canopy/asm_info.hpp>

#include <algorithm>
#include <cctype>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
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


std::string missing_optional(const std::string& what, const std::string& key) {
    const std::string hint = tool_install_hint(key);
    return what + " needs " + key + ", which Canopy can't find" + (hint.empty() ? std::string() : "; " + hint) + ". Settings > Compilers can point Canopy at it.";
}

std::string read_whole(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string find_upward(const std::string& file, const std::string& root, const std::function<bool(const fs::path&)>& match) {
    std::error_code ec;
    const fs::path stop = root.empty() ? fs::path() : fs::weakly_canonical(fs::path(root), ec);
    for (fs::path d = fs::path(file).parent_path(); !d.empty(); d = d.parent_path()) {
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec) && match(it->path())) return it->path().string();
        }
        if (!stop.empty() && fs::weakly_canonical(d, ec) == stop) break;
        if (d == d.parent_path()) break;
    }
    return std::string();
}

std::string find_named_upward(const std::string& file, const std::string& root, const std::string& name) {
    return find_upward(file, root, [&](const fs::path& p) { return p.filename() == name; });
}

int dotnet_major(const std::string& dotnet) {
    static std::map<std::string, int> cache;
    const auto it = cache.find(dotnet);
    if (it != cache.end()) return it->second;
    std::string out;
    run_command_capture({ dotnet, "--version" }, out);
    int major = 0;
    std::size_t i = 0;
    while (i < out.size() && !std::isdigit(static_cast<unsigned char>(out[i]))) ++i;
    while (i < out.size() && std::isdigit(static_cast<unsigned char>(out[i]))) major = major * 10 + (out[i++] - '0');
    cache[dotnet] = major;
    return major;
}

std::string run_folder(const BuildRequest& request) {
    if (!request.workspace_root.empty()) {
        const std::string root = fs::path(request.workspace_root).lexically_normal().string();
        const std::string file = fs::path(request.file).lexically_normal().string();
        if (file.rfind(root, 0) == 0) return request.workspace_root;
    }
    return fs::path(request.file).parent_path().string();
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

std::vector<std::string> walnut_safety_flags(const Settings& settings) {
    std::vector<std::string> out;
    std::vector<std::string> picked;
    for (std::string word : split_flags(settings.get_or("language.walnut", "checks", "off"))) {
        word.erase(std::remove(word.begin(), word.end(), ','), word.end());
        if (word == "all") picked = { "bounds", "null", "dangling", "overflow" };
        else if ((word == "bounds" || word == "null" || word == "dangling" || word == "overflow") && std::find(picked.begin(), picked.end(), word) == picked.end()) picked.push_back(word);
    }
    if (picked.size() == 4) out.push_back("--checks");
    else for (const std::string& word : picked) out.push_back("--check-" + word);
    const std::string leaks = settings.get_or("language.walnut", "leak-check", "off");
    if (leaks == "warn" || leaks == "error") out.push_back("--leak-check=" + leaks);
    if (settings.flag("language.walnut", "gc", false)) out.push_back("--gc");
    if (settings.flag("language.walnut", "ownership", false)) out.push_back("--ownership");
    if (settings.flag("language.walnut", "unused-warnings", false)) out.push_back("--warn-unused");
    return out;
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
    plan.output = request.output.empty() ? default_output_path(request, settings) : with_exe_suffix(request.output);

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
        append(plan.command, walnut_safety_flags(settings));
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
    case BuildKind::Node: {
        const Tool* node = toolchain.get("node");
        if (!node) {
            plan.error = "JavaScript files run with Node.js, which Canopy can't find. Install it from https://nodejs.org (or your package manager), or set toolchain { node: \"/path/to/node\"; } under Settings > Compilers";
            return plan;
        }
        plan.command = { node->path, "--check", request.file };
        plan.output.clear();
        break;
    }
    case BuildKind::Rust: {
        const std::string manifest = find_named_upward(request.file, request.workspace_root, "Cargo.toml");
        const Tool* cargo = toolchain.get("cargo");
        if (!manifest.empty() && cargo) {
            plan.command = { cargo->path, "build", "--manifest-path", manifest };
            if (mode == BuildMode::Release) plan.command.push_back("--release");
            append(plan.command, flags);
            plan.run = { cargo->path, "run", "--quiet", "--manifest-path", manifest };
            if (mode == BuildMode::Release) plan.run.push_back("--release");
            plan.folder = fs::path(manifest).parent_path().string();
            plan.output.clear();
            break;
        }
        const Tool* rustc = toolchain.get("rustc");
        if (!rustc) {
            plan.error = missing_optional("Building Rust", manifest.empty() ? "rustc" : "cargo");
            return plan;
        }
        plan.command = { rustc->path, "--edition", settings.get_or(section, "edition", "2021"), request.file, "-o", plan.output };
        plan.command.push_back(mode == BuildMode::Release ? "-O" : "-g");
        append(plan.command, flags);
        append(plan.command, mode_flags);
        plan.folder = run_folder(request);
        break;
    }
    case BuildKind::Zig: {
        const Tool* zig = toolchain.get("zig");
        if (!zig) {
            plan.error = missing_optional("Building Zig", "zig");
            return plan;
        }
        const std::string build_file = find_named_upward(request.file, request.workspace_root, "build.zig");
        if (!build_file.empty()) {
            plan.command = { zig->path, "build", "--build-file", build_file };
            if (mode == BuildMode::Release) plan.command.push_back("-Doptimize=ReleaseFast");
            plan.run = plan.command;
            plan.run.insert(plan.run.begin() + 2, "run");
            plan.folder = fs::path(build_file).parent_path().string();
            plan.output.clear();
            break;
        }
        const fs::path cache = fs::path(plan.output).parent_path() / "zig-cache";
        plan.command = { zig->path, "build-exe", request.file, "-femit-bin=" + plan.output, "--cache-dir", cache.string() };
        if (mode == BuildMode::Release) {
            plan.command.push_back("-O");
            plan.command.push_back("ReleaseFast");
        }
        append(plan.command, flags);
        append(plan.command, mode_flags);
        plan.folder = run_folder(request);
        break;
    }
    case BuildKind::Python: {
        const Tool* python = toolchain.get("python");
        if (!python) {
            plan.error = missing_optional("Running Python", "python");
            return plan;
        }
        plan.command = { python->path, "-c", "import ast,sys;f=sys.argv[1];ast.parse(open(f,encoding='utf-8').read(),f)", request.file };
        plan.run = { python->path, "-u" };
        append(plan.run, flags);
        plan.run.push_back(request.file);
        plan.folder = run_folder(request);
        plan.output.clear();
        break;
    }
    case BuildKind::Lua: {
        const Tool* lua = toolchain.get("lua");
        if (!lua) {
            plan.error = missing_optional("Running Lua", "lua");
            return plan;
        }
        if (const Tool* luac = toolchain.get("luac")) plan.command = { luac->path, "-p", request.file };
        else plan.command = { lua->path, "-e", "local f,e=loadfile([==[" + request.file + "]==]) if not f then io.stderr:write(e,'\\n') os.exit(1) end" };
        plan.run = { lua->path, request.file };
        plan.folder = run_folder(request);
        plan.output.clear();
        break;
    }
    case BuildKind::Java: {
        const Tool* javac = toolchain.get("javac");
        const Tool* java = toolchain.get("java");
        if (!javac || !java) {
            plan.error = missing_optional("Building Java", javac ? "java" : "javac");
            return plan;
        }
        const std::string text = read_whole(request.file);
        std::smatch m;
        std::string package;
        static const std::regex package_re(R"((?:^|\n)\s*package\s+([A-Za-z_][\w.]*)\s*;)");
        if (std::regex_search(text, m, package_re)) package = m[1].str();
        fs::path root = fs::path(request.file).parent_path();
        if (!package.empty()) {
            const std::size_t depth = static_cast<std::size_t>(std::count(package.begin(), package.end(), '.')) + 1;
            for (std::size_t k = 0; k < depth && root.has_parent_path(); ++k) root = root.parent_path();
        }
        const fs::path classes = fs::path(plan.output).parent_path() / "java-classes";
        const std::string main = (package.empty() ? std::string() : package + ".") + fs::path(request.file).stem().string();
        plan.command = { javac->path, "-d", classes.string(), "-sourcepath", root.string(), "-encoding", "UTF-8" };
        if (mode == BuildMode::Debug) plan.command.push_back("-g");
        append(plan.command, flags);
        append(plan.command, mode_flags);
        plan.command.push_back(request.file);
        plan.run = { java->path, "-cp", classes.string(), main };
        plan.folder = run_folder(request);
        plan.output = classes.string();
        break;
    }
    case BuildKind::CSharp: {
        const std::string project = find_upward(request.file, request.workspace_root, [](const fs::path& p) { return p.extension() == ".csproj"; });
        const Tool* dotnet = toolchain.get("dotnet");
        const std::string config = mode == BuildMode::Release ? "Release" : "Debug";
        if (dotnet && !project.empty()) {
            plan.command = { dotnet->path, "build", project, "-c", config, "-nologo", "-v", "q", "-clp:NoSummary" };
            append(plan.command, flags);
            plan.run = { dotnet->path, "run", "--project", project, "-c", config, "--no-build" };
            plan.folder = fs::path(project).parent_path().string();
            plan.output.clear();
            break;
        }
        if (dotnet && dotnet_major(dotnet->path) >= 10) {
            plan.command = { dotnet->path, "build", request.file, "-c", config, "-nologo", "-v", "q", "-clp:NoSummary" };
            plan.run = { dotnet->path, "run", request.file, "-c", config, "--no-build" };
            plan.folder = run_folder(request);
            plan.output.clear();
            break;
        }
        if (const Tool* mcs = toolchain.get("mcs")) {
            std::string out = plan.output;
            if (fs::path(out).extension() != ".exe") out += ".exe";
            plan.output = out;
            plan.command = { mcs->path, "-nologo", "-out:" + out, mode == BuildMode::Release ? "-optimize+" : "-debug" };
            append(plan.command, flags);
            plan.command.push_back(request.file);
#if defined(_WIN32)
            plan.run = { out };
#else
            if (const Tool* mono = toolchain.get("mono")) plan.run = { mono->path, out };
            else plan.run_error = "It built, but running a Mono .exe needs mono; " + tool_install_hint("mono");
#endif
            plan.folder = run_folder(request);
            break;
        }
        if (dotnet) {
            plan.error = fs::path(request.file).filename().string() + " isn't in a .NET project. Run `dotnet new console` in its folder to make one (Run then builds and runs it), install .NET 10 or newer to run a single .cs file, or install Mono's mcs.";
            return plan;
        }
        plan.error = missing_optional("Building C#", "dotnet");
        return plan;
    }
    case BuildKind::Assembly: {
        const std::string text = read_whole(request.file);
        const AsmInfo info = detect_assembly(text, request.file, settings.get_or(section, "dialect", "auto"), settings.get_or(section, "arch", "auto"));
        plan.note = assembly_report(info, toolchain);
        const AsmArch host = host_arch();
        const bool native = info.arch == host || (host == AsmArch::X86_64 && info.arch == AsmArch::X86);
        const bool linux_like =
#if defined(__linux__)
            true;
#else
            false;
#endif
        const bool start_entry = info.entry == "_start" || info.entry == "start";
        plan.folder = run_folder(request);
        auto qemu_for = [&]() -> const Tool* {
            switch (info.arch) {
            case AsmArch::Arm64: return toolchain.get("qemu-aarch64");
            case AsmArch::Arm: return toolchain.get("qemu-arm");
            case AsmArch::RiscV64: return toolchain.get("qemu-riscv64");
            case AsmArch::Mips: return toolchain.get("qemu-mips");
            default: return toolchain.get("qemu-x86_64");
            }
        };
        if (info.dialect == AsmDialect::Nasm || info.dialect == AsmDialect::Fasm || info.dialect == AsmDialect::Masm) {
            if (info.arch != AsmArch::X86_64 && info.arch != AsmArch::X86) {
                plan.error = std::string(to_string(info.dialect)) + " only assembles x86 code; this file uses " + to_string(info.arch) + ". " + plan.note;
                return plan;
            }
        }
        if (info.dialect == AsmDialect::Fasm) {
            const Tool* fasm = toolchain.get("fasm");
            if (!fasm) {
                plan.error = plan.note;
                return plan;
            }
            plan.command = { fasm->path, request.file, plan.output };
            break;
        }
        if (info.dialect == AsmDialect::Masm) {
            const Tool* masm = toolchain.get("masm");
            if (!masm) {
                plan.error = plan.note;
                return plan;
            }
            plan.command = { masm->path, "/nologo" };
            if (mode == BuildMode::Debug) plan.command.push_back("/Zi");
            append(plan.command, flags);
            plan.command.push_back(request.file);
            plan.command.push_back("/Fe" + plan.output);
            plan.command.push_back("/link");
            plan.command.push_back("/subsystem:console");
            plan.command.push_back("/entry:" + (info.entry.empty() ? std::string("main") : info.entry));
            break;
        }
        const Tool* cc = toolchain.get("c");
        if (info.dialect == AsmDialect::Nasm) {
            const Tool* nasm = toolchain.get("nasm");
            if (!nasm) nasm = toolchain.get("yasm");
            if (!nasm) {
                plan.error = plan.note;
                return plan;
            }
            const bool x86 = info.arch == AsmArch::X86;
#if defined(_WIN32)
            const std::string format = x86 ? "win32" : "win64";
            const std::string object = plan.output.substr(0, plan.output.size() - (fs::path(plan.output).extension() == ".exe" ? 4 : 0)) + ".obj";
#elif defined(__APPLE__)
            const std::string format = x86 ? "macho32" : "macho64";
            const std::string object = plan.output + ".o";
#else
            const std::string format = x86 ? "elf32" : "elf64";
            const std::string object = plan.output + ".o";
#endif
            plan.command = { nasm->path, "-f", format };
            if (mode == BuildMode::Debug) plan.command.push_back("-g");
            append(plan.command, flags);
            plan.command.push_back(request.file);
            plan.command.push_back("-o");
            plan.command.push_back(object);
            const Tool* ld = toolchain.get("ld");
            if (start_entry && linux_like && ld) {
                std::vector<std::string> link = { ld->path };
                if (x86) {
                    link.push_back("-m");
                    link.push_back("elf_i386");
                }
                link.push_back(object);
                link.push_back("-o");
                link.push_back(plan.output);
                if (info.entry != "_start") link.push_back("-e" + info.entry);
                plan.more_steps.push_back(link);
            } else if (cc) {
                std::vector<std::string> link = { cc->path, object, "-o", plan.output };
                if (x86) link.push_back("-m32");
                if (linux_like) link.push_back("-no-pie");
                if (start_entry) {
                    link.push_back("-nostartfiles");
                    link.push_back("-Wl,-e," + info.entry);
                    if (linux_like) {
                        link.push_back("-nostdlib");
                        link.push_back("-static");
                    }
                }
                plan.more_steps.push_back(link);
            } else {
                plan.error = "NASM made the object file, but linking it needs a C compiler or ld: " + missing_tool(toolchain, "c");
                return plan;
            }
            break;
        }
        const Tool* gcc = native ? cc : nullptr;
        if (!native) {
            switch (info.arch) {
            case AsmArch::Arm64: gcc = toolchain.get("gcc-aarch64"); break;
            case AsmArch::Arm: gcc = toolchain.get("gcc-arm"); break;
            case AsmArch::RiscV64: gcc = toolchain.get("gcc-riscv64"); break;
            case AsmArch::Mips: gcc = toolchain.get("gcc-mips"); break;
            default: gcc = toolchain.get("gcc-x86_64"); break;
            }
        }
        if (!gcc) {
            plan.error = plan.note;
            return plan;
        }
        plan.command = { gcc->path };
        if (mode == BuildMode::Debug) plan.command.push_back("-g");
        if (info.arch == AsmArch::X86 && native) plan.command.push_back("-m32");
        if (start_entry) {
            plan.command.push_back("-nostdlib");
            plan.command.push_back("-nostartfiles");
            if (linux_like || !native) plan.command.push_back("-static");
            if (info.entry != "_start") plan.command.push_back("-Wl,-e," + info.entry);
        } else if (!native) {
            plan.command.push_back("-static");
        } else if (linux_like) {
            plan.command.push_back("-no-pie");
        }
        if (linux_like) plan.command.push_back("-Wl,-z,noexecstack");
        append(plan.command, flags);
        append(plan.command, mode_flags);
        plan.command.push_back(request.file);
        plan.command.push_back("-o");
        plan.command.push_back(plan.output);
        if (!native) {
            if (const Tool* qemu = qemu_for()) plan.run = { qemu->path, plan.output };
            else plan.run_error = std::string("It built, but this is ") + to_string(info.arch) + " code and this computer is " + to_string(host) + "; running it needs an emulator: " + tool_install_hint("qemu-");
        }
        break;
    }
    case BuildKind::Web:
        plan.error = "HTML pages aren't built; press Run to open " + fs::path(request.file).filename().string() + " in the local server";
        return plan;
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
    return run_command_capture_input(command, input, output, false);
}

int run_command_capture_input(const std::vector<std::string>& command, const std::string& input, std::string& output, bool with_errors) {
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
    const std::string line = "\"" + command_line(command) + " < " + quote_argument(temp.string()) + (with_errors ? " 2>&1\"" : " 2>NUL\"");
    FILE* pipe = _popen(line.c_str(), "r");
#else
    const std::string line = command_line(command) + " < " + quote_argument(temp.string()) + (with_errors ? " 2>&1" : " 2>/dev/null");
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

namespace {

bool all_digits(const std::string& s) {
    return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
}

std::optional<CompilerMessage> other_message(const std::vector<std::string>& lines, std::size_t i) {
    const std::string& line = lines[i];
    if ((line.rfind("error", 0) == 0 || line.rfind("warning", 0) == 0) && line.find(": ") != std::string::npos && line.rfind("error: could not compile", 0) != 0 &&
        line.rfind("warning: build failed", 0) != 0) {
        const std::string head = line.substr(0, line.find(": "));
        const bool rust_head = head == "error" || head == "warning" || (head.find('[') != std::string::npos && head.back() == ']');
        if (rust_head && i + 1 < lines.size()) {
            const std::string& next = lines[i + 1];
            const std::size_t arrow = next.find("--> ");
            if (arrow != std::string::npos && next.find_first_not_of(' ') == arrow) {
                std::string where = next.substr(arrow + 4);
                CompilerMessage m;
                m.severity = line.rfind("error", 0) == 0 ? "error" : "warning";
                m.message = line.substr(line.find(": ") + 2);
                if (head.find('[') != std::string::npos) m.message += " [" + head.substr(head.find('[') + 1, head.size() - head.find('[') - 2) + "]";
                std::vector<std::string> parts;
                for (int k = 0; k < 2; ++k) {
                    const std::size_t colon = where.rfind(':');
                    if (colon == std::string::npos || !all_digits(where.substr(colon + 1))) break;
                    parts.insert(parts.begin(), where.substr(colon + 1));
                    where = where.substr(0, colon);
                }
                if (!parts.empty()) {
                    m.file = where;
                    m.line = static_cast<std::size_t>(std::stoul(parts[0]));
                    if (parts.size() > 1) m.column = static_cast<std::size_t>(std::stoul(parts[1]));
                    return m;
                }
            }
        }
    }
    {
        const std::size_t paren = line.find('(');
        const std::size_t close = paren == std::string::npos ? std::string::npos : line.find("): ", paren);
        if (paren != std::string::npos && close != std::string::npos && paren > 0) {
            const std::string inside = line.substr(paren + 1, close - paren - 1);
            const std::string rest = line.substr(close + 3);
            const bool cs = rest.rfind("error ", 0) == 0 || rest.rfind("warning ", 0) == 0;
            const bool masm = rest.rfind("error A", 0) == 0 || rest.rfind("warning A", 0) == 0 || rest.rfind(" error A", 0) == 0;
            const std::size_t comma = inside.find(',');
            const std::string l = inside.substr(0, comma);
            const std::string c = comma == std::string::npos ? std::string() : inside.substr(comma + 1);
            if ((cs || masm) && all_digits(l) && (c.empty() || all_digits(c))) {
                CompilerMessage m;
                m.file = line.substr(0, paren);
                while (!m.file.empty() && m.file.back() == ' ') m.file.pop_back();
                m.line = static_cast<std::size_t>(std::stoul(l));
                if (!c.empty()) m.column = static_cast<std::size_t>(std::stoul(c));
                m.severity = rest.find("warning") == 0 ? "warning" : "error";
                std::string msg = rest.substr(rest.find(' ') + 1);
                const std::size_t project = msg.rfind(" [");
                if (project != std::string::npos && msg.back() == ']') msg = msg.substr(0, project);
                m.message = msg;
                return m;
            }
        }
    }
    {
        const std::size_t file_at = line.find("File \"");
        if (file_at != std::string::npos && line.find_first_not_of(' ') == file_at) {
            const std::size_t end = line.find('"', file_at + 6);
            const std::size_t ln = line.find(", line ", end == std::string::npos ? 0 : end);
            if (end != std::string::npos && ln != std::string::npos) {
                std::size_t k = ln + 7;
                std::string num;
                while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k]))) num += line[k++];
                bool last = true;
                for (std::size_t j = i + 1; j < lines.size() && j < i + 8; ++j) {
                    const std::size_t other = lines[j].find("File \"");
                    if (other != std::string::npos && lines[j].find_first_not_of(' ') == other) {
                        last = false;
                        break;
                    }
                    const std::string& t = lines[j];
                    if (!t.empty() && t[0] != ' ' && (t.find("Error") != std::string::npos || t.find("Exception") != std::string::npos || t.find("Warning:") != std::string::npos) && t.find(": ") != std::string::npos) break;
                }
                const std::string file = line.substr(file_at + 6, end - file_at - 6);
                if (last && all_digits(num) && file.find('<') == std::string::npos) {
                    CompilerMessage m;
                    m.file = file;
                    m.line = static_cast<std::size_t>(std::stoul(num));
                    m.severity = "error";
                    for (std::size_t j = i + 1; j < lines.size() && j < i + 8; ++j) {
                        const std::string& t = lines[j];
                        if (!t.empty() && t[0] != ' ' && t.find(": ") != std::string::npos) {
                            m.message = t;
                            break;
                        }
                        if (!t.empty() && t[0] != ' ' && (t.find("Error") != std::string::npos || t.find("Exception") != std::string::npos)) {
                            m.message = t;
                            break;
                        }
                        const std::size_t caret = t.find('^');
                        if (caret != std::string::npos && t.find_first_not_of(" ^~") == std::string::npos && j > i + 1) {
                            const std::string& src = lines[j - 1];
                            const std::size_t lead = src.find_first_not_of(' ');
                            if (lead != std::string::npos && caret >= lead) m.column = caret - lead + 1;
                        }
                    }
                    if (!m.message.empty()) return m;
                }
            }
        }
    }
    for (const char* prefix : { "lua: ", "luac: ", "lua5.4: ", "lua5.3: ", "luajit: ", "lua54: ", "luac5.4: ", "luac5.3: " }) {
        if (line.rfind(prefix, 0) != 0) continue;
        const std::string rest = line.substr(std::string(prefix).size());
        std::size_t search = 0;
        while (true) {
            const std::size_t colon = rest.find(':', search);
            if (colon == std::string::npos) break;
            const std::size_t next = rest.find(':', colon + 1);
            if (next != std::string::npos && all_digits(rest.substr(colon + 1, next - colon - 1)) && colon > 0) {
                CompilerMessage m;
                m.file = rest.substr(0, colon);
                m.line = static_cast<std::size_t>(std::stoul(rest.substr(colon + 1, next - colon - 1)));
                m.severity = "error";
                m.message = rest.substr(next + 1);
                if (!m.message.empty() && m.message[0] == ' ') m.message.erase(0, 1);
                return m;
            }
            search = colon + 1;
        }
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
        if (std::optional<CompilerMessage> other = other_message(lines, i)) {
            out.push_back(std::move(*other));
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
    std::vector<RunStats> stats;
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
        m_state->stats.clear();
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
            RunStats stats;
            code = run_command_measured(step, working_dir, text, stats, [&state](const std::string& piece) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->output += piece;
            });
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!text.empty() && text.back() != '\n') state->output += '\n';
                if (stats.measured) state->stats.push_back(std::move(stats));
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

std::vector<RunStats> BackgroundJob::take_stats() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::vector<RunStats> out = std::move(m_state->stats);
    m_state->stats.clear();
    return out;
}

std::optional<int> BackgroundJob::take_result() {
    std::lock_guard<std::mutex> lock(m_state->mutex);
    std::optional<int> r = m_state->result;
    m_state->result.reset();
    return r;
}

} // namespace canopy
