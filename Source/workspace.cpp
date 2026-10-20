#include <canopy/workspace.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

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

std::string clean_folder_path(const std::string& folder) {
    if (folder.empty()) return folder;
    std::error_code ec;
    fs::path p = fs::absolute(fs::path(folder), ec);
    if (ec) p = fs::path(folder);
    p = p.lexically_normal();
    while (p.has_relative_path() && p.filename().empty()) p = p.parent_path();
    return p.string();
}

std::string find_workspace_root(const std::string& start) {
    std::error_code ec;
    fs::path dir = fs::absolute(start.empty() ? fs::current_path(ec) : fs::path(start), ec);
    if (!fs::is_regular_file(dir, ec)) return clean_folder_path(dir.string());
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

std::string recent_folders_path() {
    return (fs::path(user_config_dir()) / "recent.txt").string();
}

std::vector<std::string> recent_folders() {
    std::vector<std::string> out;
    std::ifstream in(recent_folders_path());
    std::string line;
    std::error_code ec;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        line = clean_folder_path(line);
        if (std::find(out.begin(), out.end(), line) != out.end()) continue;
        if (fs::is_directory(line, ec)) out.push_back(line);
    }
    return out;
}

void remember_folder(const std::string& raw) {
    if (raw.empty()) return;
    const std::string folder = clean_folder_path(raw);
    std::vector<std::string> list = recent_folders();
    list.erase(std::remove(list.begin(), list.end(), folder), list.end());
    list.insert(list.begin(), folder);
    if (list.size() > 10) list.resize(10);
    std::error_code ec;
    fs::create_directories(user_config_dir(), ec);
    std::ofstream out(recent_folders_path(), std::ios::trunc);
    for (const std::string& f : list) out << f << '\n';
}

std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

std::string create_workspace_entry(const std::string& folder, const std::string& name, bool directory) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return folder + " isn't a folder";
    const fs::path dest = fs::path(folder) / name;
    std::error_code exists_ec;
    if (fs::exists(fs::symlink_status(dest, exists_ec))) return name + " is already there";
    if (directory) {
        if (!fs::create_directories(dest, ec) && ec) return ec.message();
        return std::string();
    }
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        fs::create_directories(dest.parent_path(), ec);
        if (ec) return ec.message();
    }
    std::ofstream out(dest, std::ios::out | std::ios::binary);
    if (!out) return "the folder can't be written to";
    out.close();
    if (!fs::is_regular_file(dest, ec)) return "it wasn't created";
    return std::string();
}

} // namespace canopy
