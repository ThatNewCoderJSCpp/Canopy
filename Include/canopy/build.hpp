#ifndef CANOPY_BUILD_HPP
#define CANOPY_BUILD_HPP

#include "languages.hpp"
#include "settings.hpp"
#include "toolchain.hpp"

#include <memory>
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
int run_command_capture(const std::vector<std::string>& command, std::string& output);
int run_command_capture_in(const std::vector<std::string>& command, const std::string& working_dir, std::string& output);
int run_command_capture_input(const std::vector<std::string>& command, const std::string& input, std::string& output);

struct CompilerMessage {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string severity;
    std::string message;
};

std::vector<CompilerMessage> parse_compiler_messages(const std::string& output);

class BackgroundJob {
public:
    BackgroundJob();
    ~BackgroundJob();
    BackgroundJob(const BackgroundJob&) = delete;
    BackgroundJob& operator=(const BackgroundJob&) = delete;

    bool start(std::vector<std::vector<std::string>> steps);
    bool start(std::vector<std::vector<std::string>> steps, const std::string& working_dir);
    bool running() const;
    std::string take_output();
    std::optional<int> take_result();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace canopy

#endif // CANOPY_BUILD_HPP
