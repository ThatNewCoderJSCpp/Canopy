#include <canopy/workspace.hpp>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace canopy {

namespace {

bool hidden_entry(const std::string& name) {
    return name == ".git" || name == ".cache" || name == ".grove-cache";
}

} // namespace

std::vector<WorkspaceEntry> Workspace::list(const std::string& folder) const {
    std::vector<WorkspaceEntry> out;
    std::error_code ec;
    for (fs::directory_iterator it(folder.empty() ? root : folder, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (hidden_entry(name)) continue;
        WorkspaceEntry e;
        e.name = name;
        e.path = it->path().string();
        e.directory = it->is_directory(ec);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const WorkspaceEntry& a, const WorkspaceEntry& b) {
        if (a.directory != b.directory) return a.directory;
        return a.name < b.name;
    });
    return out;
}

std::string find_workspace_root(const std::string& start) {
    std::error_code ec;
    fs::path dir = fs::absolute(start.empty() ? fs::current_path(ec) : fs::path(start), ec);
    if (!fs::is_regular_file(dir, ec)) return dir.string();
    dir = dir.parent_path();
    for (fs::path p = dir; !p.empty(); p = p.parent_path()) {
        if (fs::is_directory(p / ".canopy", ec) || fs::exists(p / ".git", ec)) return p.string();
        if (p == p.parent_path()) break;
    }
    return dir.string();
}

Workspace open_workspace(const std::string& folder) {
    Workspace w;
    w.root = find_workspace_root(folder);
    w.settings = load_settings(w.root, w.diagnostics);
    w.toolchain = detect_toolchain(w.settings);
    return w;
}

} // namespace canopy
