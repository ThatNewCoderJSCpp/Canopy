#include <canopy/cli.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    canopy::CliOptions options;
    options.gui = true;
    return canopy::run_cli(canopy::command_line_arguments(0, nullptr), options);
}
#else
int main(int argc, char** argv) {
    canopy::CliOptions options;
    options.gui = true;
    return canopy::run_cli(canopy::command_line_arguments(argc, argv), options);
}
#endif
