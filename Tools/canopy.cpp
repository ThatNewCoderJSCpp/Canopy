#include <canopy/cli.hpp>

int main(int argc, char** argv) {
    return canopy::run_cli(canopy::command_line_arguments(argc, argv));
}
