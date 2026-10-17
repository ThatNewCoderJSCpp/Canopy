#ifndef CANOPY_WORKSPACE_HPP
#define CANOPY_WORKSPACE_HPP

#include "settings.hpp"
#include "toolchain.hpp"

#include <string>
#include <vector>

namespace canopy {

struct WorkspaceEntry {
    std::string name;
    std::string path;
    bool directory = false;
};

struct Workspace {
    std::string root;
    Settings settings;
    Toolchain toolchain;
    grove::Diagnostics diagnostics;

    std::vector<WorkspaceEntry> list(const std::string& folder) const;
};

std::string find_workspace_root(const std::string& start);
Workspace open_workspace(const std::string& folder);

} // namespace canopy

#endif // CANOPY_WORKSPACE_HPP
