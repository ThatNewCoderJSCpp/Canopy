#include <canopy/toolchain.hpp>

#include <grove/script/builder.hpp>

#include <cstdlib>
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
    return std::string();
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
    for (const std::string& folder : known_tool_folders()) {
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
    return nullptr;
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
    return {};
}

Toolchain detect_toolchain(const Settings& settings) {
    Toolchain t;
    t.c = detect_one("c", settings, t.problems);
    t.cpp = detect_one("cpp", settings, t.problems);
    t.walnut = detect_one("walnut", settings, t.problems);
    t.grove = detect_one("grove", settings, t.problems);
    return t;
}

} // namespace canopy
