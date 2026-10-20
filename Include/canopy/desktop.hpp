#ifndef CANOPY_DESKTOP_HPP
#define CANOPY_DESKTOP_HPP

#include <string>
#include <vector>

namespace canopy {

enum class InstallScope {
    User,
    System
};

struct InstallOptions {
    InstallScope scope = InstallScope::User;
    std::string prefix;
    bool command = true;
    bool menu = true;
    bool desktop_shortcut = false;
    bool context_menu = true;
    bool file_types = true;
};

struct FileType {
    std::vector<std::string> extensions;
    std::string mime;
    std::string prog_id;
    std::string description;
};

struct InstallStep {
    std::string what;
    std::string path;
    bool ok = true;
    std::string note;
};

struct InstallReport {
    std::vector<InstallStep> steps;
    std::vector<std::string> hints;

    bool ok() const;
};

struct IntegrationStatus {
    std::string command_path;
    bool command_is_this = false;
    std::string menu_entry;
    std::string desktop_shortcut;
};

std::string canopy_executable();
std::string canopy_app_executable();
std::string canopy_icon(const std::string& file_name);
std::string find_canopy_icon(const std::string& file_name);
std::string desktop_entry(const std::string& exec, const std::string& icon);
std::string desktop_folder();
const std::vector<FileType>& canopy_file_types();
std::string mime_icon_name(const FileType& type);
std::string shared_mime_info();

InstallReport install_integration(const InstallOptions& options);
InstallReport uninstall_integration(const InstallOptions& options);
IntegrationStatus integration_status();

bool has_display();
bool launch_detached(const std::string& program, const std::vector<std::string>& args);
bool move_to_trash(const std::string& path);
bool reveal_in_file_manager(const std::string& path);
bool open_in_browser(const std::string& url);
std::string file_uri(const std::string& path);

} // namespace canopy

#endif // CANOPY_DESKTOP_HPP
