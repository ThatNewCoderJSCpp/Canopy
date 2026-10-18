#ifndef CANOPY_WORKSPACE_HPP
#define CANOPY_WORKSPACE_HPP

#include "settings.hpp"
#include "toolchain.hpp"

#include <optional>
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

std::string recent_folders_path();
std::vector<std::string> recent_folders();
void remember_folder(const std::string& folder);

std::optional<std::string> read_text_file(const std::string& path);
bool write_text_file(const std::string& path, const std::string& text);

} // namespace canopy

#endif // CANOPY_WORKSPACE_HPP
