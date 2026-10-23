#ifndef CANOPY_SHELL_HPP
#define CANOPY_SHELL_HPP

#include "workspace.hpp"

#include <grove/project.hpp>

#include <ostream>
#include <string>
#include <vector>

#if defined(__ANDROID__)
namespace fizmo {
namespace android {
class App;
} // namespace android
} // namespace fizmo
#endif

namespace canopy {

std::string find_ui_document();
void populate_shell(grove::Project& project, const Workspace& workspace);
std::string toolchain_summary(const Toolchain& toolchain);

int run_shell(const Workspace& workspace);
int run_shell(const Workspace& workspace, const std::vector<std::string>& files);
int dump_shell(const Workspace& workspace, std::ostream& os);

void hide_desktop_only_parts(grove::Document& doc);

#if defined(__ANDROID__)
int run_shell_android(fizmo::android::App& app, grove::Project& project, const Workspace& workspace);
#endif

} // namespace canopy

#endif // CANOPY_SHELL_HPP
