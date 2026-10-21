#ifndef CANOPY_TOOLCHAIN_HPP
#define CANOPY_TOOLCHAIN_HPP

#include "settings.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

enum class ToolOrigin {
    Settings,
    Bundled,
    Path,
    KnownFolder,
    WalnutInit
};

const char* to_string(ToolOrigin origin) noexcept;

enum class CompilerFamily {
    Gcc,
    Clang,
    Msvc,
    Other
};

struct Tool {
    std::string key;
    std::string path;
    ToolOrigin origin = ToolOrigin::Path;
    CompilerFamily family = CompilerFamily::Other;
};

struct ToolProblem {
    std::string key;
    std::string message;
};

struct Toolchain {
    std::optional<Tool> c;
    std::optional<Tool> cpp;
    std::optional<Tool> walnut;
    std::optional<Tool> grove;
    std::optional<Tool> node;
    std::map<std::string, Tool> more;
    std::vector<ToolProblem> problems;

    const Tool* get(const std::string& key) const;
};

std::string executable_dir();
std::string with_exe_suffix(const std::string& name);
bool is_executable_file(const std::string& path);
std::optional<std::string> find_on_path(const std::string& program);
std::vector<std::string> known_tool_folders();
std::vector<std::string> walnut_toolchain_folders();
std::optional<Tool> detect_tool(const std::string& key, const Settings& settings);
CompilerFamily compiler_family(const std::string& path);

std::vector<std::string> tool_candidates(const std::string& key);
std::vector<std::string> tool_folders(const std::string& key);
const std::vector<std::string>& optional_tool_keys();
std::string tool_install_hint(const std::string& key);
Toolchain detect_toolchain(const Settings& settings);

} // namespace canopy

#endif // CANOPY_TOOLCHAIN_HPP
