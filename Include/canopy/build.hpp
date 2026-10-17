#ifndef CANOPY_BUILD_HPP
#define CANOPY_BUILD_HPP

#include "languages.hpp"
#include "settings.hpp"
#include "toolchain.hpp"

#include <optional>
#include <string>
#include <vector>

namespace canopy {

enum class BuildMode {
    Debug,
    Release
};

struct BuildRequest {
    std::string file;
    std::string output;
    std::string workspace_root;
    std::optional<BuildMode> mode;
};

struct BuildPlan {
    std::vector<std::string> command;
    std::string output;
    std::string error;
    const Language* language = nullptr;

    bool ok() const noexcept { return error.empty() && !command.empty(); }
};

std::vector<std::string> split_flags(const std::string& flags);
std::string quote_argument(const std::string& argument);
std::string command_line(const std::vector<std::string>& command);

BuildMode build_mode(const Settings& settings, const std::optional<BuildMode>& requested);
std::string default_output_path(const BuildRequest& request, const Settings& settings);
BuildPlan plan_build(const BuildRequest& request, const Settings& settings, const Toolchain& toolchain);

int run_command(const std::vector<std::string>& command);

} // namespace canopy

#endif // CANOPY_BUILD_HPP
