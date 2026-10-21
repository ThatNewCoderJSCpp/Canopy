#include <canopy/toolchain.hpp>

#include <grove/script/builder.hpp>

#include <cstdlib>
#include <algorithm>
#include <filesystem>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string install_hint(const std::string& key) {
    if (key == "c" || key == "cpp") {
#if defined(_WIN32)
        return "install MSYS2 (https://www.msys2.org), then run: pacman -S mingw-w64-ucrt-x86_64-gcc";
#elif defined(__APPLE__)
        return "run: xcode-select --install";
#else
        return "install a compiler, e.g. Fedora: sudo dnf install gcc gcc-c++   Ubuntu/Debian: sudo apt install build-essential";
#endif
    }
    if (key == "walnut") return "Canopy normally ships walnut next to itself; reinstall Canopy or set toolchain { walnut: \"/path/to/walnut\"; }";
    if (key == "grove") return "Canopy normally ships grove next to itself; reinstall Canopy or set toolchain { grove: \"/path/to/grove\"; }";
    if (key == "node") return "install Node.js from https://nodejs.org (or your package manager), or set toolchain { node: \"/path/to/node\"; }";
    if (key == "zig") return "install Zig from https://ziglang.org/download (or your package manager), or set toolchain { zig: \"/path/to/zig\"; }";
    if (key == "rustc" || key == "cargo") return "install Rust with rustup from https://rustup.rs, or set toolchain { " + key + ": \"/path/to/" + key + "\"; }";
    if (key == "python") return "install Python from https://www.python.org/downloads (or your package manager), or set toolchain { python: \"/path/to/python\"; }";
    if (key == "lua" || key == "luac") return "install Lua from https://www.lua.org/download.html (Ubuntu/Debian: sudo apt install lua5.4, Fedora: sudo dnf install lua, macOS: brew install lua, Windows: winget install DEVCOM.Lua or scoop install lua)";
    if (key == "java" || key == "javac") return "install a JDK, e.g. https://adoptium.net (Ubuntu/Debian: sudo apt install default-jdk, Fedora: sudo dnf install java-latest-openjdk-devel), or set toolchain { " + key + ": \"/path/to/" + key + "\"; }";
    if (key == "dotnet") return "install the .NET SDK from https://dotnet.microsoft.com/download, or set toolchain { dotnet: \"/path/to/dotnet\"; }";
    if (key == "mcs" || key == "mono") return "install Mono from https://www.mono-project.com (Ubuntu/Debian: sudo apt install mono-mcs), or use the .NET SDK instead";
    if (key == "nasm") return "install NASM from https://www.nasm.us (Ubuntu/Debian: sudo apt install nasm, Fedora: sudo dnf install nasm, macOS: brew install nasm, Windows: winget install NASM.NASM)";
    if (key == "yasm") return "install YASM (Ubuntu/Debian: sudo apt install yasm, macOS: brew install yasm)";
    if (key == "fasm") return "install flat assembler from https://flatassembler.net";
    if (key == "masm") return "MASM (ml64) comes with Visual Studio's C++ tools; open the x64 Native Tools prompt, or set toolchain { masm: \"/path/to/ml64.exe\"; }";
    if (key == "gcc-aarch64") return "install an ARM64 cross compiler (Ubuntu/Debian: sudo apt install gcc-aarch64-linux-gnu)";
    if (key == "gcc-arm") return "install an ARM cross compiler (Ubuntu/Debian: sudo apt install gcc-arm-linux-gnueabihf)";
    if (key == "gcc-riscv64") return "install a RISC-V cross compiler (Ubuntu/Debian: sudo apt install gcc-riscv64-linux-gnu)";
    if (key == "gcc-mips") return "install a MIPS cross compiler (Ubuntu/Debian: sudo apt install gcc-mips-linux-gnu)";
    if (key.rfind("qemu-", 0) == 0) return "install QEMU's user-mode emulators to run other CPUs' programs (Ubuntu/Debian: sudo apt install qemu-user, Fedora: sudo dnf install qemu-user)";
    return std::string();
}

void add_glob_dirs(std::vector<std::string>& out, const fs::path& parent, const std::string& prefix, const std::string& suffix) {
    std::error_code ec;
    std::vector<fs::path> found;
    for (fs::directory_iterator it(parent, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(prefix, 0) == 0) found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    for (auto it = found.rbegin(); it != found.rend(); ++it) out.push_back(suffix.empty() ? it->string() : (*it / suffix).string());
}

std::vector<std::string> node_folders() {
    std::vector<std::string> out;
#if defined(_WIN32)
    out.push_back("C:/Program Files/nodejs");
    const std::string app = env("APPDATA");
    if (!app.empty()) out.push_back((fs::path(app) / "nvm").string());
    const std::string local = env("LOCALAPPDATA");
    if (!local.empty()) out.push_back((fs::path(local) / "Volta" / "bin").string());
#else
    const std::string home = env("HOME");
    if (!home.empty()) {
        out.push_back((fs::path(home) / ".volta" / "bin").string());
        std::error_code ec;
        std::vector<fs::path> versions;
        for (fs::directory_iterator it(fs::path(home) / ".nvm" / "versions" / "node", ec), end; !ec && it != end; it.increment(ec)) versions.push_back(it->path());
        std::sort(versions.begin(), versions.end());
        for (auto it = versions.rbegin(); it != versions.rend(); ++it) out.push_back((*it / "bin").string());
    }
    out.push_back("/snap/bin");
    out.push_back("/opt/homebrew/bin");
#endif
    return out;
}

bool bundled_key(const std::string& key) {
    return key == "walnut" || key == "grove";
}

std::optional<std::string> find_in(const fs::path& folder, const std::string& program) {
    std::error_code ec;
    const fs::path candidate = folder / with_exe_suffix(program);
    if (is_executable_file(candidate.string())) return fs::absolute(candidate, ec).string();
    return std::nullopt;
}

std::optional<Tool> make_tool(const std::string& key, const std::string& path, ToolOrigin origin) {
    Tool t;
    t.key = key;
    t.path = path;
    t.origin = origin;
    t.family = (key == "c" || key == "cpp") ? compiler_family(path) : CompilerFamily::Other;
    return t;
}

std::optional<Tool> detect_one(const std::string& key, const Settings& settings, std::vector<ToolProblem>& problems) {
    const std::string chosen = settings.get_or("toolchain", key, "auto");
    if (!chosen.empty() && chosen != "auto") {
        if (is_executable_file(chosen)) return make_tool(key, chosen, ToolOrigin::Settings);
        if (fs::path(chosen).filename() == fs::path(chosen)) {
            if (const auto found = find_on_path(chosen)) return make_tool(key, *found, ToolOrigin::Settings);
        }
        const Setting* s = settings.find("toolchain", key);
        std::string where = s ? " (" + s->file + ":" + std::to_string(s->line) + ")" : std::string();
        problems.push_back({ key, "toolchain " + key + " is set to '" + chosen + "'" + where + " but that isn't a program Canopy can run; falling back to auto" });
    }

    const std::vector<std::string> names = tool_candidates(key);
    if (bundled_key(key)) {
        const fs::path dir = executable_dir();
        for (const std::string& n : names) {
            if (const auto found = find_in(dir, n)) return make_tool(key, *found, ToolOrigin::Bundled);
        }
    }
    for (const std::string& n : names) {
        if (const auto found = find_on_path(n)) return make_tool(key, *found, ToolOrigin::Path);
    }
    std::vector<std::string> folders = known_tool_folders();
    if (key == "node") {
        for (const std::string& extra : node_folders()) folders.push_back(extra);
    }
    for (const std::string& extra : tool_folders(key)) folders.push_back(extra);
    for (const std::string& folder : folders) {
        for (const std::string& n : names) {
            if (const auto found = find_in(folder, n)) return make_tool(key, *found, ToolOrigin::KnownFolder);
        }
    }
    problems.push_back({ key, "no " + key + " tool found; " + install_hint(key) });
    return std::nullopt;
}

} // namespace

const char* to_string(ToolOrigin origin) noexcept {
    switch (origin) {
    case ToolOrigin::Settings: return "settings";
    case ToolOrigin::Bundled: return "bundled";
    case ToolOrigin::Path: return "PATH";
    case ToolOrigin::KnownFolder: return "install folder";
    }
    return "?";
}

const Tool* Toolchain::get(const std::string& key) const {
    if (key == "c") return c ? &*c : nullptr;
    if (key == "cpp") return cpp ? &*cpp : nullptr;
    if (key == "walnut") return walnut ? &*walnut : nullptr;
    if (key == "grove") return grove ? &*grove : nullptr;
    if (key == "node") return node ? &*node : nullptr;
    const auto it = more.find(key);
    return it == more.end() ? nullptr : &it->second;
}

std::string executable_dir() {
    const std::string exe = grove::executable_path();
    if (exe.empty()) return fs::current_path().string();
    return fs::path(exe).parent_path().string();
}

std::string with_exe_suffix(const std::string& name) {
#if defined(_WIN32)
    if (fs::path(name).extension().empty()) return name + ".exe";
#endif
    return name;
}

bool is_executable_file(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) return false;
#if defined(_WIN32)
    return true;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

std::optional<std::string> find_on_path(const std::string& program) {
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    const std::string path = env("PATH");
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(separator, start);
        const std::string folder = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!folder.empty()) {
            if (const auto found = find_in(folder, program)) return found;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return std::nullopt;
}

std::vector<std::string> known_tool_folders() {
#if defined(_WIN32)
    return {
        "C:/msys64/ucrt64/bin",
        "C:/msys64/mingw64/bin",
        "C:/msys64/clang64/bin",
        "C:/msys64/clangarm64/bin",
        "C:/Program Files/LLVM/bin",
    };
#elif defined(__APPLE__)
    return { "/opt/homebrew/bin", "/usr/local/bin", "/opt/homebrew/opt/llvm/bin", "/usr/bin" };
#else
    std::vector<std::string> folders = { "/usr/local/bin", "/usr/bin" };
    const std::string home = env("HOME");
    if (!home.empty()) folders.push_back((fs::path(home) / ".local" / "bin").string());
    return folders;
#endif
}

CompilerFamily compiler_family(const std::string& path) {
    std::string name = fs::path(path).stem().string();
    if (name.find("clang") != std::string::npos) return CompilerFamily::Clang;
    if (name == "cl" || name == "CL") return CompilerFamily::Msvc;
    if (name.find("gcc") != std::string::npos || name.find("g++") != std::string::npos || name == "cc" || name == "c++") return CompilerFamily::Gcc;
    return CompilerFamily::Other;
}

std::vector<std::string> tool_candidates(const std::string& key) {
    if (key == "c") return { "gcc", "clang", "cc", "cl" };
    if (key == "cpp") return { "g++", "clang++", "c++", "cl" };
    if (key == "walnut") return { "walnut" };
    if (key == "grove") return { "grove" };
    if (key == "node") return { "node" };
    if (key == "zig") return { "zig" };
    if (key == "rustc") return { "rustc" };
    if (key == "cargo") return { "cargo" };
#if defined(_WIN32)
    if (key == "python") return { "py", "python", "python3" };
#else
    if (key == "python") return { "python3", "python" };
#endif
    if (key == "lua") return { "lua", "lua5.4", "lua54", "lua5.3", "lua53", "luajit" };
    if (key == "luac") return { "luac", "luac5.4", "luac54", "luac5.3", "luac53" };
    if (key == "java") return { "java" };
    if (key == "javac") return { "javac" };
    if (key == "dotnet") return { "dotnet" };
    if (key == "mcs") return { "mcs", "csc" };
    if (key == "mono") return { "mono" };
    if (key == "nasm") return { "nasm" };
    if (key == "yasm") return { "yasm" };
    if (key == "fasm") return { "fasm" };
    if (key == "masm") return { "ml64", "ml" };
    if (key == "ld") return { "ld", "ld.lld" };
    if (key == "gcc-aarch64") return { "aarch64-linux-gnu-gcc", "aarch64-none-linux-gnu-gcc", "aarch64-elf-gcc" };
    if (key == "gcc-arm") return { "arm-linux-gnueabihf-gcc", "arm-linux-gnueabi-gcc", "arm-none-eabi-gcc" };
    if (key == "gcc-riscv64") return { "riscv64-linux-gnu-gcc", "riscv64-unknown-linux-gnu-gcc", "riscv64-unknown-elf-gcc" };
    if (key == "gcc-mips") return { "mips-linux-gnu-gcc", "mipsel-linux-gnu-gcc" };
    if (key == "gcc-x86_64") return { "x86_64-linux-gnu-gcc", "x86_64-w64-mingw32-gcc" };
    if (key == "qemu-aarch64") return { "qemu-aarch64", "qemu-aarch64-static" };
    if (key == "qemu-arm") return { "qemu-arm", "qemu-arm-static" };
    if (key == "qemu-riscv64") return { "qemu-riscv64", "qemu-riscv64-static" };
    if (key == "qemu-mips") return { "qemu-mips", "qemu-mipsel", "qemu-mips-static" };
    if (key == "qemu-x86_64") return { "qemu-x86_64", "qemu-x86_64-static" };
    return {};
}

std::vector<std::string> tool_folders(const std::string& key) {
    std::vector<std::string> out;
    const std::string home = env("HOME").empty() ? env("USERPROFILE") : env("HOME");
#if defined(_WIN32)
    const std::string local = env("LOCALAPPDATA");
    if (!home.empty()) out.push_back((fs::path(home) / "scoop" / "shims").string());
    out.push_back("C:/ProgramData/chocolatey/bin");
    if (!local.empty()) out.push_back((fs::path(local) / "Microsoft" / "WinGet" / "Links").string());
#endif
    if (key == "rustc" || key == "cargo") {
        const std::string cargo_home = env("CARGO_HOME");
        if (!cargo_home.empty()) out.push_back((fs::path(cargo_home) / "bin").string());
        if (!home.empty()) out.push_back((fs::path(home) / ".cargo" / "bin").string());
    } else if (key == "zig") {
        if (!home.empty()) {
            out.push_back((fs::path(home) / ".zig").string());
            out.push_back((fs::path(home) / "zig").string());
            add_glob_dirs(out, fs::path(home) / ".local" / "share", "zig", "");
        }
#if defined(_WIN32)
        out.push_back("C:/zig");
        add_glob_dirs(out, "C:/", "zig", "");
#else
        out.push_back("/opt/zig");
        out.push_back("/usr/local/zig");
        add_glob_dirs(out, "/opt", "zig", "");
#endif
    } else if (key == "python") {
#if defined(_WIN32)
        if (!local.empty()) add_glob_dirs(out, fs::path(local) / "Programs" / "Python", "Python3", "");
        add_glob_dirs(out, "C:/", "Python3", "");
#elif defined(__APPLE__)
        out.push_back("/Library/Frameworks/Python.framework/Versions/Current/bin");
#endif
    } else if (key == "java" || key == "javac") {
        const std::string java_home = env("JAVA_HOME");
        if (!java_home.empty()) out.push_back((fs::path(java_home) / "bin").string());
#if defined(_WIN32)
        for (const char* vendor : { "C:/Program Files/Java", "C:/Program Files/Eclipse Adoptium", "C:/Program Files/Microsoft", "C:/Program Files/Zulu", "C:/Program Files/Amazon Corretto" }) add_glob_dirs(out, vendor, "", "bin");
#elif defined(__APPLE__)
        add_glob_dirs(out, "/Library/Java/JavaVirtualMachines", "", "Contents/Home/bin");
        out.push_back("/opt/homebrew/opt/openjdk/bin");
#else
        add_glob_dirs(out, "/usr/lib/jvm", "", "bin");
#endif
    } else if (key == "dotnet") {
        if (!home.empty()) out.push_back((fs::path(home) / ".dotnet").string());
#if defined(_WIN32)
        out.push_back("C:/Program Files/dotnet");
#else
        out.push_back("/usr/share/dotnet");
        out.push_back("/usr/local/share/dotnet");
        out.push_back("/usr/lib/dotnet");
#endif
    } else if (key == "mcs" || key == "mono") {
#if defined(_WIN32)
        out.push_back("C:/Program Files/Mono/bin");
        out.push_back("C:/Windows/Microsoft.NET/Framework64/v4.0.30319");
#elif defined(__APPLE__)
        out.push_back("/Library/Frameworks/Mono.framework/Versions/Current/Commands");
#endif
    } else if (key == "nasm") {
#if defined(_WIN32)
        out.push_back("C:/Program Files/NASM");
        if (!local.empty()) out.push_back((fs::path(local) / "bin" / "NASM").string());
#endif
    } else if (key == "lua" || key == "luac") {
#if defined(_WIN32)
        add_glob_dirs(out, "C:/Program Files/Lua", "", "");
        out.push_back("C:/Program Files (x86)/Lua/5.1");
#endif
    } else if (key == "masm") {
#if defined(_WIN32)
        for (const char* vs : { "C:/Program Files/Microsoft Visual Studio/2022", "C:/Program Files (x86)/Microsoft Visual Studio/2019" }) {
            std::vector<std::string> editions;
            add_glob_dirs(editions, vs, "", "VC/Tools/MSVC");
            for (const std::string& e : editions) add_glob_dirs(out, e, "", "bin/Hostx64/x64");
        }
#endif
    }
    return out;
}

const std::vector<std::string>& optional_tool_keys() {
    static const std::vector<std::string> keys = { "zig", "rustc", "cargo", "python", "lua", "luac", "java", "javac", "dotnet", "mcs", "mono", "nasm", "yasm",
                                                   "fasm", "masm", "ld", "gcc-aarch64", "gcc-arm", "gcc-riscv64", "gcc-mips", "gcc-x86_64", "qemu-aarch64",
                                                   "qemu-arm", "qemu-riscv64", "qemu-mips", "qemu-x86_64" };
    return keys;
}

std::string tool_install_hint(const std::string& key) {
    return install_hint(key);
}

Toolchain detect_toolchain(const Settings& settings) {
    Toolchain t;
    t.c = detect_one("c", settings, t.problems);
    t.cpp = detect_one("cpp", settings, t.problems);
    t.walnut = detect_one("walnut", settings, t.problems);
    t.grove = detect_one("grove", settings, t.problems);
    std::vector<ToolProblem> quiet;
    t.node = detect_one("node", settings, quiet);
    for (const ToolProblem& p : quiet) {
        if (p.message.rfind("toolchain node is set", 0) == 0) t.problems.push_back(p);
    }
    for (const std::string& key : optional_tool_keys()) {
        std::vector<ToolProblem> skipped;
        if (std::optional<Tool> found = detect_one(key, settings, skipped)) t.more[key] = *found;
        for (const ToolProblem& p : skipped) {
            if (p.message.rfind("toolchain " + key + " is set", 0) == 0) t.problems.push_back(p);
        }
    }
    return t;
}

} // namespace canopy
