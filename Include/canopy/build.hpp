#ifndef CANOPY_BUILD_HPP
#define CANOPY_BUILD_HPP

#include "languages.hpp"
#include "settings.hpp"
#include "toolchain.hpp"

#include <cstdint>
#include <functional>
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
    std::vector<std::vector<std::string>> more_steps;
    std::vector<std::string> run;
    std::string run_error;
    std::string folder;
    std::string note;

    bool ok() const noexcept { return error.empty() && !command.empty(); }
};

std::vector<std::string> split_flags(const std::string& flags);
std::string quote_argument(const std::string& argument);
std::string command_line(const std::vector<std::string>& command);

BuildMode build_mode(const Settings& settings, const std::optional<BuildMode>& requested);
std::string default_output_path(const BuildRequest& request, const Settings& settings);
std::vector<std::string> walnut_safety_flags(const Settings& settings);
BuildPlan plan_build(const BuildRequest& request, const Settings& settings, const Toolchain& toolchain);

int run_command(const std::vector<std::string>& command);
int run_command_capture(const std::vector<std::string>& command, std::string& output);
int run_command_capture_in(const std::vector<std::string>& command, const std::string& working_dir, std::string& output);
int run_command_capture_input(const std::vector<std::string>& command, const std::string& input, std::string& output);
int run_command_capture_input(const std::vector<std::string>& command, const std::string& input, std::string& output, bool with_errors);

struct CompilerMessage {
    std::string file;
    std::size_t line = 0;
    std::size_t column = 0;
    std::string severity;
    std::string message;
};

std::vector<CompilerMessage> parse_compiler_messages(const std::string& output);

struct RunStats {
    bool          measured = false;
    std::string   command;
    int           exit_code = 0;
    std::int64_t  started_ms = 0;
    std::int64_t  wall_us = 0;
    std::int64_t  user_us = 0;
    std::int64_t  system_us = 0;
    std::uint64_t peak_bytes = 0;
    bool          peak_is_commit = false;
    std::uint64_t minor_faults = 0;
    std::uint64_t major_faults = 0;
    std::uint64_t voluntary_switches = 0;
    std::uint64_t involuntary_switches = 0;
    std::uint64_t read_bytes = 0;
    std::uint64_t written_bytes = 0;
};

std::string format_duration_us(std::int64_t microseconds);
std::string format_bytes(std::uint64_t bytes);
std::string run_stats_summary(const RunStats& stats);
int run_command_measured(const std::vector<std::string>& command, const std::string& working_dir, std::string& output, RunStats& stats, const std::function<void(const std::string&)>& on_output);

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
    std::vector<RunStats> take_stats();

private:
    struct State;
    std::shared_ptr<State> m_state;
};

} // namespace canopy

#endif // CANOPY_BUILD_HPP
