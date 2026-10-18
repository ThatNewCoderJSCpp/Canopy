#ifndef CANOPY_SHELL_HPP
#define CANOPY_SHELL_HPP

#include "workspace.hpp"

#include <grove/project.hpp>

#include <ostream>
#include <string>
#include <vector>

namespace canopy {

std::string find_ui_document();
void populate_shell(grove::Project& project, const Workspace& workspace);
std::string toolchain_summary(const Toolchain& toolchain);

int run_shell(const Workspace& workspace);
int run_shell(const Workspace& workspace, const std::vector<std::string>& files);
int dump_shell(const Workspace& workspace, std::ostream& os);

} // namespace canopy

#endif // CANOPY_SHELL_HPP
