#ifndef CANOPY_CLI_HPP
#define CANOPY_CLI_HPP

#include <string>
#include <vector>

namespace canopy {

struct CliOptions {
    bool gui = false;
};

int run_cli(std::vector<std::string> args, const CliOptions& options = CliOptions());
std::vector<std::string> command_line_arguments(int argc, char** argv);

} // namespace canopy

#endif // CANOPY_CLI_HPP
