#include <canopy/desktop.hpp>

#include <canopy/build.hpp>
#include <canopy/toolchain.hpp>

#include <grove/script/builder.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

void step(InstallReport& report, std::string what, const std::string& path, bool ok, std::string note = {}) {
    report.steps.push_back(InstallStep{ std::move(what), path, ok, std::move(note) });
}

bool write_file(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << text;
    return static_cast<bool>(out);
}

bool copy_into(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool remove_path(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec))) return true;
    fs::remove(path, ec);
    return !ec;
}

bool on_path_dir(const fs::path& dir) {
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    const std::string path = env("PATH");
    std::error_code ec;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find(separator, start);
        const std::string entry = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!entry.empty() && fs::equivalent(fs::path(entry), dir, ec)) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

#if !defined(_WIN32)

std::string data_home(const InstallOptions& o) {
    if (o.scope == InstallScope::System) return (fs::path(o.prefix.empty() ? "/usr/local" : o.prefix) / "share").string();
    const std::string xdg = env("XDG_DATA_HOME");
    if (!xdg.empty()) return xdg;
    return (fs::path(env("HOME")) / ".local" / "share").string();
}

std::string bin_home(const InstallOptions& o) {
    if (o.scope == InstallScope::System) return (fs::path(o.prefix.empty() ? "/usr/local" : o.prefix) / "bin").string();
    return (fs::path(env("HOME")) / ".local" / "bin").string();
}

void run_quietly(const std::string& program, const std::vector<std::string>& args) {
    const std::optional<std::string> found = find_on_path(program);
    if (!found) return;
    const pid_t pid = fork();
    if (pid == 0) {
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 1);
            dup2(null, 2);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(found->c_str()));
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(found->c_str(), argv.data());
        _exit(127);
    }
    if (pid > 0) {
        int status = 0;
        waitpid(pid, &status, 0);
    }
}

#endif

#if defined(_WIN32)

std::wstring wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) out = narrow(raw);
    if (raw) CoTaskMemFree(raw);
    return out;
}

bool set_reg_string(HKEY root, const std::wstring& key, const std::wstring& name, const std::wstring& value, DWORD type = REG_SZ) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const LONG r = RegSetValueExW(h, name.empty() ? nullptr : name.c_str(), 0, type, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool get_reg_string(HKEY root, const std::wstring& key, const std::wstring& name, std::wstring& value, DWORD& type) {
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return false;
    DWORD size = 0;
    LONG r = RegQueryValueExW(h, name.c_str(), nullptr, &type, nullptr, &size);
    if (r == ERROR_SUCCESS && size > 0) {
        std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
        r = RegQueryValueExW(h, name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size);
        if (r == ERROR_SUCCESS) {
            buffer.resize(size / sizeof(wchar_t));
            while (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
            value = buffer;
        }
    }
    RegCloseKey(h);
    return r == ERROR_SUCCESS;
}

bool delete_reg_tree(HKEY root, const std::wstring& key) {
    const LONG r = RegDeleteTreeW(root, key.c_str());
    if (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) {
        RegDeleteKeyW(root, key.c_str());
        return true;
    }
    return false;
}

void announce_environment() {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 3000, &result);
}

std::vector<std::wstring> split_path(const std::wstring& value) {
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t end = value.find(L';', start);
        std::wstring part = value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
        if (!part.empty()) out.push_back(part);
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    return out;
}

bool same_dir(const std::wstring& a, const std::wstring& b) {
    std::wstring x = a;
    std::wstring y = b;
    while (!x.empty() && (x.back() == L'\\' || x.back() == L'/')) x.pop_back();
    while (!y.empty() && (y.back() == L'\\' || y.back() == L'/')) y.pop_back();
    return CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool edit_user_path(const std::wstring& dir, bool add) {
    std::wstring value;
    DWORD type = REG_EXPAND_SZ;
    get_reg_string(HKEY_CURRENT_USER, L"Environment", L"Path", value, type);
    std::vector<std::wstring> parts = split_path(value);
    bool present = false;
    std::vector<std::wstring> kept;
    for (const std::wstring& p : parts) {
        if (same_dir(p, dir)) present = true;
        else kept.push_back(p);
    }
    if (add && present) return true;
    if (!add && !present) return true;
    if (add) kept.push_back(dir);
    std::wstring joined;
    for (const std::wstring& p : kept) {
        if (!joined.empty()) joined += L';';
        joined += p;
    }
    const bool ok = set_reg_string(HKEY_CURRENT_USER, L"Environment", L"Path", joined, type == REG_SZ ? REG_SZ : REG_EXPAND_SZ);
    if (ok) announce_environment();
    return ok;
}

bool make_shortcut(const std::string& link, const std::string& target, const std::string& args, const std::string& description) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = false;
    IShellLinkW* shell = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&shell)))) {
        shell->SetPath(wide(target).c_str());
        shell->SetArguments(wide(args).c_str());
        shell->SetDescription(wide(description).c_str());
        shell->SetIconLocation(wide(target).c_str(), 0);
        shell->SetWorkingDirectory(wide(env("USERPROFILE")).c_str());
        IPersistFile* file = nullptr;
        if (SUCCEEDED(shell->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
            std::error_code ec;
            fs::create_directories(fs::path(link).parent_path(), ec);
            ok = SUCCEEDED(file->Save(wide(link).c_str(), TRUE));
            file->Release();
        }
        shell->Release();
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return ok;
}

const wchar_t* const k_context_keys[] = {
    L"Software\\Classes\\Directory\\shell\\Canopy",
    L"Software\\Classes\\Directory\\Background\\shell\\Canopy",
    L"Software\\Classes\\*\\shell\\Canopy",
};

const wchar_t* const k_app_paths = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\canopy.exe";

#endif

} // namespace

bool InstallReport::ok() const {
    for (const InstallStep& s : steps) {
        if (!s.ok) return false;
    }
    return true;
}

std::string canopy_executable() {
    const fs::path dir = executable_dir();
    const fs::path cli = dir / with_exe_suffix("canopy");
    std::error_code ec;
    if (fs::is_regular_file(cli, ec)) return cli.string();
    return grove::executable_path();
}

std::string canopy_app_executable() {
#if defined(_WIN32)
    const fs::path gui = fs::path(executable_dir()) / "canopy-gui.exe";
    std::error_code ec;
    if (fs::is_regular_file(gui, ec)) return gui.string();
#endif
    return canopy_executable();
}

std::string canopy_icon(const std::string& file_name) {
    static std::mutex lock;
    static std::unordered_map<std::string, std::string> found;
    {
        std::lock_guard<std::mutex> guard(lock);
        const auto it = found.find(file_name);
        if (it != found.end()) return it->second;
    }
    const std::string path = find_canopy_icon(file_name);
    std::lock_guard<std::mutex> guard(lock);
    found.emplace(file_name, path);
    return path;
}

std::string find_canopy_icon(const std::string& file_name) {
    std::error_code ec;
    const fs::path dir = executable_dir();
    for (const fs::path& candidate : { dir.parent_path() / "share" / "canopy" / "icons" / file_name, dir / "icons" / file_name }) {
        if (fs::is_regular_file(candidate, ec)) return candidate.string();
    }
#ifdef CANOPY_SOURCE_PACKAGING_DIR
    const fs::path source = fs::path(CANOPY_SOURCE_PACKAGING_DIR) / "icons" / file_name;
    if (fs::is_regular_file(source, ec)) return source.string();
#endif
    return std::string();
}

const std::vector<FileType>& canopy_file_types() {
    static const std::vector<FileType> types = {
        { { ".wal" }, "text/x-walnut", "Canopy.Walnut", "Walnut source file" },
        { { ".walh", ".hwal" }, "text/x-walnut-header", "Canopy.WalnutHeader", "Walnut header" },
        { { ".grove" }, "text/x-grove", "Canopy.Grove", "Grove document" },
        { { ".bark" }, "text/x-bark", "Canopy.Bark", "Bark stylesheet" },
    };
    return types;
}

std::string mime_icon_name(const FileType& type) {
    std::string name = type.mime;
    for (char& c : name) {
        if (c == '/') c = '-';
    }
    return name;
}

std::string shared_mime_info() {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n";
    for (const FileType& t : canopy_file_types()) {
        out += "  <mime-type type=\"" + t.mime + "\">\n";
        out += "    <comment>" + t.description + "</comment>\n";
        out += "    <sub-class-of type=\"text/plain\"/>\n";
        out += "    <icon name=\"" + mime_icon_name(t) + "\"/>\n";
        for (const std::string& e : t.extensions) out += "    <glob pattern=\"*" + e + "\"/>\n";
        out += "  </mime-type>\n";
    }
    return out + "</mime-info>\n";
}

std::string desktop_entry(const std::string& exec, const std::string& icon) {
    const std::string quoted = exec.find(' ') == std::string::npos ? exec : "\"" + exec + "\"";
    return "[Desktop Entry]\n"
           "Type=Application\n"
           "Name=Canopy\n"
           "GenericName=Code Editor\n"
           "Comment=Edit, build and run C, C++, Walnut and Grove\n"
           "Exec=" + quoted + " --wait %F\n"
           "Icon=" + icon + "\n"
           "Terminal=false\n"
           "StartupNotify=true\n"
           "Categories=Development;IDE;TextEditor;\n"
           "Keywords=code;editor;ide;c;c++;walnut;grove;bark;\n"
           "StartupWMClass=canopy\n"
           "MimeType=text/plain;text/x-csrc;text/x-chdr;text/x-c++src;text/x-c++hdr;inode/directory;text/x-walnut;text/x-walnut-header;text/x-grove;text/x-bark;\n";
}

std::string desktop_folder() {
#if defined(_WIN32)
    return known_folder(FOLDERID_Desktop);
#else
    const std::string home = env("HOME");
    std::ifstream dirs(fs::path(env("XDG_CONFIG_HOME").empty() ? home + "/.config" : env("XDG_CONFIG_HOME")) / "user-dirs.dirs");
    std::string line;
    while (std::getline(dirs, line)) {
        if (line.rfind("XDG_DESKTOP_DIR=", 0) != 0) continue;
        std::string value = line.substr(16);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        const std::string key = "$HOME";
        if (value.rfind(key, 0) == 0) value = home + value.substr(key.size());
        return value;
    }
    return (fs::path(home) / "Desktop").string();
#endif
}

InstallReport install_integration(const InstallOptions& o) {
    InstallReport report;
    const std::string exe = canopy_executable();
    const std::string app = canopy_app_executable();
#if defined(_WIN32)
    const std::string bin = fs::path(exe).parent_path().string();
    if (o.command) {
        step(report, "add Canopy's folder to your PATH", bin, edit_user_path(wide(bin), true));
        set_reg_string(HKEY_CURRENT_USER, k_app_paths, L"", wide(exe));
        set_reg_string(HKEY_CURRENT_USER, k_app_paths, L"Path", wide(bin));
        if (!on_path_dir(bin)) report.hints.push_back("Open a new terminal (or sign out and back in) for the canopy command to work there.");
    }
    if (o.menu) {
        const std::string link = (fs::path(known_folder(FOLDERID_Programs)) / "Canopy.lnk").string();
        step(report, "Start menu shortcut", link, make_shortcut(link, app, "", "Canopy code editor"));
    }
    if (o.desktop_shortcut) {
        const std::string link = (fs::path(desktop_folder()) / "Canopy.lnk").string();
        step(report, "desktop shortcut", link, make_shortcut(link, app, "", "Canopy code editor"));
    }
    if (o.context_menu) {
        const std::wstring icon = wide(app);
        bool ok = true;
        const std::wstring folder_command = L"\"" + icon + L"\" --wait \"%V\"";
        const std::wstring file_command = L"\"" + icon + L"\" --wait \"%1\"";
        for (std::size_t i = 0; i < 3; ++i) {
            const std::wstring key = k_context_keys[i];
            ok = set_reg_string(HKEY_CURRENT_USER, key, L"", L"Open with Canopy") && ok;
            ok = set_reg_string(HKEY_CURRENT_USER, key, L"Icon", icon) && ok;
            ok = set_reg_string(HKEY_CURRENT_USER, key + L"\\command", L"", i == 2 ? file_command : folder_command) && ok;
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        step(report, "\"Open with Canopy\" in Explorer's right-click menu", "HKCU\\Software\\Classes", ok);
    }
    if (o.file_types) {
        bool ok = true;
        const std::wstring command = L"\"" + wide(app) + L"\" --wait \"%1\"";
        for (const FileType& t : canopy_file_types()) {
            const std::wstring prog = L"Software\\Classes\\" + wide(t.prog_id);
            std::string icon = canopy_icon("filetypes/" + mime_icon_name(t) + ".ico");
            if (icon.empty()) icon = app + ",0";
            ok = set_reg_string(HKEY_CURRENT_USER, prog, L"", wide(t.description)) && ok;
            ok = set_reg_string(HKEY_CURRENT_USER, prog + L"\\DefaultIcon", L"", wide(icon)) && ok;
            ok = set_reg_string(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", L"", command) && ok;
            for (const std::string& e : t.extensions) {
                const std::wstring ext = L"Software\\Classes\\" + wide(e);
                ok = set_reg_string(HKEY_CURRENT_USER, ext + L"\\OpenWithProgids", wide(t.prog_id), L"") && ok;
                std::wstring current;
                DWORD type = REG_SZ;
                if (!get_reg_string(HKEY_CURRENT_USER, ext, L"", current, type) || current.empty()) ok = set_reg_string(HKEY_CURRENT_USER, ext, L"", wide(t.prog_id)) && ok;
            }
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        step(report, ".wal, .walh, .grove and .bark files open in Canopy, with their icons", "HKCU\\Software\\Classes", ok);
    }
    if (o.scope == InstallScope::System) report.hints.push_back("On Windows the installer sets Canopy up for every user; this only set it up for you.");
#else
    if (o.command) {
        const fs::path link = fs::path(bin_home(o)) / "canopy";
        std::error_code ec;
        fs::create_directories(link.parent_path(), ec);
        remove_path(link);
        fs::create_symlink(exe, link, ec);
        step(report, "canopy command", link.string(), !ec, ec ? ec.message() : std::string());
        if (!ec && !on_path_dir(link.parent_path())) {
            report.hints.push_back(link.parent_path().string() + " isn't on your PATH yet. Add this line to ~/.bashrc (or your shell's startup file), then open a new terminal:\n    export PATH=\"" + link.parent_path().string() + ":$PATH\"");
        }
    }
    const fs::path share = data_home(o);
    const std::string icon_name = "canopy";
    const std::string svg = canopy_icon("canopy.svg");
    if (o.menu || o.desktop_shortcut) {
        bool icons = !svg.empty() && copy_into(svg, share / "icons" / "hicolor" / "scalable" / "apps" / "canopy.svg");
        for (const char* size : { "16", "32", "48", "64", "128", "256", "512" }) {
            const std::string png = canopy_icon(std::string("canopy-") + size + ".png");
            if (!png.empty()) icons = copy_into(png, share / "icons" / "hicolor" / (std::string(size) + "x" + size) / "apps" / "canopy.png") && icons;
        }
        step(report, "icons", (share / "icons" / "hicolor").string(), true, icons ? std::string() : "couldn't find Canopy's icons; the menu entry will have a generic icon");
        run_quietly("gtk-update-icon-cache", { "-f", "-t", (share / "icons" / "hicolor").string() });
    }
    if (o.file_types) {
        bool ok = write_file(share / "mime" / "packages" / "canopy.xml", shared_mime_info());
        for (const FileType& t : canopy_file_types()) {
            const std::string name = mime_icon_name(t);
            const std::string svg_icon = canopy_icon("mime/" + name + ".svg");
            if (!svg_icon.empty()) copy_into(svg_icon, share / "icons" / "hicolor" / "scalable" / "mimetypes" / (name + ".svg"));
            for (const char* size : { "16", "32", "48", "64", "128", "256" }) {
                const std::string png = canopy_icon("mime/" + name + "-" + size + ".png");
                if (!png.empty()) copy_into(png, share / "icons" / "hicolor" / (std::string(size) + "x" + size) / "mimetypes" / (name + ".png"));
            }
        }
        run_quietly("update-mime-database", { (share / "mime").string() });
        run_quietly("gtk-update-icon-cache", { "-f", "-t", (share / "icons" / "hicolor").string() });
        step(report, ".wal, .walh, .grove and .bark file types with their icons", (share / "mime" / "packages" / "canopy.xml").string(), ok);
    }
    if (o.menu) {
        const fs::path entry = share / "applications" / "canopy.desktop";
        step(report, "applications menu entry", entry.string(), write_file(entry, desktop_entry(exe, icon_name)));
        run_quietly("update-desktop-database", { (share / "applications").string() });
    }
    if (o.desktop_shortcut) {
        const fs::path shortcut = fs::path(desktop_folder()) / "canopy.desktop";
        const std::string icon = svg.empty() ? icon_name : (share / "icons" / "hicolor" / "scalable" / "apps" / "canopy.svg").string();
        const bool ok = write_file(shortcut, desktop_entry(exe, icon));
        std::error_code ec;
        fs::permissions(shortcut, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add, ec);
        run_quietly("gio", { "set", shortcut.string(), "metadata::trusted", "true" });
        step(report, "desktop shortcut", shortcut.string(), ok);
        if (ok) report.hints.push_back("If your desktop shows the shortcut as untrusted, right-click it and choose \"Allow Launching\".");
    }
    (void)o.context_menu;
#endif
    return report;
}

InstallReport uninstall_integration(const InstallOptions& o) {
    InstallReport report;
#if defined(_WIN32)
    const std::string bin = fs::path(canopy_executable()).parent_path().string();
    if (o.command) {
        step(report, "remove Canopy's folder from your PATH", bin, edit_user_path(wide(bin), false));
        delete_reg_tree(HKEY_CURRENT_USER, k_app_paths);
    }
    if (o.menu) {
        const fs::path link = fs::path(known_folder(FOLDERID_Programs)) / "Canopy.lnk";
        step(report, "Start menu shortcut", link.string(), remove_path(link));
    }
    {
        const fs::path link = fs::path(desktop_folder()) / "Canopy.lnk";
        step(report, "desktop shortcut", link.string(), remove_path(link));
    }
    if (o.context_menu) {
        bool ok = true;
        for (const wchar_t* key : k_context_keys) ok = delete_reg_tree(HKEY_CURRENT_USER, key) && ok;
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        step(report, "\"Open with Canopy\" menu", "HKCU\\Software\\Classes", ok);
    }
    if (o.file_types) {
        bool ok = true;
        for (const FileType& t : canopy_file_types()) {
            ok = delete_reg_tree(HKEY_CURRENT_USER, L"Software\\Classes\\" + wide(t.prog_id)) && ok;
            for (const std::string& e : t.extensions) {
                const std::wstring ext = L"Software\\Classes\\" + wide(e);
                HKEY h = nullptr;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, (ext + L"\\OpenWithProgids").c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
                    RegDeleteValueW(h, wide(t.prog_id).c_str());
                    RegCloseKey(h);
                }
                std::wstring current;
                DWORD type = REG_SZ;
                if (get_reg_string(HKEY_CURRENT_USER, ext, L"", current, type) && current == wide(t.prog_id)) {
                    if (RegOpenKeyExW(HKEY_CURRENT_USER, ext.c_str(), 0, KEY_SET_VALUE, &h) == ERROR_SUCCESS) {
                        RegDeleteValueW(h, nullptr);
                        RegCloseKey(h);
                    }
                }
            }
        }
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        step(report, "file types", "HKCU\\Software\\Classes", ok);
    }
#else
    const fs::path share = data_home(o);
    if (o.command) {
        const fs::path link = fs::path(bin_home(o)) / "canopy";
        std::error_code ec;
        if (fs::is_symlink(link, ec) && fs::read_symlink(link, ec).filename() == "canopy") step(report, "canopy command", link.string(), remove_path(link));
        else if (fs::exists(link, ec)) step(report, "canopy command", link.string(), true, "left alone: it isn't a link Canopy made");
    }
    if (o.menu) {
        const fs::path entry = share / "applications" / "canopy.desktop";
        step(report, "applications menu entry", entry.string(), remove_path(entry));
        run_quietly("update-desktop-database", { (share / "applications").string() });
        bool icons = remove_path(share / "icons" / "hicolor" / "scalable" / "apps" / "canopy.svg");
        for (const char* size : { "16", "32", "48", "64", "128", "256", "512" }) {
            icons = remove_path(share / "icons" / "hicolor" / (std::string(size) + "x" + size) / "apps" / "canopy.png") && icons;
        }
        step(report, "icons", (share / "icons" / "hicolor").string(), icons);
    }
    if (o.file_types) {
        bool ok = remove_path(share / "mime" / "packages" / "canopy.xml");
        for (const FileType& t : canopy_file_types()) {
            const std::string name = mime_icon_name(t);
            remove_path(share / "icons" / "hicolor" / "scalable" / "mimetypes" / (name + ".svg"));
            for (const char* size : { "16", "32", "48", "64", "128", "256" }) remove_path(share / "icons" / "hicolor" / (std::string(size) + "x" + size) / "mimetypes" / (name + ".png"));
        }
        run_quietly("update-mime-database", { (share / "mime").string() });
        step(report, "file types", (share / "mime" / "packages" / "canopy.xml").string(), ok);
    }
    if (o.scope == InstallScope::User) {
        const fs::path shortcut = fs::path(desktop_folder()) / "canopy.desktop";
        step(report, "desktop shortcut", shortcut.string(), remove_path(shortcut));
    }
#endif
    return report;
}

IntegrationStatus integration_status() {
    IntegrationStatus s;
    if (const auto found = find_on_path("canopy")) {
        s.command_path = *found;
        std::error_code ec;
        s.command_is_this = fs::equivalent(*found, canopy_executable(), ec);
    }
#if defined(_WIN32)
    const fs::path menu = fs::path(known_folder(FOLDERID_Programs)) / "Canopy.lnk";
    const fs::path desk = fs::path(desktop_folder()) / "Canopy.lnk";
    const fs::path common_menu = fs::path(known_folder(FOLDERID_CommonPrograms)) / "Canopy";
    std::error_code ec;
    if (fs::exists(menu, ec)) s.menu_entry = menu.string();
    else if (fs::exists(common_menu, ec)) s.menu_entry = common_menu.string();
    if (fs::exists(desk, ec)) s.desktop_shortcut = desk.string();
#else
    InstallOptions user;
    std::error_code ec;
    for (const fs::path& p : { fs::path(data_home(user)) / "applications" / "canopy.desktop", fs::path("/usr/share/applications/canopy.desktop"), fs::path("/usr/local/share/applications/canopy.desktop") }) {
        if (fs::exists(p, ec)) {
            s.menu_entry = p.string();
            break;
        }
    }
    const fs::path desk = fs::path(desktop_folder()) / "canopy.desktop";
    if (fs::exists(desk, ec)) s.desktop_shortcut = desk.string();
#endif
    return s;
}

bool has_display() {
#if defined(_WIN32) || defined(__APPLE__)
    return true;
#else
    return !env("DISPLAY").empty() || !env("WAYLAND_DISPLAY").empty();
#endif
}

bool launch_detached(const std::string& program, const std::vector<std::string>& args) {
#if defined(_WIN32)
    std::wstring line = L"\"" + wide(program) + L"\"";
    for (const std::string& a : args) {
        std::wstring w = wide(a);
        std::wstring quoted = L"\"";
        std::size_t slashes = 0;
        for (wchar_t c : w) {
            if (c == L'\\') {
                ++slashes;
                continue;
            }
            if (c == L'"') quoted.append(slashes * 2 + 1, L'\\');
            else quoted.append(slashes, L'\\');
            slashes = 0;
            quoted += c;
        }
        quoted.append(slashes * 2, L'\\');
        quoted += L"\"";
        line += L" " + quoted;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipefd[0]);
        setsid();
        const pid_t inner = fork();
        if (inner != 0) _exit(inner < 0 ? 1 : 0);
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(program.c_str(), argv.data());
        const char failed = 1;
        (void)!write(pipefd[1], &failed, 1);
        _exit(127);
    }
    close(pipefd[1]);
    int status = 0;
    waitpid(pid, &status, 0);
    char failed = 0;
    const ssize_t n = read(pipefd[0], &failed, 1);
    close(pipefd[0]);
    return n <= 0;
#endif
}

bool move_to_trash(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec))) return false;
#if defined(_WIN32)
    std::wstring from = wide(fs::path(path).make_preferred().string());
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&op) != 0 || op.fAnyOperationsAborted) return false;
#elif defined(__APPLE__)
    std::string escaped;
    for (char c : path) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += c;
    }
    if (run_command({ "osascript", "-e", "tell application \"Finder\" to delete POSIX file \"" + escaped + "\"" }) != 0) return false;
#else
    std::string out;
    if (run_command_capture({ "gio", "trash", "--", path }, out) != 0) return false;
#endif
    return !fs::exists(fs::symlink_status(path, ec));
}

std::string file_uri(const std::string& path) {
    std::error_code ec;
    std::string absolute = fs::absolute(path, ec).generic_string();
    if (ec) absolute = path;
    static const char* hex = "0123456789ABCDEF";
    std::string out = "file://";
    if (absolute.empty() || absolute[0] != '/') out += '/';
    const bool drive = absolute.size() >= 2 && absolute[1] == ':' && std::isalpha(static_cast<unsigned char>(absolute[0]));
    for (std::size_t i = 0; i < absolute.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(absolute[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || (drive && i == 1);
        if (plain) out += static_cast<char>(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

bool open_in_browser(const std::string& url) {
    if (url.empty()) return false;
#if defined(_WIN32)
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#elif defined(__APPLE__)
    return launch_detached("open", { url });
#else
    return launch_detached("xdg-open", { url });
#endif
}

bool reveal_in_file_manager(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec))) return false;
#if defined(_WIN32)
    const std::wstring target = wide(fs::absolute(path, ec).make_preferred().string());
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool ok = false;
    if (PIDLIST_ABSOLUTE item = ILCreateFromPathW(target.c_str())) {
        ok = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
        ILFree(item);
    }
    if (SUCCEEDED(init)) CoUninitialize();
    if (!ok) {
        const std::wstring folder = wide((fs::is_directory(path, ec) ? fs::path(path) : fs::path(path).parent_path()).make_preferred().string());
        ok = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }
    return ok;
#elif defined(__APPLE__)
    return launch_detached("open", { "-R", fs::absolute(path, ec).string() });
#else
    const std::string absolute = fs::absolute(path, ec).string();
    const std::string folder = fs::is_directory(absolute, ec) ? absolute : fs::path(absolute).parent_path().string();
    const std::string script =
        "uri=\"$1\"; folder=\"$2\"; "
        "if command -v gdbus >/dev/null 2>&1 && gdbus call --session --timeout 3 --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 "
        "--method org.freedesktop.FileManager1.ShowItems \"['$uri']\" '' >/dev/null 2>&1; then exit 0; fi; "
        "if command -v dbus-send >/dev/null 2>&1 && dbus-send --session --print-reply --reply-timeout=3000 --dest=org.freedesktop.FileManager1 /org/freedesktop/FileManager1 "
        "org.freedesktop.FileManager1.ShowItems array:string:\"$uri\" string:'' >/dev/null 2>&1; then exit 0; fi; "
        "exec xdg-open \"$folder\"";
    return launch_detached("/bin/sh", { "-c", script, "canopy-reveal", file_uri(absolute), folder });
#endif
}

} // namespace canopy
