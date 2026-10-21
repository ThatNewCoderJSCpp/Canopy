#include <canopy/shell.hpp>

#include <canopy/build.hpp>
#include <canopy/code_intel.hpp>
#include <canopy/completion.hpp>
#include <canopy/desktop.hpp>
#include <canopy/developer.hpp>
#include <canopy/console_panel.hpp>
#include <canopy/elements_panel.hpp>
#include <canopy/web_intel.hpp>
#include <canopy/asm_info.hpp>
#include <canopy/lang_intel.hpp>
#include <canopy/find_bar.hpp>
#include <canopy/special_names.hpp>
#include <canopy/git_panel.hpp>
#include <canopy/history.hpp>
#include <canopy/languages.hpp>
#include <canopy/live_server.hpp>
#include <canopy/markdown.hpp>
#include <canopy/settings_page.hpp>
#include <canopy/shortcuts.hpp>
#include <canopy/terminal.hpp>
#include <canopy/version.hpp>
#include <canopy/viewers.hpp>
#include <canopy/walnut_semantic.hpp>

#include <grove/dom_tools.hpp>
#include <grove/input/code_buffer.hpp>
#include <grove/input/input.hpp>
#include <grove/native.hpp>
#include <grove/runtime/runtime.hpp>
#include <grove/terminal.hpp>
#include <grove/windowing.hpp>

#include <cstdio>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <map>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;

namespace canopy {

namespace {

const std::string k_settings_tab = "canopy:settings";
const std::string k_developer_tab = "canopy:developer";

bool virtual_tab(const std::string& path) {
    return path.rfind("canopy:", 0) == 0;
}

std::map<std::string, std::string>& virtual_titles() {
    static std::map<std::string, std::string> titles;
    return titles;
}

grove::Node* add(grove::Document& doc, grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes = {}) {
    grove::Node* el = doc.create_element(tag);
    if (!classes.empty()) el->set_attribute("class", classes);
    if (!text.empty()) el->set_inner_text(text);
    return doc.append_child(parent, *el);
}

bool has_text(const grove::Node& el, const std::string& text) {
    const auto& kids = el.children();
    if (kids.empty()) return text.empty();
    return kids.size() == 1 && !kids.front()->is_element() && kids.front()->text() == text;
}

void set_text(grove::Document& doc, grove::Node& el, const std::string& text) {
    if (has_text(el, text)) return;
    el.set_inner_text(text);
    doc.touch();
}

void set_text(grove::Document& doc, const std::string& id, const std::string& text) {
    if (grove::Node* el = doc.find_by_id(id)) set_text(doc, *el, text);
}

class ListSync {
public:
    ListSync(grove::Document& doc, grove::Node& parent) : m_doc(doc), m_parent(parent) {}
    ListSync(const ListSync&) = delete;
    ListSync& operator=(const ListSync&) = delete;
    ~ListSync() { finish(); }

    grove::Node* add(const std::string& tag, const std::string& text, const std::string& classes = {}) {
        settle();
        const auto& kids = m_parent.children();
        grove::Node* node = m_at < kids.size() && plain(*kids[m_at], tag) ? kids[m_at].get() : nullptr;
        if (node) {
            if (classes.empty()) node->remove_attribute("class");
            else node->set_attribute("class", classes);
            set_text(m_doc, *node, text);
        } else {
            node = m_doc.create_element(tag);
            if (!classes.empty()) node->set_attribute("class", classes);
            if (!text.empty()) node->set_inner_text(text);
            m_doc.insert_before(m_parent, *node, m_at < kids.size() ? kids[m_at].get() : nullptr);
        }
        ++m_at;
        m_last = node;
        m_kept.clear();
        return node;
    }

    void set(const std::string& name, const std::string& value) {
        if (!m_last) return;
        m_last->set_attribute(name, value);
        m_kept.push_back(name);
    }

    std::size_t size() const noexcept { return m_at; }

    void finish() {
        settle();
        while (m_parent.children().size() > m_at) {
            if (!m_doc.remove(*m_parent.children().back())) break;
        }
    }

private:
    static bool plain(const grove::Node& node, const std::string& tag) {
        if (!node.is_element() || node.tag() != tag) return false;
        const auto& kids = node.children();
        return kids.empty() || (kids.size() == 1 && kids.front()->is_text());
    }

    void settle() {
        if (!m_last) return;
        std::vector<std::string> stale;
        for (const grove::Attribute& a : m_last->attributes()) {
            if (a.name == "class") continue;
            if (std::find(m_kept.begin(), m_kept.end(), a.name) == m_kept.end()) stale.push_back(a.name);
        }
        for (const std::string& name : stale) m_last->remove_attribute(name);
        m_last = nullptr;
        m_kept.clear();
    }

    grove::Document& m_doc;
    grove::Node& m_parent;
    std::size_t m_at = 0;
    grove::Node* m_last = nullptr;
    std::vector<std::string> m_kept;
};

void clear(grove::Document& doc, grove::Node& node) {
    while (!node.children().empty()) {
        grove::Node* last = node.children().back().get();
        if (!doc.remove(*last)) {
            node.clear_children();
            break;
        }
    }
    doc.touch();
}

std::string tool_label(const Toolchain& toolchain, const std::string& key, const std::string& name) {
    const Tool* t = toolchain.get(key);
    if (!t) return name + ": missing";
    return name + ": " + fs::path(t->path).filename().string();
}

std::string code_language_name(const std::string& path) {
    const Language* lang = language_for_path(path);
    return lang ? lang->key : std::string(grove::to_string(grove::code_language_for_path(path)));
}

std::string display_name(const std::string& path) {
    if (path == k_settings_tab) return "Settings";
    if (virtual_tab(path)) {
        const auto it = virtual_titles().find(path);
        return it == virtual_titles().end() ? path : it->second;
    }
    const std::string name = fs::path(path).filename().string();
    return name.empty() ? path : name;
}

const char* icon_for_name(const std::string& base) {
    static const std::map<std::string, const char*> names = {
        { "cmakelists.txt", "cmake" }, { "makefile", "make" }, { "gnumakefile", "make" }, { "dockerfile", "docker" },
        { "containerfile", "docker" }, { "gemfile", "ruby" }, { "rakefile", "ruby" }, { ".gitignore", "git" },
        { ".gitattributes", "git" }, { ".gitmodules", "git" }, { ".gitkeep", "git" }, { ".mailmap", "git" },
        { "meson.build", "meson" }, { "meson_options.txt", "meson" }, { ".env", "env" }, { "build.gradle", "groovy" },
        { "dune", "ocaml" }, { "dune-project", "ocaml" }, { "dune-workspace", "ocaml" }, { ".ocamlformat", "ocaml" },
        { ".editorconfig", "config" }, { ".clang-format", "config" }, { ".clang-tidy", "config" },
    };
    const auto it = names.find(base);
    return it == names.end() ? nullptr : it->second;
}

const char* icon_for_extension(const std::string& ext) {
    static const std::map<std::string, const char*> exts = {
        { ".cmake", "cmake" }, { ".wal", "walnut" }, { ".walh", "walnut-header" }, { ".hwal", "walnut-header" },
        { ".grove", "grove" }, { ".bark", "bark" }, { ".c", "c" }, { ".cpp", "cpp" }, { ".cc", "cpp" }, { ".cxx", "cpp" },
        { ".c++", "cpp" }, { ".h", "h" }, { ".hpp", "hpp" }, { ".hh", "hpp" }, { ".hxx", "hpp" }, { ".h++", "hpp" },
        { ".ipp", "header" }, { ".tpp", "header" },
        { ".s", "asm" }, { ".asm", "asm" }, { ".nasm", "asm" },
        { ".md", "markdown" }, { ".markdown", "markdown" }, { ".mdx", "markdown" },
        { ".html", "html" }, { ".htm", "html" }, { ".xhtml", "html" }, { ".css", "css" }, { ".scss", "scss" }, { ".sass", "scss" },
        { ".less", "less" }, { ".js", "js" }, { ".mjs", "js" }, { ".cjs", "js" }, { ".jsx", "js" }, { ".ts", "ts" },
        { ".mts", "ts" }, { ".cts", "ts" }, { ".tsx", "ts" }, { ".json", "json" }, { ".jsonc", "json" }, { ".json5", "json" },
        { ".yml", "yaml" }, { ".yaml", "yaml" }, { ".toml", "toml" }, { ".xml", "xml" }, { ".xsd", "xml" }, { ".xsl", "xml" },
        { ".plist", "xml" }, { ".ini", "config" }, { ".cfg", "config" }, { ".conf", "config" },
        { ".py", "python" }, { ".pyw", "python" }, { ".pyi", "python" }, { ".rs", "rust" }, { ".go", "go" },
        { ".java", "java" }, { ".kt", "kotlin" }, { ".kts", "kotlin" }, { ".swift", "swift" }, { ".cs", "csharp" },
        { ".fs", "fsharp" }, { ".fsx", "fsharp" }, { ".fsi", "fsharp" }, { ".rb", "ruby" }, { ".php", "php" },
        { ".pl", "perl" }, { ".pm", "perl" }, { ".lua", "lua" }, { ".r", "r" }, { ".jl", "julia" }, { ".hs", "haskell" },
        { ".lhs", "haskell" }, { ".ml", "ocaml" }, { ".mli", "ocaml" }, { ".mll", "ocaml" }, { ".mly", "ocaml" }, { ".opam", "ocaml" }, { ".ex", "elixir" }, { ".exs", "elixir" },
        { ".erl", "erlang" }, { ".hrl", "erlang" }, { ".clj", "clojure" }, { ".cljs", "clojure" }, { ".cljc", "clojure" },
        { ".edn", "clojure" }, { ".scala", "scala" }, { ".sc", "scala" }, { ".dart", "dart" }, { ".f", "fortran" },
        { ".for", "fortran" }, { ".ftn", "fortran" }, { ".f77", "fortran" }, { ".f90", "fortran" }, { ".f95", "fortran" },
        { ".f03", "fortran" }, { ".f08", "fortran" }, { ".zig", "zig" }, { ".zon", "zig" }, { ".nim", "nim" },
        { ".nims", "nim" }, { ".d", "d" }, { ".cr", "crystal" }, { ".elm", "elm" }, { ".lisp", "lisp" }, { ".lsp", "lisp" },
        { ".el", "lisp" }, { ".scm", "lisp" }, { ".ss", "lisp" }, { ".rkt", "lisp" }, { ".pro", "prolog" },
        { ".cob", "cobol" }, { ".cbl", "cobol" }, { ".pas", "pascal" }, { ".pp", "pascal" }, { ".adb", "ada" },
        { ".ads", "ada" }, { ".sh", "shell" }, { ".bash", "shell" }, { ".zsh", "shell" }, { ".fish", "shell" },
        { ".ksh", "shell" }, { ".ps1", "powershell" }, { ".psm1", "powershell" }, { ".psd1", "powershell" },
        { ".bat", "batch" }, { ".cmd", "batch" }, { ".mk", "make" }, { ".mak", "make" }, { ".dockerfile", "docker" },
        { ".sql", "sql" }, { ".glsl", "shader" }, { ".vert", "shader" }, { ".frag", "shader" }, { ".geom", "shader" },
        { ".comp", "shader" }, { ".tesc", "shader" }, { ".tese", "shader" }, { ".hlsl", "shader" }, { ".wgsl", "shader" },
        { ".metal", "shader" }, { ".cu", "cuda" }, { ".cuh", "cuda" }, { ".v", "verilog" }, { ".sv", "verilog" },
        { ".svh", "verilog" }, { ".vhd", "vhdl" }, { ".vhdl", "vhdl" }, { ".m", "objc" }, { ".mm", "objc" },
        { ".nix", "nix" }, { ".proto", "proto" }, { ".graphql", "graphql" }, { ".gql", "graphql" }, { ".vue", "vue" },
        { ".svelte", "svelte" }, { ".wat", "wasm" }, { ".wast", "wasm" }, { ".wasm", "wasm" }, { ".sol", "solidity" },
        { ".lean", "lean" }, { ".gleam", "gleam" }, { ".odin", "odin" }, { ".groovy", "groovy" }, { ".gradle", "groovy" },
        { ".tcl", "tcl" }, { ".awk", "awk" }, { ".diff", "diff" }, { ".patch", "diff" }, { ".csv", "csv" }, { ".tsv", "csv" },
    };
    const auto it = exts.find(ext);
    return it == exts.end() ? nullptr : it->second;
}

const char* icon_for_folder(const std::string& base) {
    if (base == ".vscode") return "folder-vscode";
    if (base == ".github") return "folder-github";
    if (base == ".git") return "folder-git";
    if (base == ".canopy") return "folder-canopy";
    if (base == "cmake") return "folder-cmake";
    return "folder";
}

std::string file_icon(const std::string& path, bool directory, bool open) {
    std::string base = fs::path(path).filename().string();
    for (char& c : base) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (directory) {
        const std::string folder = std::string("files/") + icon_for_folder(base) + (open ? "-open" : "");
        for (const char* ext : { ".webp", ".png" }) {
            const std::string icon = canopy_icon(folder + ext);
            if (!icon.empty()) return icon;
        }
        return canopy_icon(open ? "files/folder-open.png" : "files/folder.png");
    }
    std::string ext = fs::path(path).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char* name = icon_for_name(base);
    if (!name && base.rfind("dockerfile.", 0) == 0) name = "docker";
    if (!name && (base.rfind(".env.", 0) == 0)) name = "env";
    if (!name) name = icon_for_extension(ext);
    if (!name) {
        if (is_image_path(path)) name = "image";
        else if (is_pdf_path(path)) name = "pdf";
        else if (is_latex_path(path)) name = "tex";
        else if (is_binary_path(path)) name = "binary";
        else name = "file";
    }
    for (const char* kind : { ".webp", ".png" }) {
        const std::string icon = canopy_icon(std::string("files/") + name + kind);
        if (!icon.empty()) return icon;
    }
    return canopy_icon("files/file.png");
}

grove::Node* add_icon(grove::Document& doc, grove::Node& parent, const std::string& icon) {
    if (icon.empty()) return nullptr;
    grove::Node* img = doc.create_element("img");
    img->set_attribute("src", icon);
    img->set_attribute("width", "16");
    img->set_attribute("class", "file-icon");
    return doc.append_child(parent, *img);
}

void set_window_icon(grove::Document& doc) {
    grove::Node* body = doc.first_by_tag("body");
    if (!body) return;
    std::string icons;
    for (const char* size : { "16", "32", "48", "64", "128", "256" }) {
        const std::string path = canopy_icon(std::string("canopy-") + size + ".png");
        if (!path.empty()) icons += (icons.empty() ? "" : " ") + path;
    }
    if (!icons.empty()) body->set_attribute("icon", icons);
    body->set_attribute("app-id", "canopy");
}

std::string normalized(const std::string& path) {
    std::error_code ec;
    const fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
    return ec ? path : p.string();
}

bool path_inside(const std::string& path, const std::string& folder) {
    if (folder.empty()) return false;
    if (path == folder) return true;
    if (path.size() <= folder.size() || path.compare(0, folder.size(), folder) != 0) return false;
    const char next = path[folder.size()];
    return next == '/' || next == '\\' || folder.back() == '/' || folder.back() == '\\';
}

std::string workspace_folders_path() {
    return (fs::path(user_config_dir()) / "workspace-folders.txt").string();
}

std::vector<std::pair<std::string, std::string>> read_workspace_folders() {
    std::vector<std::pair<std::string, std::string>> out;
    std::ifstream in(workspace_folders_path());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }
    return out;
}

std::vector<std::string> saved_extra_roots(const std::string& root) {
    std::vector<std::string> out;
    std::error_code ec;
    const std::string want = clean_folder_path(root);
    for (const auto& [owner, raw] : read_workspace_folders()) {
        const std::string folder = clean_folder_path(raw);
        if (clean_folder_path(owner) == want && folder != want && fs::is_directory(folder, ec) && std::find(out.begin(), out.end(), folder) == out.end()) out.push_back(folder);
    }
    return out;
}

void save_extra_roots(const std::string& root, const std::vector<std::string>& extras) {
    if (root.empty()) return;
    std::vector<std::pair<std::string, std::string>> all = read_workspace_folders();
    const std::string owner = clean_folder_path(root);
    all.erase(std::remove_if(all.begin(), all.end(), [&](const auto& e) { return clean_folder_path(e.first) == owner; }), all.end());
    for (const std::string& e : extras) all.emplace_back(owner, clean_folder_path(e));
    std::error_code ec;
    fs::create_directories(user_config_dir(), ec);
    std::ofstream out(workspace_folders_path(), std::ios::trunc);
    for (const auto& [owner, folder] : all) out << owner << '\t' << folder << '\n';
}

std::string unique_destination(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    fs::path candidate = dir / name;
    if (!fs::exists(fs::symlink_status(candidate, ec))) return candidate.string();
    const fs::path base(name);
    const bool folder = fs::is_directory(candidate, ec);
    const std::string stem = folder ? name : base.stem().string();
    const std::string ext = folder ? std::string() : base.extension().string();
    for (int i = 1; i < 10000; ++i) {
        candidate = dir / (stem + (i == 1 ? std::string(" copy") : " copy " + std::to_string(i)) + ext);
        if (!fs::exists(fs::symlink_status(candidate, ec))) return candidate.string();
    }
    return (dir / (name + " copy")).string();
}

std::string bad_file_name(const std::string& name) {
    if (name.empty()) return "The name can't be empty.";
    if (name == "." || name == "..") return "That name is reserved.";
    if (name.find_first_of("/\\") != std::string::npos) return "A name can't contain / or \\.";
#if defined(_WIN32)
    if (name.find_first_of(":*?\"<>|") != std::string::npos) return "A name can't contain any of : * ? \" < > |";
    if (name.back() == '.' || name.back() == ' ') return "A name can't end with a dot or a space on Windows.";
#endif
    return std::string();
}

std::string trimmed(const std::string& text) {
    const std::size_t b = text.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return std::string();
    const std::size_t e = text.find_last_not_of(" \t\r\n");
    return text.substr(b, e - b + 1);
}

bool chord_is(const grove::Event& e, const char* spec) {
    const std::optional<grove::Chord> want = grove::parse_chord(spec);
    return want && grove::chord_of(e.key, e.modifiers) == *want;
}

const grove::Node* with_attribute(const grove::Node* node, const char* name) {
    for (const grove::Node* n = node; n; n = n->parent()) {
        if (n->is_element() && n->has_attribute(name)) return n;
    }
    return nullptr;
}

class EditorShell {
public:
    EditorShell(grove::Project& project, Workspace workspace) : m_project(project), m_doc(project.document), m_ws(std::move(workspace)) {}

    void install() {
        m_themes = load_themes(m_ws.diagnostics);
        set_window_icon(m_doc);
        load_roots();
        populate_shell(m_project, m_ws);
        refresh_all();
        listen_by_id("explorer", "click", [this](grove::Event& e) { explorer_click(e); });
        listen_by_id("explorer", "double_click", [this](grove::Event& e) { explorer_double_click(e); });
        listen_by_id("explorer", "mouse_down", [this](grove::Event& e) { explorer_mouse_down(e); });
        listen_by_id("explorer", "key_down", [this](grove::Event& e) { explorer_key(e); });
        listen_by_id("explorer", "menu", [this](grove::Event& e) { explorer_menu(e.key); });
        listen_by_id("tabs", "double_click", [this](grove::Event& e) {
            if (const grove::Node* tab = with_attribute(e.target, "data-path")) {
                if (OpenFile* f = find_file(tab->attribute_or("data-path", ""))) pin(*f);
            }
        });
        listen_by_id("add-folder", "change", [this](grove::Event& e) {
            if (!e.target) return;
            const std::string value = grove::input_string(*e.target).value_or("");
            std::size_t start = 0;
            while (start < value.size()) {
                const std::size_t end = value.find('\n', start);
                add_folder(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        });
        listen_by_id("terminal-picker", "change", [this](grove::Event& e) {
            if (e.target) pick_terminal(grove::input_string(*e.target).value_or(""));
        });
        listen_by_id("terminal-new", "click", [this](grove::Event&) {
            const TerminalSession* t = active_terminal();
            new_terminal(t ? t->folder : m_ws.root);
        });
        listen_by_id("terminal-clear", "click", [this](grove::Event&) { clear_terminal(); });
        listen_by_id("server-start", "click", [this](grove::Event&) { start_server_from_bar(); });
        listen_by_id("servers", "click", [this](grove::Event& e) { server_click(e); });
        listen_by_id("terminal-kill", "click", [this](grove::Event&) { kill_terminal(); });
        listen_by_id("terminal-views", "mouse_down", [this](grove::Event& e) {
            if (e.button == 2) terminal_menu(e);
        });
        listen_by_id("terminal-views", "menu", [this](grove::Event& e) { terminal_menu_action(e.key); });
        listen_by_id("recent", "click", [this](grove::Event& e) {
            if (const grove::Node* item = with_attribute(e.target, "data-path")) open_folder(item->attribute_or("data-path", ""));
        });
        listen_by_id("tabs", "click", [this](grove::Event& e) { tab_click(e); });
        for (const char* tabs : { "sidebar-tabs", "panel-tabs", "aside-tabs" }) {
            listen_by_id(tabs, "click", [this](grove::Event& e) {
                if (const grove::Node* tab = with_attribute(e.target, "data-view")) show_panel(tab->attribute_or("data-view", ""));
            });
            listen_by_id(tabs, "mouse_down", [this](grove::Event& e) {
                if (e.button == 2) tab_menu(e);
            });
            listen_by_id(tabs, "menu", [this](grove::Event& e) { tab_menu_action(e.key); });
        }
        listen_by_id("shell", "mouse_down", [this](grove::Event& e) { layout_mouse_down(e); });
        for (const char* id : { "view-output", "view-problems", "view-warnings" }) {
            listen_by_id(id, "mouse_down", [this, id](grove::Event& e) {
                if (e.button == 2) panel_menu(id, e);
            });
            listen_by_id(id, "menu", [this](grove::Event& e) { panel_menu_action(e.key); });
        }
        listen_by_id("problems", "click", [this](grove::Event& e) {
            if (const grove::Node* page = with_attribute(e.target, "data-settings-page")) {
                open_settings();
                if (m_settings) m_settings->show(page->attribute_or("data-settings-page", "editor"));
                return;
            }
            const grove::Node* item = with_attribute(e.target, "data-path");
            if (!item) return;
            const std::size_t line = static_cast<std::size_t>(grove::parse_integer(item->attribute_or("data-line", "1")).value_or(1));
            const std::size_t column = static_cast<std::size_t>(grove::parse_integer(item->attribute_or("data-column", "1")).value_or(1));
            open_file(item->attribute_or("data-path", ""), line, column);
        });
        listen_by_id("open-folder", "change", [this](grove::Event& e) {
            if (e.target) open_folder(grove::input_string(*e.target).value_or(""));
        });
        listen_by_id("open-file", "change", [this](grove::Event& e) {
            if (!e.target) return;
            const std::string value = grove::input_string(*e.target).value_or("");
            std::size_t start = 0;
            while (start < value.size()) {
                const std::size_t end = value.find('\n', start);
                open_file(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
        });
        listen_by_id("open-settings", "click", [this](grove::Event&) { open_settings(); });
        listen_by_id("status-version", "click", [this](grove::Event&) { open_developer(); });
        listen_by_id("welcome-settings", "click", [this](grove::Event&) { open_settings(); });
        listen_by_id("build-button", "click", [this](grove::Event&) { build(false); });
        listen_by_id("run-button", "click", [this](grove::Event&) { build(true); });
        listen_by_id("editors", "input", [this](grove::Event& e) { editor_input(e); });
        listen_by_id("editors", "cursor", [this](grove::Event& e) {
            set_text(m_doc, "status-cursor", "Ln " + std::to_string(static_cast<long>(e.x)) + ", Col " + std::to_string(static_cast<long>(e.y)));
            if (const grove::Node* editor = with_attribute(e.target, "data-path")) m_carets[editor->attribute_or("data-path", "")] = { static_cast<std::size_t>(e.x), static_cast<std::size_t>(e.y) };
        });
        listen_by_id("editors", "hover", [this](grove::Event& e) { editor_hover(e); });
        listen_by_id("editors", "complete", [this](grove::Event& e) { editor_complete(e); });
        listen_by_id("editors", "goto", [this](grove::Event& e) { editor_goto(e); });
        listen_by_id("editors", "hover_action", [this](grove::Event& e) { editor_hover_action(e); });
        listen_by_id("editors", "click", [this](grove::Event& e) { viewer_click(e); });
        setup_intel();
        if (grove::Node* body = m_doc.first_by_tag("body")) {
            m_doc.events().add_listener(body, "key_down", [this](grove::Event& e) { key_down(e); });
            m_doc.events().add_listener(body, "mouse_move", [this](grove::Event& e) { drag_move(e); });
            m_doc.events().add_listener(body, "mouse_up", [this](grove::Event& e) { drag_up(e); });
            m_doc.events().add_listener(body, "mouse_move", [this](grove::Event& e) { layout_mouse_move(e); });
            m_doc.events().add_listener(body, "mouse_up", [this](grove::Event& e) { layout_mouse_up(e); });
            m_doc.events().add_listener(body, "drop", [this](grove::Event& e) { files_dropped(e); });
        }
        SettingsPage::Hooks hooks;
        hooks.changed = [this] { reload_settings(); };
        hooks.open_file = [this](const std::string& path) { open_file(path); };
        hooks.project_file = [this] { return walnut_names_target(); };
        hooks.project_names_changed = [this](const std::string&) {
            load_walnut_names(true);
            reload_from_disk();
        };
        m_settings = std::make_unique<SettingsPage>(m_doc, m_ws, m_themes, std::move(hooks));
        load_walnut_names(true);
        setup_git();
        setup_console();
        setup_find();
        m_doc.events().set_interval(100.0, [this] { poll_job(); });
        m_doc.events().set_interval(40.0, [this] { poll_terminals(); });
        m_doc.events().set_interval(1500.0, [this] { watch_explorer(); });
        m_doc.events().set_interval(30.0, [this] { poll_completion(); });
        reset_terminals();
        m_doc.events().add_listener(nullptr, "resize", [this](grove::Event&) { apply_gui(); });
        listen_by_id("theme-picker", "change", [this](grove::Event& e) {
            if (e.target) pick_theme(grove::input_string(*e.target).value_or(""));
        });
        listen_by_id("warnings", "click", [this](grove::Event& e) {
            const grove::Node* item = with_attribute(e.target, "data-path");
            if (!item) return;
            const std::size_t line = static_cast<std::size_t>(grove::parse_integer(item->attribute_or("data-line", "1")).value_or(1));
            const std::size_t column = static_cast<std::size_t>(grove::parse_integer(item->attribute_or("data-column", "1")).value_or(1));
            open_file(item->attribute_or("data-path", ""), line, column);
        });
        apply_gui();
        refresh_theme_picker();
        register_natives();
        build_layout();
    }

    void open_folder(const std::string& path) {
        if (path.empty()) return;
        std::error_code ec;
        if (!fs::is_directory(path, ec)) {
            notice("Can't open " + path + ": it isn't a folder");
            return;
        }
        m_ws = open_workspace(path);
        m_themes = load_themes(m_ws.diagnostics);
        setup_intel();
        m_expanded.clear();
        m_messages.clear();
        m_selected.clear();
        m_anchor.clear();
        m_collapsed_roots.clear();
        remember_folder(m_ws.root);
        load_roots();
        reset_terminals();
        populate_shell(m_project, m_ws);
        for (OpenFile& f : m_files) {
            if (f.editor && !virtual_tab(f.path)) apply_editor_settings(*f.editor);
        }
        refresh_viewers();
        apply_gui();
        refresh_theme_picker();
        if (m_settings) m_settings->refresh();
        if (m_git) m_git->set_root(m_ws.root);
        refresh_all();
    }

    void open_file(const std::string& raw_path, std::size_t line = 0, std::size_t column = 0, bool preview = false, bool take_focus = true) {
        if (raw_path.empty()) return;
        const std::string path = normalized(raw_path);
        if (!m_ws.settings.flag("explorer", "preview-tabs", true)) preview = false;
        OpenFile* file = find_file(path);
        if (file && !preview) pin(*file);
        if (!file) {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) {
                notice("Can't open " + path);
                return;
            }
            bool replaced = false;
            if (preview) replaced = replace_preview_slot();
            const auto chosen = m_open_as.find(path);
            const ViewerKind kind = chosen != m_open_as.end() ? chosen->second : viewer_kind_for(path);
            bool opened = false;
            if (kind == ViewerKind::Image || kind == ViewerKind::Pdf || kind == ViewerKind::Binary) opened = open_viewer(path, kind);
            else opened = open_text(path, kind);
            if (replaced) {
                m_tab_anchor = nullptr;
                m_file_slot = static_cast<std::size_t>(-1);
            }
            if (!opened) return;
            file = find_file(path);
            if (!file) return;
            if (preview) {
                file->preview = true;
                grove::add_class(m_doc, *file->tab, "preview");
            }
        }
        activate(path);
        if (line > 0 && file->editor && !virtual_tab(file->path)) file->editor->set_attribute("caret", std::to_string(line) + ":" + std::to_string(std::max<std::size_t>(1, column)) + "#" + std::to_string(++m_caret_serial));
        grove::Document& doc = m_doc;
        if (take_focus && doc.host().focus && file->editor) doc.host().focus(file->editor);
        doc.touch();
    }

    bool open_text(const std::string& path, ViewerKind kind) {
        const std::optional<std::string> text = read_text_file(path);
        if (!text) {
            notice("Can't open " + path);
            return false;
        }
        grove::Node* editors = m_doc.find_by_id("editors");
        if (!editors || !m_doc.find_by_id("tabs")) return false;
        grove::Node* host = editors;
        grove::Node* view = nullptr;
        if (kind == ViewerKind::Latex || kind == ViewerKind::Markdown || kind == ViewerKind::Math) {
            view = m_doc.create_element("row");
            view->set_attribute("class", kind == ViewerKind::Markdown || kind == ViewerKind::Math ? "tex-split md-split hidden" : "tex-split hidden");
            m_doc.append_child(*editors, *view);
            host = view;
        }
        grove::Node* editor = m_doc.create_element("input_code");
        editor->set_attribute("class", view ? "editor" : "editor hidden");
        editor->set_attribute("language", code_language_name(path));
        editor->set_attribute("data-path", path);
        apply_editor_settings(*editor);
        editor->set_attribute("value", *text);
        if (intel_language(code_language_name(path))) editor->set_attribute("link-words", "true");
        m_doc.append_child(*host, *editor);
        grove::Node* tab = create_tab(path, file_icon(path, false, false), display_name(path));
        add_open_file(OpenFile{ path, editor, tab, *text, false, view, kind });
        if (kind == ViewerKind::Latex) {
            Viewer& v = m_viewers[path];
            v.kind = ViewerKind::Latex;
            v.root = add(m_doc, *view, "column", "", "viewer tex-preview");
            v.root->set_attribute("data-viewer", path);
            v.collapsed = !m_ws.settings.flag("viewer", "latex-preview", true);
            render_viewer(path);
            if (!v.collapsed) start_latex(path);
        }
        if (kind == ViewerKind::Math) {
            Viewer& v = m_viewers[path];
            v.kind = ViewerKind::Math;
            v.root = add(m_doc, *view, "column", "", "viewer tex-preview md-preview math-preview");
            v.root->set_attribute("data-viewer", path);
            v.collapsed = !m_ws.settings.flag("viewer", "math-preview", true);
            render_viewer(path);
        }
        if (kind == ViewerKind::Markdown) {
            Viewer& v = m_viewers[path];
            v.kind = ViewerKind::Markdown;
            v.root = add(m_doc, *view, "column", "", "viewer tex-preview md-preview");
            v.root->set_attribute("data-viewer", path);
            v.collapsed = !m_ws.settings.flag("viewer", "markdown-preview", true);
            render_viewer(path);
        }
        schedule_semantic(path, 0.0);
        return true;
    }

    grove::Node* create_tab(const std::string& path, const std::string& icon, const std::string& label) {
        grove::Node* tabs = m_doc.find_by_id("tabs");
        grove::Node* tab = m_doc.create_element("row");
        tab->set_attribute("class", "tab");
        tab->set_attribute("data-path", path);
        add_icon(m_doc, *tab, icon);
        add(m_doc, *tab, "text", label, "tab-name");
        grove::Node* close = add(m_doc, *tab, "text", "\xC3\x97", "tab-close");
        close->set_attribute("data-close", path);
        if (m_tab_anchor && m_tab_anchor->parent() == tabs) m_doc.insert_before(*tabs, *tab, m_tab_anchor);
        else m_doc.append_child(*tabs, *tab);
        return tab;
    }

    void activate(const std::string& path) {
        m_active = path;
        for (OpenFile& f : m_files) {
            const bool on = f.path == path;
            if (on) {
                grove::remove_class(m_doc, *shown(f), "hidden");
                grove::add_class(m_doc, *f.tab, "active");
            } else {
                grove::add_class(m_doc, *shown(f), "hidden");
                grove::remove_class(m_doc, *f.tab, "active");
            }
        }
        if (grove::Node* welcome = m_doc.find_by_id("welcome")) {
            if (m_files.empty()) grove::remove_class(m_doc, *welcome, "hidden");
            else grove::add_class(m_doc, *welcome, "hidden");
        }
        refresh_explorer();
        refresh_status();
        if (m_git && !virtual_tab(path)) m_git->active_changed(path);
        if (!virtual_tab(path)) load_walnut_names(false);
        if (m_find) m_find->editor_changed();
    }

    std::string walnut_names_from() const {
        if (!m_active.empty() && !virtual_tab(m_active)) return m_active;
        return m_ws.root;
    }

    std::string walnut_names_target() const {
        std::string file = find_project_file(walnut_names_from());
        if (file.empty() && !m_ws.root.empty()) file = find_project_file(m_ws.root);
        if (file.empty() && !m_ws.root.empty()) file = (fs::path(m_ws.root) / project_file_name()).string();
        return file;
    }

    void load_walnut_names(bool force) {
        const std::string file = find_project_file(walnut_names_from());
        std::error_code ec;
        const auto stamp = file.empty() ? fs::file_time_type{} : fs::last_write_time(file, ec);
        if (!force && file == m_names_file && stamp == m_names_time) return;
        m_names_file = file;
        m_names_time = stamp;
        m_names = file.empty() ? ProjectNames{} : read_project_names(file);
        if (file.empty()) clear_project_names();
        else use_project_names(m_names);
        refresh_problems();
        for (const OpenFile& f : m_files) {
            if (!f.editor || virtual_tab(f.path)) continue;
            const Language* lang = language_for_path(f.path);
            if (lang && lang->id == LanguageId::Walnut) schedule_semantic(f.path, 0.0);
        }
    }

    void close_file(const std::string& path) {
        auto it = std::find_if(m_files.begin(), m_files.end(), [&](const OpenFile& f) { return f.path == path; });
        if (it == m_files.end()) return;
        if (m_find) m_find->forget(it->editor);
        if (path == k_settings_tab && m_settings) m_settings->forget();
        if (path == k_developer_tab) {
            m_developer.stop();
            if (m_developer_timer) m_doc.events().clear_timer(m_developer_timer);
            m_developer_timer = 0;
        }
        if (path != k_settings_tab && virtual_tab(path)) virtual_titles().erase(path);
        m_semantic_due.erase(path);
        m_markdown_due.erase(path);
        m_web_due.erase(path);
        m_web_lint.erase(path);
        m_web_syntax.erase(path);
        m_web_problems.erase(path);
        if (m_live.erase(path)) refresh_problems();
        m_walnut.erase(path);
        m_marks.erase(path);
        forget_viewer(path);
        m_doc.remove(*shown(*it));
        m_doc.remove(*it->tab);
        const bool was_active = m_active == path;
        const std::size_t index = static_cast<std::size_t>(it - m_files.begin());
        m_files.erase(it);
        if (was_active) {
            m_active.clear();
            if (!m_files.empty()) m_active = m_files[std::min(index, m_files.size() - 1)].path;
        }
        activate(m_active);
    }

    bool save(const std::string& path) {
        if (virtual_tab(path)) return true;
        OpenFile* f = find_file(path);
        if (!f) return false;
        if (!f->editor) return true;
        const std::string text = grove::input_string(*f->editor).value_or("");
        const std::string before = f->saved;
        if (!write_text_file(f->path, text)) {
            notice("Couldn't save " + f->path);
            return false;
        }
        record_history(f->path, before, text);
        f->saved = text;
        set_dirty(*f, false);
        for (const auto& s : m_servers) s->changed(f->path);
        if (is_settings_file(f->path)) reload_settings();
        restart_node_apps(f->path);
        if (fs::path(f->path).filename() == project_file_name()) {
            load_walnut_names(true);
            if (m_settings && m_settings->page() == "characters" && m_settings->root()) m_settings->rebuild();
        }
        latex_saved(f->path);
        if (m_git) m_git->file_saved(f->path);
        return true;
    }

    bool save_active() { return !m_active.empty() && save(m_active); }

    void build(bool run) {
        if (m_job.running()) {
            notice("A build is already running");
            return;
        }
        if (m_git && !m_ws.root.empty()) {
            m_git->reload_tasks();
            const TaskFile& tasks = m_git->tasks();
            const Task* instead = run ? tasks.hooked_run(TaskHook::Instead) : tasks.hooked_build(TaskHook::Instead);
            const Task* before = run ? tasks.hooked_run(TaskHook::Before) : tasks.hooked_build(TaskHook::Before);
            const Task* after = run ? tasks.hooked_run(TaskHook::After) : tasks.hooked_build(TaskHook::After);
            if (instead) {
                show_panel("output");
                m_git->run_task(instead->id);
                return;
            }
            const std::string after_id = after ? after->id : std::string();
            if (before) {
                show_panel("output");
                m_git->run_task(before->id, [this, run, after_id](bool ok) {
                    if (ok) start_build(run, after_id);
                });
                return;
            }
            start_build(run, after_id);
            return;
        }
        start_build(run, std::string());
    }

    void start_build(bool run, const std::string& after_task) {
        m_after_task.clear();
        if (m_job.running()) {
            notice("A build is already running");
            return;
        }
        if (m_active.empty() || virtual_tab(m_active)) {
            notice("Open a file to build it");
            show_panel("output");
            return;
        }
        if (const OpenFile* active = find_file(m_active); active && active->kind == ViewerKind::Latex) {
            if (m_ws.settings.flag("build", "save-before-build", true)) save_active();
            Viewer& v = m_viewers[m_active];
            if (v.collapsed) set_collapsed(m_active, false);
            start_latex(m_active);
            return;
        }
        if (const OpenFile* active = find_file(m_active); active && !active->editor) {
            notice(display_name(m_active) + " isn't source code, so there's nothing to build");
            show_panel("output");
            return;
        }
        if (const Language* lang = language_for_path(m_active); lang && lang->build == BuildKind::Web) {
            if (m_ws.settings.flag("build", "save-before-build", true)) save_active();
            serve_path(m_active);
            show_panel("console");
            return;
        }
        if (const Language* lang = language_for_path(m_active); lang && lang->build == BuildKind::Node && run) {
            if (m_ws.settings.flag("build", "save-before-build", true)) save_active();
            run_node(m_active);
            return;
        }
        if (m_ws.settings.flag("build", "save-before-build", true)) save_active();
        BuildRequest request;
        request.file = m_active;
        request.workspace_root = m_ws.root;
        const BuildPlan plan = plan_build(request, m_ws.settings, m_ws.toolchain);
        m_output.clear();
        m_messages.clear();
        if (!plan.ok()) {
            m_output = plan.error + "\n";
            sync_output();
            show_panel("output");
            refresh_problems();
            return;
        }
        std::error_code ec;
        if (!plan.output.empty()) fs::create_directories(fs::path(plan.output).parent_path(), ec);
        std::vector<std::vector<std::string>> steps{ plan.command };
        for (const auto& step : plan.more_steps) steps.push_back(step);
        if (run && !plan.run.empty()) steps.push_back(plan.run);
        else if (run && plan.run_error.empty()) steps.push_back({ fs::absolute(plan.output, ec).string() });
        m_build_folder = plan.folder;
        m_running_label = std::string(run ? "Running " : "Building ") + display_name(m_active);
        m_output = m_running_label + "\xE2\x80\xA6\n";
        if (!plan.note.empty()) m_output += plan.note + "\n";
        if (run && !plan.run_error.empty()) m_output += plan.run_error + "\n";
        sync_output();
        set_text(m_doc, "status-problems", m_running_label + "\xE2\x80\xA6");
        show_panel("output");
        refresh_problems();
        m_after_task = after_task;
        m_job_steps = steps.size();
        m_job_program = run && (!plan.run.empty() || plan.run_error.empty());
        m_job_file = m_active;
        m_job.start(std::move(steps), plan.folder);
    }

    static std::size_t output_chunk_end(const std::string& text, std::size_t from) {
        constexpr std::size_t chunk = 8192;
        if (text.size() - from <= chunk) return text.size();
        const std::size_t nl = text.find('\n', from + chunk);
        return nl == std::string::npos ? text.size() : nl + 1;
    }

    void trim_output() {
        grove::Node* log = m_doc.find_by_id("output-log");
        while (m_output.size() > 400000) {
            const std::size_t end = output_chunk_end(m_output, 0);
            if (end >= m_output.size()) {
                m_output.erase(0, m_output.size() - 400000);
                if (log) clear(m_doc, *log);
                return;
            }
            m_output.erase(0, end);
            if (log && !log->children().empty()) m_doc.remove(*log->children().front());
        }
    }

    void sync_output() {
        grove::Node* log = m_doc.find_by_id("output-log");
        if (!log) return;
        std::size_t pos = 0;
        std::size_t index = 0;
        std::string piece;
        while (pos < m_output.size()) {
            const std::size_t end = output_chunk_end(m_output, pos);
            piece.assign(m_output, pos, end - pos);
            if (end < m_output.size() && !piece.empty() && piece.back() == '\n') piece.pop_back();
            const auto& kids = log->children();
            if (index < kids.size() && kids[index]->is_element() && kids[index]->has_class("out-chunk")) {
                set_text(m_doc, *kids[index], piece);
            } else {
                grove::Node* chunk = m_doc.create_element("pre");
                chunk->set_attribute("class", "out-chunk");
                chunk->set_inner_text(piece);
                m_doc.insert_before(*log, *chunk, index < kids.size() ? kids[index].get() : nullptr);
            }
            pos = end;
            ++index;
        }
        while (log->children().size() > index) {
            if (!m_doc.remove(*log->children().back())) break;
        }
        m_doc.touch();
    }

    void panel_menu(const std::string& view, grove::Event& e) {
        grove::Node* owner = m_doc.find_by_id(view);
        if (!owner) return;
        m_panel_menu_view = view;
        m_panel_menu_item.clear();
        if (const grove::Node* item = with_attribute(e.target, "data-path")) m_panel_menu_item = item->inner_text();
        const bool selected = !grove::selected_text(m_doc).empty();
        std::vector<grove::MenuItem> items;
        items.push_back(grove::menu_item("copy", "Copy", "Ctrl+C", !selected));
        if (!m_panel_menu_item.empty()) items.push_back(grove::menu_item("copy-item", view == "view-output" ? "Copy Line" : "Copy Problem"));
        items.push_back(grove::menu_item("copy-all", "Copy All"));
        items.push_back(grove::menu_item("select-all", "Select All"));
        if (view == "view-output") {
            items.push_back(grove::menu_separator());
            items.push_back(grove::menu_item("clear", "Clear Output"));
        }
        grove::open_menu(m_doc, *owner, items, e.x, e.y);
        e.default_prevented = true;
    }

    std::string panel_text(const std::string& view) const {
        const grove::Node* node = m_doc.find_by_id(view);
        if (!node) return std::string();
        if (view == "view-output") return m_output;
        std::string out;
        for (const auto& child : node->children()) {
            if (!child->is_element()) continue;
            for (const auto& p : child->children()) {
                if (!p->is_element()) continue;
                out += p->inner_text();
                out += '\n';
            }
        }
        return out;
    }

    void panel_menu_action(const std::string& action) {
        const std::string view = m_panel_menu_view;
        std::string text;
        if (action == "copy") text = grove::selected_text(m_doc);
        else if (action == "copy-item") text = m_panel_menu_item;
        else if (action == "copy-all") text = panel_text(view);
        else if (action == "select-all") {
            if (grove::Node* node = m_doc.find_by_id(view)) grove::select_text(m_doc, *node);
            return;
        } else if (action == "clear") {
            m_output.clear();
            sync_output();
            return;
        }
        if (!text.empty() && grove::set_clipboard_text(m_doc, text)) set_text(m_doc, "status-problems", "Copied " + std::to_string(text.size()) + " characters");
    }

    void show_panel(const std::string& name) {
        activate_view(name, true);
    }

    void activate_view(const std::string& name, bool focus) {
        if (std::find(layout_views().begin(), layout_views().end(), name) == layout_views().end()) return;
        const std::string area = view_area(name);
        m_area_active[area] = name;
        for (const std::string& v : layout_views()) {
            if (view_area(v) != area) continue;
            const bool on = v == name;
            if (grove::Node* tab = m_doc.find_by_id("tab-" + v)) on ? (void)grove::add_class(m_doc, *tab, "active") : (void)grove::remove_class(m_doc, *tab, "active");
            if (grove::Node* view = m_doc.find_by_id("view-" + v)) on ? (void)grove::add_class(m_doc, *view, "active") : (void)grove::remove_class(m_doc, *view, "active");
        }
        if (name == "console" && focus && m_console) m_console->focus_input();
        if (name == "terminal") {
            if (m_terms.empty()) add_terminal(m_roots.empty() ? std::string() : m_roots.front());
            if (TerminalSession* t = active_terminal()) start_terminal(*t);
            refresh_terminal_ui(true);
            if (focus) focus_terminal();
        }
        if (m_git) {
            m_git->set_visible(area_showing("git"), area_showing("history"));
            if (focus && (name == "git" || name == "history")) m_git->panel_shown(name);
        }
        m_doc.touch();
    }

private:
    struct OpenFile {
        std::string path;
        grove::Node* editor = nullptr;
        grove::Node* tab = nullptr;
        std::string saved;
        bool dirty = false;
        grove::Node* view = nullptr;
        ViewerKind kind = ViewerKind::Text;
        bool preview = false;
    };

    enum class Clash {
        Replace,
        KeepBoth,
        Skip
    };

    struct Drag {
        std::string path;
        double x = 0;
        double y = 0;
        bool active = false;
        bool started = false;
        bool copy = false;
        bool hinted = false;
        std::string target;
    };

    struct TerminalSession {
        std::string id;
        std::string folder;
        std::string name;
        std::unique_ptr<TerminalProcess> process;
        grove::Node* view = nullptr;
        std::shared_ptr<grove::Terminal> term;
        int cols = 0;
        int rows = 0;
        bool started = false;
        bool exited = false;
        std::string node_file;
        std::string node_scan;
        std::uint16_t node_port = 0;
        bool node_open_browser = false;
        LiveServer* node_proxy = nullptr;
    };

    static grove::Node* shown(const OpenFile& f) { return f.view ? f.view : f.editor; }

    void pin(OpenFile& f) {
        if (!f.preview) return;
        f.preview = false;
        if (f.tab) grove::remove_class(m_doc, *f.tab, "preview");
        m_doc.touch();
    }

    bool replace_preview_slot() {
        const auto it = std::find_if(m_files.begin(), m_files.end(), [](const OpenFile& f) { return f.preview && !f.dirty; });
        if (it == m_files.end()) return false;
        const std::string old = it->path;
        m_file_slot = static_cast<std::size_t>(it - m_files.begin());
        m_tab_anchor = nullptr;
        if (grove::Node* tabs = m_doc.find_by_id("tabs")) {
            const auto& kids = tabs->children();
            for (std::size_t i = 0; i < kids.size(); ++i) {
                if (kids[i].get() == it->tab && i + 1 < kids.size()) m_tab_anchor = kids[i + 1].get();
            }
        }
        close_file(old);
        return true;
    }

    void add_open_file(OpenFile file) {
        if (m_file_slot < m_files.size()) m_files.insert(m_files.begin() + static_cast<std::ptrdiff_t>(m_file_slot), std::move(file));
        else m_files.push_back(std::move(file));
    }

    void listen_by_id(const std::string& id, const char* type, grove::Listener fn) {
        if (grove::Node* n = m_doc.find_by_id(id)) m_doc.events().add_listener(n, type, std::move(fn));
    }

    OpenFile* find_file(const std::string& path) {
        for (OpenFile& f : m_files) {
            if (f.path == path) return &f;
        }
        return nullptr;
    }

    void set_dirty(OpenFile& f, bool dirty) {
        if (f.dirty == dirty) return;
        f.dirty = dirty;
        for (const auto& child : f.tab->children()) {
            if (child->is_element() && child->has_class("tab-name")) child->set_inner_text((dirty ? "\xE2\x97\x8F " : "") + display_name(f.path));
        }
        m_doc.touch();
    }

    void editor_input(grove::Event& e) {
        const grove::Node* editor = with_attribute(e.target, "data-path");
        if (!editor) return;
        OpenFile* f = find_file(editor->attribute_or("data-path", ""));
        if (!f) return;
        set_dirty(*f, grove::input_string(*f->editor).value_or("") != f->saved);
        if (f->dirty) pin(*f);
        schedule_semantic(f->path, 0.35);
        if (f->kind == ViewerKind::Markdown || f->kind == ViewerKind::Math) m_markdown_due[f->path] = now_seconds() + 0.15;
    }

    const std::vector<ShortcutBinding>& bindings() {
        if (m_bindings_revision != m_ws.settings.revision()) {
            m_bindings = shortcut_bindings(m_ws.settings);
            m_bindings_revision = m_ws.settings.revision();
        }
        return m_bindings;
    }

    void key_down(grove::Event& e) {
        if (m_settings && m_settings->record_key(e)) {
            e.default_prevented = true;
            return;
        }
        if (m_drag.active && e.key == "Escape") {
            const bool started = m_drag.started;
            m_drag = Drag{};
            hide_drag_preview();
            highlight_drop(std::string());
            if (started) {
                refresh_status();
                refresh_problems();
            }
            e.default_prevented = true;
            return;
        }
        if (e.key == "Escape" && m_find && m_find->is_open() && e.target && e.target->tag() == "input_code") {
            m_find->close();
            e.default_prevented = true;
            return;
        }
        const std::optional<std::string> command = command_for(bindings(), grove::chord_of(e.key, e.modifiers));
        if (!command) return;
        if (e.target && e.target->tag() == "terminal") {
            static const std::set<std::string> passthrough = { "show-terminal", "new-terminal", "show-problems", "show-warnings", "show-output", "open-settings", "open-developer", "show-git", "show-history" };
            if (!passthrough.count(*command)) return;
        }
        if (!run_command(*command)) return;
        e.default_prevented = true;
    }

    bool run_command(const std::string& command) {
        if (command == "save") save_active();
        else if (command == "build") build(false);
        else if (command == "run") build(true);
        else if (command == "close-tab") close_file(m_active);
        else if (command == "show-problems") show_panel("problems");
        else if (command == "show-warnings") show_panel("warnings");
        else if (command == "show-output") show_panel("output");
        else if (command == "open-settings") open_settings();
        else if (command == "open-developer") open_developer();
        else if (command == "definition") goto_caret();
        else if (command == "go-back") go_back();
        else if (command == "go-forward") go_forward();
        else if (command == "show-terminal") show_panel("terminal");
        else if (command == "new-terminal") {
            const TerminalSession* t = active_terminal();
            new_terminal(t ? t->folder : m_ws.root, true);
        } else if (command == "save-all") save_all();
        else if (command == "show-git") show_panel("git");
        else if (command == "show-history") show_panel("history");
        else if (command == "git-commit" && m_git) m_git->commit(false);
        else if (command == "git-commit-push" && m_git) m_git->commit(true);
        else if (command == "git-push" && m_git) m_git->push();
        else if (command == "git-pull" && m_git) m_git->pull();
        else if (command == "run-task" && m_git) m_git->pick_task();
        else if (command == "find" && m_find) m_find->open(false);
        else if (command == "replace" && m_find) m_find->open(true);
        else if (command == "find-next" && m_find) m_find->next();
        else if (command == "find-previous" && m_find) m_find->previous();
        else if (command == "select-all-matches" && m_find && m_find->is_open()) m_find->select_all();
        else return false;
        return true;
    }

    void tab_click(grove::Event& e) {
        if (const grove::Node* close = with_attribute(e.target, "data-close")) {
            const std::string path = close->attribute_or("data-close", "");
            OpenFile* f = find_file(path);
            if (f && f->dirty) {
                grove::PopupOptions options;
                options.kind = "question";
                options.title = "Unsaved changes";
                options.message = display_name(path) + " has unsaved changes. Close it and lose them?";
                if (grove::Node* popup = grove::create_popup(m_doc, nullptr, options)) {
                    m_doc.events().add_listener(popup, "ok", [this, path](grove::Event&) { close_file(path); });
                }
                return;
            }
            close_file(path);
            return;
        }
        if (const grove::Node* tab = with_attribute(e.target, "data-path")) activate(tab->attribute_or("data-path", ""));
    }

    void load_roots() {
        m_roots.clear();
        if (m_ws.root.empty()) return;
        m_roots.push_back(m_ws.root);
        for (const std::string& extra : saved_extra_roots(m_ws.root)) {
            if (!is_root(extra)) m_roots.push_back(extra);
        }
    }

    bool multi_root() const { return m_roots.size() > 1; }

    bool is_root(const std::string& path) const {
        const std::string p = normalized(path);
        for (const std::string& r : m_roots) {
            if (normalized(r) == p) return true;
        }
        return false;
    }

    std::string root_of(const std::string& path) const {
        const std::string p = normalized(path);
        std::string best;
        for (const std::string& r : m_roots) {
            const std::string n = normalized(r);
            if (path_inside(p, n) && n.size() > best.size()) best = n;
        }
        return best;
    }

    static std::string folder_label(const std::string& raw) {
        const std::string folder = clean_folder_path(raw);
        const std::string name = fs::path(folder).filename().string();
        return name.empty() ? folder : name;
    }

    void add_folder(const std::string& raw) {
        if (raw.empty()) return;
        std::error_code ec;
        if (!fs::is_directory(raw, ec)) {
            notice("Can't add " + raw + ": it isn't a folder");
            return;
        }
        if (m_ws.root.empty()) {
            open_folder(raw);
            return;
        }
        const std::string folder = normalized(raw);
        if (is_root(folder)) {
            set_text(m_doc, "status-problems", folder_label(folder) + " is already in the workspace");
            return;
        }
        m_roots.push_back(folder);
        save_extra_roots(m_ws.root, std::vector<std::string>(m_roots.begin() + 1, m_roots.end()));
        new_terminal(folder, false);
        refresh_explorer();
        refresh_status();
        refresh_ports(true);
    }

    void remove_root(const std::string& folder) {
        const std::string n = normalized(folder);
        if (m_roots.empty()) return;
        if (normalized(m_roots.front()) == n) {
            if (m_roots.size() < 2) {
                notice(folder_label(folder) + " is the only folder in this workspace; open another folder to replace it");
                return;
            }
            const std::string next = clean_folder_path(m_roots[1]);
            const std::vector<std::string> rest(m_roots.begin() + 2, m_roots.end());
            save_extra_roots(m_ws.root, {});
            save_extra_roots(next, rest);
            open_folder(next);
            return;
        }
        m_roots.erase(std::remove_if(m_roots.begin(), m_roots.end(), [&](const std::string& r) { return normalized(r) == n; }), m_roots.end());
        save_extra_roots(m_ws.root, std::vector<std::string>(m_roots.begin() + 1, m_roots.end()));
        for (std::size_t i = m_terms.size(); i-- > 0;) {
            if (normalized(m_terms[i]->folder) == n) {
                if (m_terms[i]->process) m_terms[i]->process->stop();
                m_terms.erase(m_terms.begin() + static_cast<std::ptrdiff_t>(i));
            }
        }
        if (m_term >= m_terms.size()) m_term = m_terms.empty() ? 0 : m_terms.size() - 1;
        refresh_terminal_ui(true);
        refresh_explorer();
        refresh_status();
        refresh_ports(true);
    }

    grove::Node* explorer_item(grove::Node& list, const std::string& path, const std::string& name, bool directory, bool open, int depth, const std::string& extra) {
        std::string classes = directory ? "entry folder" : "entry file";
        if (!extra.empty()) classes += " " + extra;
        if (!directory && path == m_active) classes += " open";
        if (m_selected.count(path)) classes += " selected";
        if (m_clip_cut && std::find(m_clip.begin(), m_clip.end(), path) != m_clip.end()) classes += " cut";
        if (m_git) {
            const std::string deco = m_git->decoration(path, directory);
            if (!deco.empty()) classes += " git-" + deco;
        }
        if (std::getenv("CANOPY_DECO_DEBUG") && name == "proj") std::fprintf(stderr, "%.2f item classes=[%s]\n", now_seconds(), classes.c_str());
        const std::string icon = file_icon(path, directory, open);
        const int indent = 8 + depth * 14 + (directory ? 0 : 14);
        const std::string style = "padding: 2px 8px 2px " + std::to_string(indent) + "px";
        grove::Node* item = reuse_explorer_item(list, path, directory, !icon.empty());
        if (item) {
            item->set_attribute("class", classes);
            std::size_t at = 0;
            const auto& kids = item->children();
            if (directory) set_text(m_doc, *kids[at++], open ? "\xE2\x96\xBE" : "\xE2\x96\xB8");
            if (!icon.empty()) kids[at++]->set_attribute("src", icon);
            set_text(m_doc, *kids[at], name);
        } else {
            item = m_doc.create_element("item");
            item->set_attribute("class", classes);
            if (directory) add(m_doc, *item, "text", open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", "twisty");
            add_icon(m_doc, *item, icon);
            add(m_doc, *item, "text", name, "entry-name");
            const auto& kids = list.children();
            m_doc.insert_before(list, *item, m_explorer_at < kids.size() ? kids[m_explorer_at].get() : nullptr);
        }
        ++m_explorer_at;
        item->set_attribute("data-path", path);
        item->set_attribute("style", style);
        m_visible.push_back(path);
        m_items[path] = item;
        return item;
    }

    grove::Node* reuse_explorer_item(grove::Node& list, const std::string& path, bool directory, bool icon) {
        const auto old = m_explorer_old.find(path);
        if (old == m_explorer_old.end()) return nullptr;
        grove::Node* item = old->second;
        m_explorer_old.erase(old);
        if (item->parent() != &list || !explorer_shape(*item, directory, icon)) return nullptr;
        const auto& kids = list.children();
        if (m_explorer_at >= kids.size() || kids[m_explorer_at].get() != item) {
            m_doc.insert_before(list, *item, m_explorer_at < kids.size() ? kids[m_explorer_at].get() : nullptr);
        }
        return item;
    }

    static bool explorer_shape(const grove::Node& item, bool directory, bool icon) {
        if (!item.is_element() || item.tag() != "item") return false;
        const auto& parts = item.children();
        const std::size_t want = (directory ? 1u : 0u) + (icon ? 1u : 0u) + 1u;
        if (parts.size() != want) return false;
        std::size_t at = 0;
        if (directory != parts[0]->has_class("twisty")) return false;
        if (directory) ++at;
        if (icon && parts[at++]->tag() != "img") return false;
        return parts[at]->has_class("entry-name");
    }

    void add_entries(grove::Node& list, const std::string& folder, int depth, std::size_t& budget) {
        const std::vector<WorkspaceEntry> entries = m_ws.list(folder);
        remember_listing(folder, entries);
        for (const WorkspaceEntry& entry : entries) {
            if (budget == 0) return;
            --budget;
            const bool open = entry.directory && m_expanded.count(entry.path);
            const std::string path = entry.directory ? entry.path : normalized(entry.path);
            explorer_item(list, path, entry.name, entry.directory, open, depth, std::string());
            if (open) add_entries(list, entry.path, depth + 1, budget);
        }
    }

    std::string explorer_signature() const {
        std::string sig;
        const std::vector<std::string> folders = explorer_folders();
        std::size_t budget = 5000;
        for (const std::string& folder : folders) {
            sig += folder;
            sig += '\n';
            for (const WorkspaceEntry& entry : m_ws.list(folder)) {
                if (budget == 0) return sig;
                --budget;
                sig += entry.directory ? "d " : "f ";
                sig += entry.name;
                sig += '\n';
            }
        }
        return sig;
    }

    std::vector<std::string> explorer_folders() const {
        std::vector<std::string> folders;
        if (multi_root()) {
            for (const std::string& root : m_roots) {
                if (!m_collapsed_roots.count(root)) folders.push_back(root);
            }
        } else if (!m_ws.root.empty()) {
            folders.push_back(m_ws.root);
        }
        for (const std::string& e : m_expanded) folders.push_back(e);
        return folders;
    }

    static std::string folder_signature(const std::vector<WorkspaceEntry>& entries) {
        std::string sig;
        for (const WorkspaceEntry& entry : entries) {
            sig += entry.directory ? "d " : "f ";
            sig += entry.name;
            sig += '\n';
        }
        return sig;
    }

    static fs::file_time_type folder_time(const std::string& folder) {
        std::error_code ec;
        const fs::file_time_type t = fs::last_write_time(folder, ec);
        return ec ? fs::file_time_type{} : t;
    }

    void remember_listing(const std::string& folder, const std::vector<WorkspaceEntry>& entries) {
        m_explorer_lists[folder] = folder_signature(entries);
        m_explorer_times[folder] = folder_time(folder);
    }

    bool explorer_times_changed() {
        bool changed = false;
        const auto now = fs::file_time_type::clock::now();
        for (const std::string& folder : explorer_folders()) {
            const fs::file_time_type t = folder_time(folder);
            const auto known = m_explorer_times.find(folder);
            const bool recent = now - t < std::chrono::seconds(3);
            if (known != m_explorer_times.end() && known->second == t && !recent) continue;
            m_explorer_times[folder] = t;
            const std::string sig = folder_signature(m_ws.list(folder));
            const auto listed = m_explorer_lists.find(folder);
            if (listed == m_explorer_lists.end()) {
                m_explorer_lists[folder] = sig;
                continue;
            }
            if (listed->second == sig) continue;
            listed->second = sig;
            changed = true;
        }
        return changed;
    }

    void watch_explorer() {
        load_walnut_names(false);
        if (m_drag.active || m_ws.root.empty()) return;
        if (explorer_times_changed()) refresh_explorer();
    }

    void refresh_explorer() {
        grove::Node* explorer = m_doc.find_by_id("explorer");
        if (!explorer) return;
        m_explorer_lists.clear();
        m_visible.clear();
        m_explorer_old = std::move(m_items);
        m_items.clear();
        m_drop_node = nullptr;
        m_explorer_at = 0;
        std::size_t budget = 3000;
        if (multi_root()) {
            for (const std::string& root : m_roots) {
                const bool open = !m_collapsed_roots.count(root);
                explorer_item(*explorer, root, folder_label(root), true, open, 0, "root");
                if (open) add_entries(*explorer, root, 1, budget);
            }
        } else if (!m_ws.root.empty()) {
            add_entries(*explorer, m_ws.root, 0, budget);
        }
        while (explorer->children().size() > m_explorer_at) {
            if (!m_doc.remove(*explorer->children().back())) break;
        }
        m_explorer_old.clear();
        m_doc.touch();
    }

    void select_range(const std::string& from, const std::string& to) {
        const auto a = std::find(m_visible.begin(), m_visible.end(), from);
        const auto b = std::find(m_visible.begin(), m_visible.end(), to);
        if (a == m_visible.end() || b == m_visible.end()) {
            m_selected = { to };
            return;
        }
        m_selected.clear();
        for (auto it = std::min(a, b); it <= std::max(a, b); ++it) m_selected.insert(*it);
    }

    std::vector<std::string> selection() const {
        std::vector<std::string> out;
        for (const std::string& p : m_visible) {
            if (m_selected.count(p)) out.push_back(p);
        }
        for (const std::string& p : m_selected) {
            if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
        }
        return out;
    }

    void explorer_click(grove::Event& e) {
        if (now_seconds() < m_suppress_click_until) return;
        const grove::Node* item = with_attribute(e.target, "data-path");
        const bool ctrl = (e.modifiers & (grove::ModifierControl | grove::ModifierMeta)) != 0;
        const bool shift = (e.modifiers & grove::ModifierShift) != 0;
        if (!item) {
            if (!ctrl && !shift && !m_selected.empty()) {
                m_selected.clear();
                refresh_explorer();
            }
            return;
        }
        const std::string path = item->attribute_or("data-path", "");
        if (ctrl) {
            if (m_selected.count(path)) m_selected.erase(path);
            else m_selected.insert(path);
            m_anchor = path;
            refresh_explorer();
            return;
        }
        if (shift && !m_anchor.empty()) {
            select_range(m_anchor, path);
            refresh_explorer();
            return;
        }
        m_selected = { path };
        m_anchor = path;
        if (item->has_class("root")) {
            if (m_collapsed_roots.count(path)) m_collapsed_roots.erase(path);
            else m_collapsed_roots.insert(path);
        } else if (item->has_class("folder")) {
            if (m_expanded.count(path)) m_expanded.erase(path);
            else m_expanded.insert(path);
        } else {
            open_file(path, 0, 0, true, false);
            return;
        }
        refresh_explorer();
    }

    void explorer_double_click(grove::Event& e) {
        m_drag = Drag{};
        const grove::Node* item = with_attribute(e.target, "data-path");
        if (!item || item->has_class("folder")) return;
        open_file(item->attribute_or("data-path", ""), 0, 0, false);
    }

    void explorer_mouse_down(grove::Event& e) {
        const grove::Node* item = with_attribute(e.target, "data-path");
        if (e.button == 2) {
            if (item) {
                const std::string path = item->attribute_or("data-path", "");
                if (!m_selected.count(path)) {
                    m_selected = { path };
                    m_anchor = path;
                    refresh_explorer();
                }
                show_explorer_menu(e.x, e.y, selection());
            } else {
                show_explorer_menu(e.x, e.y, {});
            }
            e.default_prevented = true;
            return;
        }
        if (e.button == 1 && item && !item->has_class("root")) {
            m_drag = Drag{};
            m_drag.path = item->attribute_or("data-path", "");
            m_drag.x = e.x;
            m_drag.y = e.y;
            m_drag.active = true;
        }
    }

    bool inside_explorer(const grove::Node* node) const {
        const grove::Node* explorer = m_doc.find_by_id("explorer");
        for (const grove::Node* n = node; n; n = n->parent()) {
            if (n == explorer) return true;
        }
        return false;
    }

    std::string folder_for_item(const grove::Node* target) const {
        if (!inside_explorer(target)) return std::string();
        const grove::Node* item = with_attribute(target, "data-path");
        if (!item) return m_ws.root;
        const std::string path = item->attribute_or("data-path", "");
        if (item->has_class("folder")) return path;
        return fs::path(path).parent_path().string();
    }

    void highlight_drop(const std::string& folder) {
        if (m_drop_node) grove::remove_class(m_doc, *m_drop_node, "drop-target");
        m_drop_node = nullptr;
        if (!folder.empty()) {
            auto it = m_items.find(folder);
            if (it == m_items.end()) {
                for (auto& [path, node] : m_items) {
                    if (node->has_class("folder") && normalized(path) == normalized(folder)) it = m_items.find(path);
                }
            }
            if (it != m_items.end()) {
                m_drop_node = it->second;
                grove::add_class(m_doc, *m_drop_node, "drop-target");
            }
        }
        m_doc.touch();
    }

    void drag_move(grove::Event& e) {
        if (!m_drag.active) return;
        if (!m_drag.started) {
            if (std::hypot(e.x - m_drag.x, e.y - m_drag.y) < 6.0) return;
            m_drag.started = true;
            if (!m_selected.count(m_drag.path)) {
                m_selected = { m_drag.path };
                m_anchor = m_drag.path;
                refresh_explorer();
            }
            set_text(m_doc, "status-problems", "Drop on a folder to move it there; hold Ctrl to copy");
            show_drag_preview();
        }
        const std::string folder = folder_for_item(e.target);
        const bool copy = (e.modifiers & (grove::ModifierControl | grove::ModifierMeta)) != 0;
        if (folder == m_drag.target && copy == m_drag.copy && m_drag.hinted) return;
        m_drag.copy = copy;
        m_drag.hinted = true;
        update_drag_hint(folder);
        if (folder == m_drag.target) return;
        m_drag.target = folder;
        highlight_drop(folder);
    }

    void show_drag_preview() {
        grove::Node* preview = m_doc.find_by_id("drag-preview");
        if (!preview) return;
        clear(m_doc, *preview);
        const std::vector<std::string> paths = m_selected.count(m_drag.path) ? selection() : std::vector<std::string>{ m_drag.path };
        const std::size_t shown = std::min<std::size_t>(paths.size(), 8);
        for (std::size_t i = 0; i < shown; ++i) {
            std::error_code ec;
            const bool directory = fs::is_directory(paths[i], ec);
            grove::Node* row = add(m_doc, *preview, "row", "", "drag-row");
            add_icon(m_doc, *row, file_icon(paths[i], directory, false));
            add(m_doc, *row, "text", folder_label(paths[i]), "drag-name");
        }
        if (paths.size() > shown) add(m_doc, *preview, "div", "and " + std::to_string(paths.size() - shown) + " more", "drag-more");
        add(m_doc, *preview, "div", "", "drag-hint")->set_attribute("id", "drag-hint");
        grove::remove_class(m_doc, *preview, "hidden");
        m_doc.touch();
    }

    void update_drag_hint(const std::string& folder) {
        const std::string text = folder.empty() ? "Drop on a folder" : std::string(m_drag.copy ? "Copy to " : "Move to ") + folder_label(folder);
        set_text(m_doc, "drag-hint", text);
    }

    void files_dropped(grove::Event& e) {
        std::vector<std::string> paths;
        for (const std::string& p : e.paths) {
            std::error_code ec;
            if (!p.empty() && fs::exists(p, ec)) paths.push_back(fs::path(p).lexically_normal().make_preferred().string());
        }
        if (paths.empty()) return;
        e.default_prevented = true;
        const std::string folder = m_ws.root.empty() ? std::string() : folder_for_item(e.target);
        if (!folder.empty()) {
            transfer(paths, folder, true, true);
            return;
        }
        for (const std::string& p : paths) {
            std::error_code ec;
            if (fs::is_directory(p, ec)) add_folder(p);
            else open_file(p);
        }
    }

    void hide_drag_preview() {
        grove::Node* preview = m_doc.find_by_id("drag-preview");
        if (!preview || preview->has_class("hidden")) return;
        grove::add_class(m_doc, *preview, "hidden");
        clear(m_doc, *preview);
        m_doc.touch();
    }

    void drag_up(grove::Event& e) {
        if (!m_drag.active) return;
        const Drag drag = m_drag;
        m_drag = Drag{};
        hide_drag_preview();
        highlight_drop(std::string());
        if (!drag.started) return;
        m_suppress_click_until = now_seconds() + 0.3;
        refresh_status();
        refresh_problems();
        const std::string folder = folder_for_item(e.target);
        if (folder.empty()) return;
        std::vector<std::string> sources = m_selected.count(drag.path) ? selection() : std::vector<std::string>{ drag.path };
        transfer(sources, folder, (e.modifiers & (grove::ModifierControl | grove::ModifierMeta)) != 0, true);
    }

    void confirm(const std::string& title, const std::string& message, std::function<void()> then) {
        grove::PopupOptions options;
        options.kind = "question";
        options.title = title;
        options.message = message;
        grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
        if (!popup) {
            then();
            return;
        }
        m_doc.events().add_listener(popup, "ok", [then](grove::Event&) { then(); });
    }

    void ask_text(const std::string& title, const std::string& message, const std::string& value, std::function<void(const std::string&)> then) {
        grove::PopupOptions options;
        options.kind = "question";
        options.title = title;
        options.message = message;
        options.input = "string";
        options.value = value;
        grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
        if (!popup) return;
        m_doc.events().add_listener(popup, "ok", [popup, then](grove::Event&) { then(popup->attribute_or("value", "")); });
    }

    static std::string describe_items(const std::vector<std::string>& paths) {
        if (paths.size() == 1) return "'" + folder_label(paths.front()) + "'";
        return std::to_string(paths.size()) + " items";
    }

    void transfer(const std::vector<std::string>& sources_in, const std::string& folder, bool copy, bool ask) {
        std::vector<std::string> sources;
        const std::string target = normalized(folder);
        for (const std::string& src : sources_in) {
            if (is_root(src)) continue;
            const std::string n = normalized(src);
            if (path_inside(target, n)) {
                notice("Can't put " + folder_label(src) + " inside itself");
                continue;
            }
            if (!copy && normalized(fs::path(n).parent_path().string()) == target) continue;
            sources.push_back(src);
        }
        if (sources.empty()) return;
        std::vector<std::string> clashes;
        for (const std::string& src : sources) {
            std::error_code ec;
            const bool same_folder = normalized(fs::path(normalized(src)).parent_path().string()) == target;
            if (!same_folder && fs::exists(fs::symlink_status(fs::path(folder) / fs::path(src).filename(), ec))) clashes.push_back(src);
        }
        auto act = [this, sources, folder, copy](Clash clash) { copy ? copy_into(sources, folder, clash) : move_into(sources, folder, clash); };
        const std::string verb = copy ? "Copy" : "Move";
        const std::string question = verb + " " + describe_items(sources) + " into '" + folder_label(folder) + "'?";
        if (!clashes.empty() && m_ws.settings.flag("explorer", "confirm-replace", true)) {
            const std::string already = clashes.size() == 1 ? "'" + folder_label(clashes.front()) + "' is already there." : std::to_string(clashes.size()) + " of them are already there.";
            ask_choice(verb, question + "\n" + already + "\nReplace moves what's there now to the trash.", { "Replace", "Keep both", "Skip" }, [act](const std::string& choice) {
                act(choice == "Replace" ? Clash::Replace : choice == "Keep both" ? Clash::KeepBoth : Clash::Skip);
            });
        } else if (ask && m_ws.settings.flag("explorer", "confirm-drag-and-drop", true)) {
            confirm(verb, question, [act] { act(Clash::Replace); });
        } else {
            act(Clash::Replace);
        }
    }

    void ask_choice(const std::string& title, const std::string& message, const std::vector<std::string>& choices, std::function<void(const std::string&)> then) {
        if (choices.empty()) return;
        grove::PopupOptions options;
        options.kind = "question";
        options.title = title;
        options.message = message;
        options.input = "choice";
        options.options = choices;
        options.value = choices.front();
        grove::Node* popup = grove::create_popup(m_doc, nullptr, options);
        if (!popup) {
            then(choices.front());
            return;
        }
        m_doc.events().add_listener(popup, "ok", [popup, then](grove::Event&) { then(popup->attribute_or("value", "")); });
    }

    bool clear_destination(const fs::path& dest) {
        const std::string path = dest.string();
        if (!move_to_trash(path)) {
            std::error_code ec;
            fs::remove_all(dest, ec);
            if (ec) {
                notice("Couldn't replace " + path + ": " + ec.message());
                show_panel("output");
                return false;
            }
        }
        forget_paths({ path });
        return true;
    }

    std::string explorer_key_for(const std::string& path) const {
        std::error_code ec;
        return fs::is_directory(path, ec) ? path : normalized(path);
    }

    void finish_file_op(const std::vector<std::string>& paths, const std::string& folder) {
        if (!folder.empty() && !is_root(folder)) m_expanded.insert(folder);
        if (!paths.empty()) {
            m_selected.clear();
            for (const std::string& p : paths) m_selected.insert(explorer_key_for(p));
            m_anchor = explorer_key_for(paths.back());
        }
        refresh_explorer();
        refresh_status();
    }

    void move_into(const std::vector<std::string>& sources, const std::string& folder) {
        move_into(sources, folder, Clash::Skip);
    }

    void move_into(const std::vector<std::string>& sources, const std::string& folder, Clash clash) {
        std::vector<std::string> moved;
        for (const std::string& src : sources) {
            fs::path dest = fs::path(folder) / fs::path(src).filename();
            std::error_code ec;
            if (fs::exists(fs::symlink_status(dest, ec))) {
                if (normalized(dest.string()) == normalized(src)) continue;
                if (clash == Clash::Skip) {
                    notice(dest.filename().string() + " is already in " + folder + ", so " + src + " stayed where it was");
                    continue;
                }
                if (clash == Clash::KeepBoth) dest = unique_destination(folder, fs::path(src).filename().string());
                else if (path_inside(normalized(src), normalized(dest.string())) || !clear_destination(dest)) continue;
            }
            fs::rename(src, dest, ec);
            if (ec) {
                std::error_code copy_ec;
                fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks, copy_ec);
                if (!copy_ec) fs::remove_all(src, copy_ec);
                if (copy_ec) {
                    notice("Couldn't move " + src + ": " + copy_ec.message());
                    show_panel("output");
                    continue;
                }
            }
            path_moved(src, dest.string());
            moved.push_back(dest.string());
        }
        finish_file_op(moved, folder);
    }

    void copy_into(const std::vector<std::string>& sources, const std::string& folder) {
        copy_into(sources, folder, Clash::KeepBoth);
    }

    void copy_into(const std::vector<std::string>& sources, const std::string& folder, Clash clash) {
        std::vector<std::string> made;
        for (const std::string& src : sources) {
            std::error_code ec;
            const fs::path plain = fs::path(folder) / fs::path(src).filename();
            const bool same_folder = normalized(fs::path(normalized(src)).parent_path().string()) == normalized(folder);
            std::string dest = plain.string();
            if (fs::exists(fs::symlink_status(plain, ec))) {
                if (same_folder || clash == Clash::KeepBoth) dest = unique_destination(folder, fs::path(src).filename().string());
                else if (clash == Clash::Skip) continue;
                else if (path_inside(normalized(src), normalized(dest)) || !clear_destination(plain)) continue;
            }
            fs::copy(src, dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
            if (ec) {
                notice("Couldn't copy " + src + ": " + ec.message());
                show_panel("output");
                continue;
            }
            made.push_back(dest);
        }
        finish_file_op(made, folder);
    }

    void path_moved(const std::string& from, const std::string& to) {
        const std::string old_root = normalized(from);
        const std::string new_root = normalized(to);
        std::vector<std::pair<std::string, std::string>> retarget;
        std::vector<std::string> close;
        for (const OpenFile& f : m_files) {
            if (virtual_tab(f.path) || !path_inside(f.path, old_root)) continue;
            const std::string now = new_root + f.path.substr(old_root.size());
            if (f.editor && !f.view) retarget.emplace_back(f.path, now);
            else close.push_back(f.path);
        }
        for (const std::string& p : close) close_file(p);
        for (const auto& [old_path, now] : retarget) {
            if (OpenFile* f = find_file(old_path)) retarget_file(*f, now);
        }
        std::set<std::string> expanded;
        for (const std::string& e : m_expanded) expanded.insert(path_inside(e, from) ? to + e.substr(from.size()) : e);
        m_expanded = expanded;
        refresh_status();
    }

    void retarget_file(OpenFile& f, const std::string& now) {
        const std::string old = f.path;
        f.path = now;
        f.editor->set_attribute("data-path", now);
        f.editor->set_attribute("language", code_language_name(now));
        apply_editor_settings(*f.editor);
        f.tab->set_attribute("data-path", now);
        for (const auto& child : f.tab->children()) {
            if (!child->is_element()) continue;
            if (child->has_attribute("data-close")) child->set_attribute("data-close", now);
            if (child->has_class("tab-name")) child->set_inner_text((f.dirty ? "\xE2\x97\x8F " : "") + display_name(now));
            if (child->has_class("file-icon")) child->set_attribute("src", file_icon(now, false, false));
        }
        if (const auto c = m_carets.find(old); c != m_carets.end()) {
            m_carets[now] = c->second;
            m_carets.erase(old);
        }
        m_walnut.erase(old);
        m_marks.erase(old);
        m_live.erase(old);
        m_semantic_due.erase(old);
        if (m_active == old) m_active = now;
        schedule_semantic(now, 0.0);
        m_doc.touch();
    }

    void forget_paths(const std::vector<std::string>& gone) {
        std::vector<std::string> close;
        for (const OpenFile& f : m_files) {
            if (virtual_tab(f.path)) continue;
            for (const std::string& g : gone) {
                if (path_inside(f.path, normalized(g))) close.push_back(f.path);
            }
        }
        for (const std::string& p : close) close_file(p);
        for (const std::string& g : gone) {
            m_selected.erase(g);
            m_clip.erase(std::remove(m_clip.begin(), m_clip.end(), g), m_clip.end());
        }
    }

    void ask_delete(const std::vector<std::string>& paths_in) {
        std::vector<std::string> paths;
        for (const std::string& p : paths_in) {
            if (!is_root(p)) paths.push_back(p);
        }
        if (paths.empty()) return;
        auto act = [this, paths] { delete_paths(paths); };
        if (m_ws.settings.flag("explorer", "confirm-delete", true)) {
            std::error_code ec;
            const bool folder = paths.size() == 1 && fs::is_directory(paths.front(), ec);
            confirm("Delete", "Move " + describe_items(paths) + " to the trash?" + (folder ? "\nEverything inside the folder goes with it." : std::string()), act);
        } else {
            act();
        }
    }

    void delete_paths(const std::vector<std::string>& paths) {
        std::vector<std::string> gone;
        std::vector<std::string> stuck;
        for (const std::string& p : paths) {
            if (move_to_trash(p)) gone.push_back(p);
            else stuck.push_back(p);
        }
        forget_paths(gone);
        if (!stuck.empty()) {
            confirm("Delete permanently", "Couldn't move " + describe_items(stuck) + " to the trash.\nDelete permanently? This can't be undone.", [this, stuck] {
                std::vector<std::string> removed;
                for (const std::string& p : stuck) {
                    std::error_code ec;
                    fs::remove_all(p, ec);
                    if (ec) notice("Couldn't delete " + p + ": " + ec.message());
                    else removed.push_back(p);
                }
                forget_paths(removed);
                refresh_explorer();
            });
        }
        refresh_explorer();
    }

    void rename_to(const std::string& path, const std::string& name) {
        rename_to(path, name, false);
    }

    void rename_to(const std::string& path, const std::string& name, bool replace) {
        const std::string problem = bad_file_name(name);
        if (!problem.empty()) {
            notice("Can't rename " + folder_label(path) + ": " + problem);
            show_panel("output");
            return;
        }
        const fs::path dest = fs::path(path).parent_path() / name;
        if (dest == fs::path(path)) return;
        std::error_code ec;
        std::string lower_dest = dest.string();
        std::string lower_path = path;
        for (char& c : lower_dest) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : lower_path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const bool only_case = lower_dest == lower_path;
        if (!only_case && fs::exists(fs::symlink_status(dest, ec))) {
            if (!replace && m_ws.settings.flag("explorer", "confirm-replace", true)) {
                confirm("Replace", "'" + name + "' is already in '" + folder_label(dest.parent_path().string()) + "'.\nReplace it? What's there now goes to the trash.", [this, path, name] { rename_to(path, name, true); });
                return;
            }
            if (path_inside(normalized(path), normalized(dest.string())) || !clear_destination(dest)) return;
        }
        fs::rename(path, dest, ec);
        if (ec) {
            notice("Couldn't rename " + path + ": " + ec.message());
            show_panel("output");
            return;
        }
        if (m_expanded.count(path)) {
            m_expanded.erase(path);
            m_expanded.insert(dest.string());
        }
        path_moved(path, dest.string());
        finish_file_op({ dest.string() }, std::string());
    }

    void ask_rename(const std::string& path) {
        if (is_root(path)) return;
        std::error_code ec;
        const bool folder = fs::is_directory(path, ec);
        const fs::path p(path);
        const std::string ext = folder ? std::string() : p.extension().string();
        const std::string stem = folder ? p.filename().string() : p.stem().string();
        const std::string message = folder ? "New name for the folder '" + stem + "':" : ext.empty() ? "New name for '" + p.filename().string() + "':" : "New name for '" + p.filename().string() + "'. It keeps the " + ext + " extension; use Change Extension to change that.";
        ask_text("Rename", message, stem, [this, path, ext](const std::string& value) {
            const std::string name = trimmed(value);
            if (name.empty()) return;
            rename_to(path, name + ext);
        });
    }

    void ask_extension(const std::string& path) {
        const fs::path p(path);
        std::string ext = p.extension().string();
        if (!ext.empty()) ext.erase(0, 1);
        const std::string stem = p.stem().string();
        ask_text("Change Extension", "New extension for '" + p.filename().string() + "' (leave it empty for none):", ext, [this, path, stem](const std::string& value) {
            std::string e = trimmed(value);
            while (!e.empty() && e.front() == '.') e.erase(e.begin());
            rename_to(path, e.empty() ? stem : stem + "." + e);
        });
    }

    void ask_new(const std::string& folder, bool directory) {
        ask_text(directory ? "New Folder" : "New File", std::string(directory ? "Name of the new folder" : "Name of the new file") + " in '" + folder_label(folder) + "':", "", [this, folder, directory](const std::string& value) {
            const std::string name = trimmed(value);
            if (name.empty()) return;
            const std::string problem = bad_file_name(name);
            if (!problem.empty()) {
                notice("Can't make " + name + ": " + problem);
                show_panel("output");
                return;
            }
            const fs::path dest = fs::path(folder) / name;
            const std::string failed = create_workspace_entry(folder, name, directory);
            if (!failed.empty()) {
                notice("Couldn't make " + dest.string() + ": " + failed);
                set_text(m_doc, "status-problems", "Couldn't make " + name + ": " + failed);
                show_panel("output");
                return;
            }
            finish_file_op({ dest.string() }, folder);
            if (!directory) open_file(dest.string(), 0, 0, false);
        });
    }

    void copy_paths(const std::vector<std::string>& paths, bool relative) {
        std::string text;
        for (const std::string& p : paths) {
            std::string shown = normalized(p);
            if (relative) {
                const std::string root = root_of(p);
                std::error_code ec;
                const fs::path rel = root.empty() ? fs::path(shown) : fs::relative(shown, root, ec);
                if (!ec && !rel.empty()) shown = rel.string();
                if (rel == ".") shown = ".";
            }
            shown = fs::path(shown).make_preferred().string();
            text += (text.empty() ? "" : "\n") + shown;
        }
        if (grove::set_clipboard_text(m_doc, text)) set_text(m_doc, "status-problems", paths.size() == 1 ? "Copied " + text : "Copied " + std::to_string(paths.size()) + " paths");
    }

    void clip(const std::vector<std::string>& paths, bool cut) {
        m_clip.clear();
        for (const std::string& p : paths) {
            if (!is_root(p) || !cut) m_clip.push_back(p);
        }
        m_clip_cut = cut;
        set_text(m_doc, "status-problems", std::string(cut ? "Cut " : "Copied ") + describe_items(m_clip) + "; paste into a folder");
        refresh_explorer();
    }

    void paste_into(const std::string& folder) {
        if (m_clip.empty() || folder.empty()) return;
        std::vector<std::string> sources;
        std::error_code ec;
        for (const std::string& p : m_clip) {
            if (fs::exists(fs::symlink_status(p, ec))) sources.push_back(p);
        }
        const bool cut = m_clip_cut;
        if (cut) {
            m_clip.clear();
            m_clip_cut = false;
        }
        transfer(sources, folder, !cut, false);
    }

    std::string target_folder(const std::vector<std::string>& paths) const {
        if (paths.empty()) return m_ws.root;
        std::error_code ec;
        const std::string& p = paths.front();
        return fs::is_directory(p, ec) ? p : fs::path(p).parent_path().string();
    }

    std::vector<OpenFile*> open_under(const std::vector<std::string>& paths) {
        std::vector<OpenFile*> out;
        for (OpenFile& f : m_files) {
            if (virtual_tab(f.path)) continue;
            for (const std::string& p : paths) {
                if (path_inside(f.path, normalized(p))) {
                    out.push_back(&f);
                    break;
                }
            }
        }
        return out;
    }

    void revert(OpenFile& f) {
        if (!f.editor) return;
        const std::optional<std::string> text = read_text_file(f.path);
        if (!text) {
            notice("Couldn't read " + f.path + " to revert it");
            return;
        }
        f.editor->set_attribute("value", *text);
        f.saved = *text;
        set_dirty(f, false);
        schedule_semantic(f.path, 0.0);
        m_doc.touch();
    }

    void save_all() {
        for (OpenFile& f : m_files) {
            if (f.dirty) save(f.path);
        }
    }

    bool any_dirty() const {
        for (const OpenFile& f : m_files) {
            if (f.dirty) return true;
        }
        return false;
    }

    void show_explorer_menu(double x, double y, const std::vector<std::string>& paths) {
        grove::Node* explorer = m_doc.find_by_id("explorer");
        if (!explorer) return;
        m_menu_paths = paths;
        m_menu_folder = target_folder(paths);
        std::vector<grove::MenuItem> items;
        std::error_code ec;
        const bool single = paths.size() == 1;
        const bool folder = single && fs::is_directory(paths.front(), ec);
        const bool root = single && is_root(paths.front());
        const bool has_folder = !m_ws.root.empty();
        if (paths.empty()) {
            if (has_folder) {
                items.push_back(grove::menu_item("new-file", "New File\xE2\x80\xA6"));
                items.push_back(grove::menu_item("new-folder", "New Folder\xE2\x80\xA6"));
                items.push_back(grove::menu_item("paste", "Paste", "Ctrl+V", m_clip.empty()));
                items.push_back(grove::menu_separator());
                items.push_back(grove::menu_item("terminal", "Open in Terminal"));
                items.push_back(grove::menu_item("serve-root", "Serve with Local Server"));
                items.push_back(grove::menu_item("refresh", "Refresh"));
                items.push_back(grove::menu_separator());
            }
            items.push_back(grove::menu_item("add-folder", "Add Folder to Workspace\xE2\x80\xA6"));
            if (any_dirty()) items.push_back(grove::menu_item("save-all", "Save All", "Ctrl+Alt+S"));
            grove::open_menu(m_doc, *explorer, items, x, y);
            return;
        }
        if (single && !folder) items.push_back(grove::menu_item("open", "Open", "Enter"));
        if (folder) {
            items.push_back(grove::menu_item("new-file", "New File\xE2\x80\xA6"));
            items.push_back(grove::menu_item("new-folder", "New Folder\xE2\x80\xA6"));
        }
        if (single) {
            items.push_back(grove::menu_item("terminal", "Open in Terminal"));
            items.push_back(grove::menu_item("reveal", "Reveal in File Manager"));
            const std::string ext = fs::path(paths.front()).extension().string();
            if (folder) items.push_back(grove::menu_item("serve", "Serve with Local Server"));
            else if (ext == ".html" || ext == ".htm" || ext == ".HTML" || ext == ".HTM") items.push_back(grove::menu_item("serve", "Open with Local Server"));
        }
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("cut", "Cut", "Ctrl+X", root));
        items.push_back(grove::menu_item("copy", "Copy", "Ctrl+C"));
        if (folder) items.push_back(grove::menu_item("paste", "Paste", "Ctrl+V", m_clip.empty()));
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("copy-path", "Copy Path", "Shift+Alt+C"));
        items.push_back(grove::menu_item("copy-relative", "Copy Relative Path", "Ctrl+Shift+Alt+C"));
        if (!root) {
            items.push_back(grove::menu_separator());
            if (single) items.push_back(grove::menu_item("rename", "Rename\xE2\x80\xA6", "F2"));
            if (single && !folder) items.push_back(grove::menu_item("extension", "Change Extension\xE2\x80\xA6"));
            items.push_back(grove::menu_item("delete", "Delete", "Delete"));
        }
        bool dirty_here = false;
        for (const OpenFile* f : open_under(paths)) dirty_here = dirty_here || f->dirty;
        items.push_back(grove::menu_separator());
        if (dirty_here) {
            items.push_back(grove::menu_item("save", "Save"));
            items.push_back(grove::menu_item("revert", "Revert Changes"));
        }
        items.push_back(grove::menu_item("save-all", "Save All", "Ctrl+Alt+S", !any_dirty()));
        if (root && multi_root()) items.push_back(grove::menu_item("remove-root", "Remove Folder from Workspace"));
        items.push_back(grove::menu_item("add-folder", "Add Folder to Workspace\xE2\x80\xA6"));
        grove::open_menu(m_doc, *explorer, items, x, y);
    }

    void explorer_menu(const std::string& action) {
        const std::vector<std::string> paths = m_menu_paths;
        const std::string folder = m_menu_folder;
        explorer_action(action, paths, folder);
    }

    void explorer_action(const std::string& action, const std::vector<std::string>& paths, const std::string& folder) {
        std::error_code ec;
        if (action == "open") {
            for (const std::string& p : paths) {
                if (!fs::is_directory(p, ec)) open_file(p, 0, 0, false);
            }
        } else if (action == "new-file") ask_new(folder, false);
        else if (action == "new-folder") ask_new(folder, true);
        else if (action == "terminal") {
            new_terminal(folder.empty() ? m_ws.root : folder, true);
        } else if (action == "reveal") {
            if (!paths.empty() && !reveal_in_file_manager(paths.front())) {
                set_text(m_doc, "status-problems", "Couldn't open the file manager for " + folder_label(paths.front()));
                notice("Couldn't open the file manager for " + paths.front());
            }
        } else if (action == "cut") clip(paths, true);
        else if (action == "copy") clip(paths, false);
        else if (action == "paste") paste_into(folder);
        else if (action == "serve") {
            if (!paths.empty()) serve_path(paths.front());
        } else if (action == "serve-root") {
            if (!m_ws.root.empty()) serve_path(m_ws.root);
        } else if (action == "copy-path") copy_paths(paths, false);
        else if (action == "copy-relative") copy_paths(paths, true);
        else if (action == "rename") {
            if (paths.size() == 1) ask_rename(paths.front());
        } else if (action == "extension") {
            if (paths.size() == 1) ask_extension(paths.front());
        } else if (action == "delete") ask_delete(paths);
        else if (action == "save") {
            for (OpenFile* f : open_under(paths)) {
                if (f->dirty) save(f->path);
            }
        } else if (action == "revert") {
            for (OpenFile* f : open_under(paths)) {
                if (f->dirty) revert(*f);
            }
        } else if (action == "save-all") save_all();
        else if (action == "remove-root") {
            if (!paths.empty()) remove_root(paths.front());
        } else if (action == "add-folder") {
            if (!m_doc.host().file_dialog) return;
            grove::FileDialogRequest request;
            request.mode = "folder";
            request.title = "Add a folder to the workspace";
            request.start = m_ws.root;
            for (const std::string& chosen : m_doc.host().file_dialog(request)) add_folder(chosen);
        } else if (action == "refresh") refresh_explorer();
    }

    void move_selection(int step) {
        if (m_visible.empty()) return;
        auto it = std::find(m_visible.begin(), m_visible.end(), m_anchor);
        std::ptrdiff_t at = it == m_visible.end() ? (step > 0 ? -1 : static_cast<std::ptrdiff_t>(m_visible.size())) : it - m_visible.begin();
        at = std::clamp<std::ptrdiff_t>(at + step, 0, static_cast<std::ptrdiff_t>(m_visible.size()) - 1);
        m_anchor = m_visible[static_cast<std::size_t>(at)];
        m_selected = { m_anchor };
        refresh_explorer();
    }

    void explorer_key(grove::Event& e) {
        const std::vector<std::string> paths = selection();
        const std::string folder = target_folder(paths);
        if (chord_is(e, "Delete")) explorer_action("delete", paths, folder);
        else if (chord_is(e, "F2")) explorer_action("rename", paths, folder);
        else if (chord_is(e, "Ctrl+C")) explorer_action("copy", paths, folder);
        else if (chord_is(e, "Ctrl+X")) explorer_action("cut", paths, folder);
        else if (chord_is(e, "Ctrl+V")) explorer_action("paste", paths, folder);
        else if (chord_is(e, "Shift+Alt+C")) explorer_action("copy-path", paths, folder);
        else if (chord_is(e, "Ctrl+Shift+Alt+C")) explorer_action("copy-relative", paths, folder);
        else if (chord_is(e, "Up")) move_selection(-1);
        else if (chord_is(e, "Down")) move_selection(1);
        else if (chord_is(e, "Escape")) {
            m_selected.clear();
            refresh_explorer();
        } else if (chord_is(e, "Enter")) {
            if (paths.size() == 1) {
                std::error_code ec;
                if (fs::is_directory(paths.front(), ec)) {
                    const std::string p = paths.front();
                    if (is_root(p)) m_collapsed_roots.count(p) ? (void)m_collapsed_roots.erase(p) : (void)m_collapsed_roots.insert(p);
                    else m_expanded.count(p) ? (void)m_expanded.erase(p) : (void)m_expanded.insert(p);
                    refresh_explorer();
                } else {
                    open_file(paths.front(), 0, 0, false);
                }
            }
        } else {
            return;
        }
        e.default_prevented = true;
        e.propagation_stopped = true;
    }

    TerminalSession* active_terminal() { return m_term < m_terms.size() ? m_terms[m_term].get() : nullptr; }

    bool panel_showing(const char* name) {
        const grove::Node* view = m_doc.find_by_id(std::string("view-") + name);
        return view && view->has_class("active");
    }

    void apply_terminal_style(grove::Node& view) {
        const std::string family = m_ws.settings.get_or("terminal", "font-family", m_ws.settings.get_or("editor", "font-family", "monospace"));
        grove::set_inline_style(m_doc, view, "font-family", family.empty() ? "" : "\"" + family + "\"");
        const std::string fallback = "13px";
        const std::optional<SizeValue> size = parse_size(m_ws.settings.get_or("terminal", "font-size", fallback));
        grove::set_inline_style(m_doc, view, "font-size", grove_size(size.value_or(parse_size(fallback).value_or(SizeValue{ 14.0, "px" })), m_zoom));
    }

    int terminal_tab_size() const {
        return static_cast<int>(std::clamp<std::int64_t>(grove::parse_integer(m_ws.settings.get_or("terminal", "tab-size", "4")).value_or(4), 1, 16));
    }

    std::size_t terminal_scrollback() const {
        return static_cast<std::size_t>(std::clamp<std::int64_t>(grove::parse_integer(m_ws.settings.get_or("terminal", "scrollback", "10000")).value_or(10000), 100, 1000000));
    }

    TerminalSession& add_terminal(const std::string& folder) {
        auto t = std::make_unique<TerminalSession>();
        t->id = "terminal-" + std::to_string(++m_term_serial);
        t->folder = folder;
        const std::string base = folder.empty() ? std::string("Home") : folder_label(folder);
        std::size_t same = 0;
        for (const auto& other : m_terms) {
            if (normalized(other->folder) == normalized(folder)) ++same;
        }
        t->name = same == 0 ? base : base + " (" + std::to_string(same + 1) + ")";
        grove::Node* view = m_doc.create_element("terminal");
        view->set_attribute("class", "terminal-view hidden");
        view->set_attribute("data-terminal", t->id);
        view->set_attribute("copy-on-ctrl-c", "false");
        apply_terminal_style(*view);
        if (grove::Node* host = m_doc.find_by_id("terminal-views")) m_doc.append_child(*host, *view);
        t->view = view;
        t->term = grove::terminal_of(m_doc, *view);
        t->term->set_scrollback_limit(terminal_scrollback());
        t->term->set_tab_width(terminal_tab_size());
        TerminalSession* raw = t.get();
        t->term->set_input_handler([raw](const std::string& bytes) {
            if (raw->process && raw->started && !raw->exited) raw->process->write(bytes);
        });
        m_doc.events().add_listener(view, "key_down", [this, raw](grove::Event& e) {
            if (raw->exited && chord_is(e, "Enter")) {
                start_terminal(*raw);
                refresh_terminal_ui(true);
                e.default_prevented = true;
            }
        });
        m_terms.push_back(std::move(t));
        return *m_terms.back();
    }

    void remove_terminal(std::size_t index) {
        if (index >= m_terms.size()) return;
        TerminalSession& t = *m_terms[index];
        if (t.process) t.process->stop();
        if (t.node_proxy) {
            for (std::size_t i = 0; i < m_servers.size(); ++i) {
                if (m_servers[i].get() == t.node_proxy) {
                    stop_server(i);
                    break;
                }
            }
            t.node_proxy = nullptr;
        }
        if (t.view) {
            grove::forget_terminal(m_doc, *t.view);
            m_doc.remove(*t.view);
        }
        m_terms.erase(m_terms.begin() + static_cast<std::ptrdiff_t>(index));
        if (m_term >= m_terms.size()) m_term = m_terms.empty() ? 0 : m_terms.size() - 1;
    }

    std::string node_project_folder(const std::string& file) const {
        std::error_code ec;
        fs::path dir = fs::path(file).parent_path();
        const std::string root = root_of(file);
        for (fs::path d = dir; !d.empty(); d = d.parent_path()) {
            if (fs::is_regular_file(d / "package.json", ec)) return d.string();
            if (!root.empty() && normalized(d.string()) == normalized(root)) break;
            if (d == d.parent_path()) break;
        }
        return dir.string();
    }

    std::string node_command(const TerminalSession& t, const std::string& node, const std::string& file) const {
        std::error_code ec;
        std::string rel = fs::relative(file, t.folder, ec).string();
        if (ec || rel.empty() || rel.rfind("..", 0) == 0) rel = file;
        auto quoted = [](const std::string& text) {
            std::string out = "\"";
            for (const char c : text) {
                if (c == '"' || c == '$' || c == '`' || c == '\\') {
#if defined(_WIN32)
                    if (c == '"') out += '\\';
#else
                    out += '\\';
#endif
                }
                out += c;
            }
            return out + "\"";
        };
        std::string shell = t.process ? t.process->shell() : std::string();
        std::transform(shell.begin(), shell.end(), shell.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool powershell = shell.find("powershell") != std::string::npos || shell.find("pwsh") != std::string::npos;
        return std::string(powershell ? "& " : "") + quoted(node) + " " + quoted(rel);
    }

    TerminalSession* node_session(const std::string& file) {
        for (const auto& t : m_terms) {
            if (normalized(t->node_file) == normalized(file)) return t.get();
        }
        return nullptr;
    }

    void run_node(const std::string& file) {
        const Tool* node = m_ws.toolchain.get("node");
        if (!node) {
            m_output = "JavaScript files run with Node.js, which Canopy can't find. Install it from https://nodejs.org (or your package manager), or set toolchain { node: \"/path/to/node\"; } under Settings > Compilers.\n";
            sync_output();
            show_panel("output");
            return;
        }
        TerminalSession* t = node_session(file);
        bool created = false;
        if (!t) {
            created = true;
            t = &add_terminal(node_project_folder(file));
            t->name = "node " + fs::path(file).filename().string();
            t->node_file = file;
        }
        for (std::size_t i = 0; i < m_terms.size(); ++i) {
            if (m_terms[i].get() == t) m_term = i;
        }
        show_panel("terminal");
        const bool fresh = created || !t->started || t->exited;
        start_terminal(*t);
        if (!fresh && t->process) t->process->interrupt();
        t->node_port = 0;
        t->node_scan.clear();
        t->node_open_browser = true;
        const std::string id = t->id;
        const std::string node_path = node->path;
        m_doc.events().set_timeout(fresh ? 500.0 : 350.0, [this, id, node_path, file] {
            for (const auto& owned : m_terms) {
                if (owned->id != id || !owned->process || owned->exited) continue;
                owned->process->send_line(node_command(*owned, node_path, file));
            }
        });
        refresh_terminal_ui(true);
    }

    void restart_node_apps(const std::string& saved) {
        if (!m_ws.settings.flag("server", "restart-node", true)) return;
        const Tool* node = m_ws.toolchain.get("node");
        if (!node) return;
        for (const auto& owned : m_terms) {
            TerminalSession& t = *owned;
            if (t.node_file.empty() || !t.started || t.exited || !t.process) continue;
            const std::string folder = normalized(t.folder);
            const std::string path = normalized(saved);
            if (path.rfind(folder, 0) != 0) continue;
            const std::string ext = fs::path(saved).extension().string();
            if (ext != ".js" && ext != ".mjs" && ext != ".cjs" && ext != ".json") continue;
            if (path.find("/node_modules/") != std::string::npos) continue;
            t.process->interrupt();
            t.node_port = 0;
            t.node_scan.clear();
            t.node_open_browser = false;
            t.term->feed("\x1b[2m[Canopy: restarting after you saved " + fs::path(saved).filename().string() + "]\x1b[0m\r\n");
            const std::string id = t.id;
            const std::string node_path = node->path;
            const std::string file = t.node_file;
            m_doc.events().set_timeout(350.0, [this, id, node_path, file] {
                for (const auto& o : m_terms) {
                    if (o->id != id || !o->process || o->exited) continue;
                    o->process->send_line(node_command(*o, node_path, file));
                }
            });
        }
    }

    void scan_node_output(TerminalSession& t, const std::string& data) {
        if (t.node_file.empty() || data.empty()) return;
        std::string clean;
        for (std::size_t i = 0; i < data.size(); ++i) {
            if (data[i] == '\x1b') {
                ++i;
                if (i < data.size() && data[i] == '[') {
                    while (i + 1 < data.size() && !(data[i + 1] >= '@' && data[i + 1] <= '~')) ++i;
                    ++i;
                }
                continue;
            }
            clean += data[i];
        }
        t.node_scan += clean;
        if (t.node_scan.size() > 8192) t.node_scan.erase(0, t.node_scan.size() - 8192);
        static const std::regex missing_module(R"(Cannot find module '([^'./][^']*)')");
        std::smatch mm;
        if (std::regex_search(t.node_scan, mm, missing_module)) {
            std::string name = mm[1].str();
            if (name.rfind("node:", 0) != 0) {
                const std::size_t slash = name.find('/');
                if (name[0] != '@' && slash != std::string::npos) name = name.substr(0, slash);
                notice(fs::path(t.node_file).filename().string() + " needs the '" + name + "' package: run  npm install " + name + "  in its terminal");
                t.node_scan.clear();
            }
        }
        if (t.node_port != 0) return;
        static const std::regex port_re(R"((?:localhost|127\.0\.0\.1|0\.0\.0\.0|\[::\]|\[::1\]):(\d{2,5})|\bport\s*[:=]?\s*(\d{2,5})\b)", std::regex::icase);
        std::smatch m;
        if (!std::regex_search(t.node_scan, m, port_re)) return;
        const int port = std::stoi(m[1].matched ? m[1].str() : m[2].str());
        if (port <= 0 || port > 65535) return;
        t.node_port = static_cast<std::uint16_t>(port);
        node_listening(t);
    }

    void node_listening(TerminalSession& t) {
        const std::string direct = "http://localhost:" + std::to_string(t.node_port) + "/";
        if (!m_ws.settings.flag("server", "proxy-node", true)) {
            if (t.node_open_browser && !open_in_browser(direct)) notice("Your app is at " + direct);
            t.node_open_browser = false;
            return;
        }
        LiveServer* proxy = nullptr;
        for (const auto& s : m_servers) {
            if (s.get() == t.node_proxy) proxy = s.get();
        }
        if (proxy) {
            proxy->set_proxy(t.node_port);
            proxy->changed(proxy->folder());
        } else {
            auto server = std::make_unique<LiveServer>();
            server->set_console(m_ws.settings.flag("server", "console", true));
            server->set_proxy(t.node_port);
            std::string error;
            std::uint16_t first = server_port_setting();
            if (first == t.node_port) first = static_cast<std::uint16_t>(first + 1);
            if (!server->start(t.folder, first, m_ws.settings.get_or("server", "host", "127.0.0.1"), m_ws.settings.flag("server", "live-reload", true), error)) {
                notice(error + "; your app is at " + direct);
                if (t.node_open_browser) open_in_browser(direct);
                t.node_open_browser = false;
                return;
            }
            proxy = server.get();
            t.node_proxy = proxy;
            m_servers.push_back(std::move(server));
            refresh_ports(true);
            if (m_console) m_console->servers_changed();
            if (m_elements) m_elements->servers_changed();
        }
        if (t.node_open_browser) {
            notice("Your app is listening on port " + std::to_string(t.node_port) + "; Canopy opened it through " + proxy->url() + " so the Console, Elements and live reload work");
            if (!open_in_browser(proxy->url())) notice("Couldn't open a browser; your app is at " + proxy->url());
        }
        t.node_open_browser = false;
    }

    void start_terminal(TerminalSession& t) {
        if (t.started && !t.exited) return;
        if (t.started) t.term->feed("\x1b[0m\r\n");
        t.process = std::make_unique<TerminalProcess>();
        TerminalOptions options;
        options.folder = t.folder;
        if (options.folder.empty()) {
            const char* home = std::getenv("HOME");
            if (!home) home = std::getenv("USERPROFILE");
            options.folder = home ? home : "";
        }
        options.shell = terminal_shell(m_ws.settings);
        options.columns = static_cast<unsigned>(t.term->columns());
        options.rows = static_cast<unsigned>(t.term->rows());
        options.canopy_prompt = m_ws.settings.get_or("terminal", "prompt", "canopy") != "shell";
        t.cols = t.term->columns();
        t.rows = t.term->rows();
        t.started = true;
        t.exited = false;
        if (!t.process->start(options)) {
            t.term->feed("\x1b[31mCouldn't start the terminal: " + t.process->error() + "\x1b[0m\r\nSet terminal { shell: \"/path/to/shell\"; } under Settings > Explorer, Terminal and Git to pick another shell.\r\n");
            t.exited = true;
        }
        m_doc.touch();
    }

    void reset_terminals() {
        while (!m_terms.empty()) remove_terminal(m_terms.size() - 1);
        m_term = 0;
        if (m_roots.empty()) add_terminal(std::string());
        for (const std::string& root : m_roots) add_terminal(root);
        if (panel_showing("terminal")) {
            if (TerminalSession* t = active_terminal()) start_terminal(*t);
        }
        refresh_terminal_ui(true);
    }

    void new_terminal(const std::string& folder, bool show) {
        add_terminal(folder);
        m_term = m_terms.size() - 1;
        if (show) show_panel("terminal");
        else if (panel_showing("terminal")) start_terminal(*m_terms.back());
        refresh_terminal_ui(true);
    }

    void new_terminal(const std::string& folder) { new_terminal(folder, true); }

    void pick_terminal(const std::string& id) {
        for (std::size_t i = 0; i < m_terms.size(); ++i) {
            if (m_terms[i]->id != id) continue;
            m_term = i;
            start_terminal(*m_terms[i]);
            refresh_terminal_ui(true);
            focus_terminal();
            return;
        }
    }

    void clear_terminal() {
        TerminalSession* t = active_terminal();
        if (!t) return;
        t->term->clear_scrollback();
        if (t->process && t->started && !t->exited) t->process->write(std::string(1, '\x0c'));
        else t->term->feed("\x1b[H\x1b[2J");
        m_doc.touch();
        focus_terminal();
    }

    void kill_terminal() {
        if (m_terms.empty()) return;
        remove_terminal(m_term);
        if (TerminalSession* t = active_terminal(); t && panel_showing("terminal")) start_terminal(*t);
        refresh_terminal_ui(true);
        focus_terminal();
    }

    TerminalSession* terminal_by_id(const std::string& id) {
        for (const auto& t : m_terms) {
            if (t->id == id) return t.get();
        }
        return nullptr;
    }

    void terminal_menu(grove::Event& e) {
        const grove::Node* view = with_attribute(e.target, "data-terminal");
        grove::Node* host = m_doc.find_by_id("terminal-views");
        if (!view || !host) return;
        TerminalSession* t = terminal_by_id(view->attribute_or("data-terminal", ""));
        if (!t || !t->term) return;
        if (t->term->mouse_mode() != grove::TermMouseMode::None && (e.modifiers & grove::ModifierShift) == 0) return;
        m_menu_terminal = t->id;
        std::vector<grove::MenuItem> items;
        items.push_back(grove::menu_item("copy", "Copy", "Ctrl+Shift+C", !t->term->has_selection()));
        items.push_back(grove::menu_item("paste", "Paste", "Ctrl+Shift+V", grove::clipboard_text(m_doc).empty()));
        items.push_back(grove::menu_item("select-all", "Select All"));
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("clear", "Clear", "Ctrl+L"));
        items.push_back(grove::menu_item("new", "New Terminal"));
        items.push_back(grove::menu_item("kill", "Kill Terminal"));
        grove::open_menu(m_doc, *host, items, e.x, e.y);
        e.default_prevented = true;
    }

    void terminal_menu_action(const std::string& action) {
        TerminalSession* t = terminal_by_id(m_menu_terminal);
        if (!t || !t->term) return;
        for (std::size_t i = 0; i < m_terms.size(); ++i) {
            if (m_terms[i].get() == t) m_term = i;
        }
        if (action == "copy") {
            if (t->term->has_selection() && grove::set_clipboard_text(m_doc, t->term->selected_text())) t->term->clear_selection();
        } else if (action == "paste") {
            const std::string text = grove::clipboard_text(m_doc);
            if (!text.empty()) {
                t->term->clear_selection();
                t->term->reset_view();
                t->term->send(t->term->encode_paste(text));
            }
        } else if (action == "select-all") {
            t->term->select_all();
        } else if (action == "clear") {
            clear_terminal();
        } else if (action == "new") {
            new_terminal(t->folder);
        } else if (action == "kill") {
            kill_terminal();
            return;
        }
        m_doc.touch();
        focus_terminal();
    }

    void focus_terminal() {
        TerminalSession* t = active_terminal();
        if (t && t->view && m_doc.host().focus) m_doc.host().focus(t->view);
    }

    void refresh_terminal_ui(bool full) {
        TerminalSession* active = active_terminal();
        if (grove::Node* picker = m_doc.find_by_id("terminal-picker")) {
            std::vector<grove::SelectOption> options;
            for (const auto& s : m_terms) {
                std::string label = s->name;
                const std::string title = s->term ? s->term->title() : std::string();
                if (s->exited) label += " (exited)";
                else if (!s->started) label += " (not started)";
                else if (!title.empty()) label += "  \xC2\xB7  " + title;
                if (!s->folder.empty() && title.empty()) label += "  \xC2\xB7  " + s->folder;
                options.push_back(grove::SelectOption{ s->id, label, false });
            }
            if (options.empty()) options.push_back(grove::SelectOption{ "", "No terminal; press + New", false });
            std::vector<grove::SelectOption> now = grove::select_options(*picker);
            bool same = !full && now.size() == options.size();
            for (std::size_t i = 0; same && i < now.size(); ++i) same = now[i].value == options[i].value && now[i].label == options[i].label;
            if (!same) grove::set_select_options(m_doc, *picker, options);
            picker->set_attribute("value", active ? active->id : std::string());
        }
        for (const auto& s : m_terms) {
            if (!s->view) continue;
            if (s.get() == active) grove::remove_class(m_doc, *s->view, "hidden");
            else grove::add_class(m_doc, *s->view, "hidden");
            if (full) apply_terminal_style(*s->view);
        }
        if (grove::Node* empty = m_doc.find_by_id("terminal-empty")) {
            if (m_terms.empty()) grove::remove_class(m_doc, *empty, "hidden");
            else grove::add_class(m_doc, *empty, "hidden");
        }
        m_doc.touch();
    }

    void poll_terminals() {
        bool touched = false;
        bool relabel = false;
        for (const auto& owned : m_terms) {
            TerminalSession& t = *owned;
            if (!t.process || !t.started || !t.term) continue;
            if (!t.exited) {
                std::string data = t.process->read();
                const bool alive = t.process->running();
                if (!alive) data += t.process->read();
                if (!data.empty()) t.term->feed(data);
                if (!data.empty() && !t.node_file.empty()) scan_node_output(t, data);
                const std::string reply = t.term->take_reply();
                if (!reply.empty() && alive) t.process->write(reply);
                if (!alive) {
                    t.exited = true;
                    t.term->feed("\x1b[0m\r\n\x1b[2m[The shell exited with code " + std::to_string(t.process->exit_code().value_or(0)) + ". Press Enter to start a new one.]\x1b[0m\r\n");
                    relabel = true;
                }
            }
            if (t.term->columns() != t.cols || t.term->rows() != t.rows) {
                t.cols = t.term->columns();
                t.rows = t.term->rows();
                if (!t.exited) t.process->resize(static_cast<unsigned>(t.cols), static_cast<unsigned>(t.rows));
            }
            if (t.term->take_title_changed()) relabel = true;
            t.term->take_bell();
            if (t.term->take_changed()) touched = true;
        }
        if (relabel) refresh_terminal_ui(false);
        else if (touched) m_doc.touch();
    }

    void refresh_recent() {
        grove::Node* recent = m_doc.find_by_id("recent");
        if (!recent) return;
        clear(m_doc, *recent);
        for (const std::string& folder : recent_folders()) {
            if (folder == m_ws.root) continue;
            grove::Node* item = add(m_doc, *recent, "item", display_name(folder));
            item->set_attribute("data-path", folder);
        }
        if (recent->children().empty()) add(m_doc, *recent, "item", "None yet", "muted");
    }

    void refresh_problems() {
        grove::Node* problems = m_doc.find_by_id("problems");
        grove::Node* warnings = m_doc.find_by_id("warnings");
        if (!problems) return;
        ListSync errors_list(m_doc, *problems);
        std::optional<ListSync> warnings_list;
        if (warnings) warnings_list.emplace(m_doc, *warnings);
        ListSync& warn_list = warnings_list ? *warnings_list : errors_list;
        std::size_t errors = 0;
        std::size_t warns = 0;
        std::vector<CompilerMessage> shown = m_messages;
        for (const auto& [path, list] : m_live) {
            for (const CompilerMessage& live : list) {
                bool seen = false;
                for (const CompilerMessage& m : m_messages) seen = seen || (m.line == live.line && m.message == live.message && normalized(m.file) == normalized(live.file));
                if (!seen) shown.push_back(live);
            }
        }
        for (const CompilerMessage& m : shown) {
            if (m.severity == "note") continue;
            const bool error = m.severity == "error";
            ListSync& list = error || !warnings ? errors_list : warn_list;
            (error ? errors : warns) += 1;
            list.add("p", display_name(m.file) + ":" + std::to_string(m.line) + (m.column > 0 ? ":" + std::to_string(m.column) : std::string()) + "  " + m.message, error ? "problem error" : "problem");
            fs::path file(m.file);
            if (file.is_relative()) file = fs::path(m_ws.root) / file;
            list.set("data-path", file.string());
            list.set("data-line", std::to_string(m.line));
            list.set("data-column", std::to_string(std::max<std::size_t>(1, m.column)));
        }
        for (const ToolProblem& p : m_ws.toolchain.problems) {
            warn_list.add("p", p.message, "problem");
            ++warns;
        }
        for (const grove::Diagnostic& d : m_ws.diagnostics.items()) {
            const bool error = d.severity == grove::Severity::Error;
            ListSync& list = error ? errors_list : warn_list;
            list.add("p", d.file + ":" + std::to_string(d.span.begin.line) + ": " + d.message, error ? "problem error" : "problem");
            list.set("data-path", d.file);
            list.set("data-line", std::to_string(d.span.begin.line));
            (error ? errors : warns) += 1;
        }
        const std::vector<ShortcutBinding>& bindings = this->bindings();
        for (const std::string& message : shortcut_errors(bindings)) {
            errors_list.add("p", "Shortcuts: " + message, "problem error");
            errors_list.set("data-settings-page", "shortcuts");
            ++errors;
        }
        for (const ShortcutConflict& c : shortcut_conflicts(bindings)) {
            errors_list.add("p", "Shortcut conflict: " + c.message, "problem error");
            if (!c.file.empty()) {
                errors_list.set("data-path", c.file);
                errors_list.set("data-line", std::to_string(std::max<std::size_t>(1, c.line)));
            } else {
                errors_list.set("data-settings-page", "shortcuts");
            }
            ++errors;
        }
        for (const ProjectNameProblem& p : m_names.problems) {
            errors_list.add("p", display_name(m_names.path) + ":" + std::to_string(std::max<std::size_t>(1, p.line)) + "  Special characters: " + p.message, "problem error");
            errors_list.set("data-path", m_names.path);
            errors_list.set("data-line", std::to_string(std::max<std::size_t>(1, p.line)));
            errors_list.set("data-column", "1");
            ++errors;
        }
        if (errors_list.size() == 0) errors_list.add("p", "No errors.", "ok");
        if (warnings_list && warnings_list->size() == 0) warnings_list->add("p", "No warnings.", "ok");
        errors_list.finish();
        if (warnings_list) warnings_list->finish();
        auto count = [](std::size_t n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); };
        set_text(m_doc, "status-problems", errors + warns == 0 ? std::string("No problems") : count(errors, "error") + ", " + count(warns, "warning"));
        set_text(m_doc, "tab-problems", errors == 0 ? std::string("PROBLEMS") : "PROBLEMS " + std::to_string(errors));
        set_text(m_doc, "tab-warnings", warns == 0 ? std::string("WARNINGS") : "WARNINGS " + std::to_string(warns));
    }

    void refresh_status() {
        const std::string name = folder_label(m_ws.root);
        set_text(m_doc, "workspace-name", multi_root() ? name + " + " + std::to_string(m_roots.size() - 1) + (m_roots.size() == 2 ? " folder" : " folders") : name.empty() ? m_ws.root : name);
        set_text(m_doc, "status-workspace", multi_root() ? m_ws.root + " (+" + std::to_string(m_roots.size() - 1) + ")" : m_ws.root);
        set_text(m_doc, "status-toolchain", toolchain_summary(m_ws.toolchain));
        set_text(m_doc, "status-version", std::string("Canopy ") + CANOPY_VERSION_STRING);
        if (m_active.empty()) {
            set_text(m_doc, "status-language", "");
            set_text(m_doc, "status-cursor", "");
        } else {
            const Language* lang = language_for_path(m_active);
            const grove::CodeLanguage code = grove::code_language_for_path(m_active);
            std::string label = lang ? lang->name : code == grove::CodeLanguage::CMake ? std::string("CMake") : std::string("Plain text");
            std::string tip;
            if (lang && lang->id == LanguageId::Assembly) {
                if (const OpenFile* f = find_file(m_active); f && f->editor) {
                    const AsmInfo info = detect_assembly(grove::input_string(*f->editor).value_or(""), m_active, m_ws.settings.get_or("language.asm", "dialect", "auto"), m_ws.settings.get_or("language.asm", "arch", "auto"));
                    label = "Assembly (" + assembly_summary(info) + ")";
                    tip = assembly_report(info, m_ws.toolchain);
                }
            }
            if (grove::Node* status = m_doc.find_by_id("status-language")) {
                if (tip.empty()) status->remove_attribute("title");
                else status->set_attribute("title", tip);
            }
            if (m_active == k_settings_tab) label = "Settings";
            else if (m_active == k_developer_tab) label = "Developer";
            else if (virtual_tab(m_active)) label = m_active.rfind("canopy:diff:", 0) == 0 ? "Diff" : "Read-only";
            if (const OpenFile* f = find_file(m_active)) {
                if (f->kind == ViewerKind::Image) label = "Image";
                else if (f->kind == ViewerKind::Pdf) label = "PDF";
                else if (f->kind == ViewerKind::Binary) label = "Binary";
                else if (f->kind == ViewerKind::Latex) label = "LaTeX";
                else if (f->kind == ViewerKind::Markdown) label = "Markdown";
                else if (f->kind == ViewerKind::Math) label = is_brace_math_path(f->path) ? "MathText (brace)" : "MathText";
                if (!f->editor) set_text(m_doc, "status-cursor", "");
            }
            set_text(m_doc, "status-language", label);
        }
        if (grove::Node* body = m_doc.first_by_tag("body")) {
            body->set_attribute("title", m_active.empty() ? "Canopy \xE2\x80\x94 " + (name.empty() ? m_ws.root : name) : display_name(m_active) + " \xE2\x80\x94 Canopy");
        }
    }

    void refresh_all() {
        refresh_explorer();
        refresh_recent();
        refresh_problems();
        refresh_status();
        refresh_ports(true);
    }

    std::uint16_t server_port_setting() const {
        const std::int64_t port = grove::parse_integer(m_ws.settings.get_or("server", "port", "5500")).value_or(5500);
        return static_cast<std::uint16_t>(std::clamp<std::int64_t>(port, 1, 65535));
    }

    LiveServer* server_for(const std::string& path) {
        LiveServer* best = nullptr;
        for (const auto& s : m_servers) {
            if (s->running() && s->contains(path) && (!best || s->folder().size() > best->folder().size())) best = s.get();
        }
        return best;
    }

    LiveServer* start_server(const std::string& raw_folder, std::uint16_t port, bool reload) {
        const std::string folder = clean_folder_path(raw_folder);
        for (const auto& s : m_servers) {
            if (s->running() && normalized(s->folder()) == normalized(folder)) return s.get();
        }
        auto server = std::make_unique<LiveServer>();
        server->set_console(m_ws.settings.flag("server", "console", true));
        std::string error;
        if (!server->start(folder, port, m_ws.settings.get_or("server", "host", "127.0.0.1"), reload, error)) {
            notice(error);
            show_panel("output");
            return nullptr;
        }
        notice("Serving " + folder + " at " + server->url());
        m_servers.push_back(std::move(server));
        refresh_ports(true);
        if (m_console) m_console->servers_changed();
        if (m_elements) m_elements->servers_changed();
        return m_servers.back().get();
    }

    void start_server_from_bar() {
        std::string folder = m_ws.root;
        std::uint16_t port = server_port_setting();
        bool reload = m_ws.settings.flag("server", "live-reload", true);
        if (const grove::Node* pick = m_doc.find_by_id("server-folder")) {
            const std::string chosen = grove::input_string(*pick).value_or("");
            if (!chosen.empty()) folder = chosen;
        }
        if (const grove::Node* p = m_doc.find_by_id("server-port")) port = static_cast<std::uint16_t>(std::clamp<std::int64_t>(grove::input_integer(*p).value_or(port), 1, 65535));
        if (const grove::Node* r = m_doc.find_by_id("server-reload")) reload = grove::input_bool(*r).value_or(reload);
        if (folder.empty()) {
            notice("Open a folder first, then start a server for it");
            show_panel("output");
            return;
        }
        if (LiveServer* s = start_server(folder, port, reload)) {
            if (!open_in_browser(s->url())) notice("Couldn't open a browser; the server is at " + s->url());
        }
    }

    void serve_path(const std::string& path) {
        std::error_code ec;
        LiveServer* s = server_for(path);
        if (!s) {
            std::string folder = fs::is_directory(path, ec) ? path : root_of(path);
            if (folder.empty()) folder = fs::path(path).parent_path().string();
            s = start_server(folder, server_port_setting(), m_ws.settings.flag("server", "live-reload", true));
        }
        if (!s) return;
        show_panel("ports");
        const std::string url = s->url(path);
        if (!open_in_browser(url)) notice("Couldn't open a browser; the page is at " + url);
    }

    void stop_server(std::size_t index) {
        if (index >= m_servers.size()) return;
        notice("Stopped the server for " + m_servers[index]->folder());
        m_servers[index]->stop();
        if (m_console) m_console->poll();
        m_servers.erase(m_servers.begin() + static_cast<std::ptrdiff_t>(index));
        m_server_last.clear();
        refresh_ports(true);
        if (m_console) m_console->servers_changed();
        if (m_elements) m_elements->servers_changed();
    }

    void server_click(grove::Event& e) {
        const grove::Node* button = with_attribute(e.target, "data-server-action");
        if (!button) return;
        const std::size_t index = static_cast<std::size_t>(grove::parse_integer(button->attribute_or("data-server", "")).value_or(-1));
        if (index >= m_servers.size()) return;
        const std::string action = button->attribute_or("data-server-action", "");
        LiveServer& s = *m_servers[index];
        if (action == "open") {
            if (!open_in_browser(s.url())) notice("Couldn't open a browser; the server is at " + s.url());
        } else if (action == "copy") {
            if (grove::set_clipboard_text(m_doc, s.url())) set_text(m_doc, "status-problems", "Copied " + s.url());
        } else if (action == "stop") {
            stop_server(index);
        }
        e.default_prevented = true;
    }

    void poll_servers() {
        if (m_servers.empty()) return;
        LiveServer::pump();
        if (m_console) m_console->poll();
        if (m_elements) m_elements->poll();
        bool changed = false;
        for (std::size_t i = 0; i < m_servers.size(); ++i) {
            const std::vector<std::string> log = m_servers[i]->take_log();
            if (log.empty()) continue;
            m_server_last[i] = log.back();
            changed = true;
        }
        if (changed) refresh_ports(false);
    }

    void refresh_ports(bool full) {
        if (grove::Node* pick = m_doc.find_by_id("server-folder"); pick && full) {
            std::vector<grove::SelectOption> options;
            for (const std::string& root : m_roots) options.push_back(grove::SelectOption{ root, folder_label(root), false });
            if (options.empty()) options.push_back(grove::SelectOption{ "", "No folder open", false });
            const std::vector<grove::SelectOption> now = grove::select_options(*pick);
            bool same = now.size() == options.size();
            for (std::size_t i = 0; same && i < now.size(); ++i) same = now[i].value == options[i].value && now[i].label == options[i].label;
            if (!same) {
                const std::string chosen = grove::input_string(*pick).value_or("");
                grove::set_select_options(m_doc, *pick, options);
                const bool keep = std::any_of(options.begin(), options.end(), [&](const grove::SelectOption& o) { return o.value == chosen; });
                pick->set_attribute("value", keep && !chosen.empty() ? chosen : options.front().value);
            }
        }
        if (grove::Node* port = m_doc.find_by_id("server-port"); port && full && !m_ports_ready) {
            grove::set_input_integer(m_doc, *port, server_port_setting());
            if (grove::Node* reload = m_doc.find_by_id("server-reload")) grove::set_input_bool(m_doc, *reload, m_ws.settings.flag("server", "live-reload", true));
            m_ports_ready = true;
        }
        grove::Node* list = m_doc.find_by_id("servers");
        if (!list) return;
        clear(m_doc, *list);
        if (m_servers.empty()) {
            add(m_doc, *list, "p", "No local servers. Pick a folder and press Start Server to serve its HTML at http://localhost, or right-click a folder or .html file and choose Serve with Local Server.", "muted");
        }
        for (std::size_t i = 0; i < m_servers.size(); ++i) {
            const LiveServer& s = *m_servers[i];
            grove::Node* row = add(m_doc, *list, "row", "", "server-row");
            add(m_doc, *row, "text", "\xE2\x97\x8F", s.running() ? "server-dot on" : "server-dot");
            grove::Node* url = add(m_doc, *row, "link", s.url(), "server-url");
            url->set_attribute("data-server-action", "open");
            url->set_attribute("data-server", std::to_string(i));
            url->set_attribute("title", "Open in your browser");
            add(m_doc, *row, "text", folder_label(s.folder()), "server-folder-name")->set_attribute("title", s.folder());
            const std::size_t n = s.requests();
            std::string meta = std::to_string(n) + (n == 1 ? " request" : " requests");
            if (s.proxy() != 0) meta += "  \xC2\xB7  in front of your app on port " + std::to_string(s.proxy());
            if (s.live_reload()) meta += "  \xC2\xB7  live reload";
            const auto last = m_server_last.find(i);
            if (last != m_server_last.end()) meta += "  \xC2\xB7  " + last->second;
            add(m_doc, *row, "text", meta, "server-meta");
            add(m_doc, *row, "div", "", "viewer-spacer");
            for (const auto& [action, label, title] : std::vector<std::tuple<std::string, std::string, std::string>>{
                     { "open", "Open in Browser", "Open " + s.url() },
                     { "copy", "Copy URL", "Copy " + s.url() },
                     { "stop", "Stop", "Stop serving this folder" } }) {
                grove::Node* b = add(m_doc, *row, "text", label, "view-button");
                b->set_attribute("data-server-action", action);
                b->set_attribute("data-server", std::to_string(i));
                b->set_attribute("title", title);
            }
        }
        set_text(m_doc, "tab-ports", m_servers.empty() ? std::string("PORTS") : "PORTS " + std::to_string(m_servers.size()));
        m_doc.touch();
    }

    void reload_settings() {
        m_ws.diagnostics = grove::Diagnostics{};
        m_ws.settings = load_settings(m_ws.root, m_ws.diagnostics);
        m_ws.toolchain = detect_toolchain(m_ws.settings);
        for (const auto& s : m_servers) s->set_console(m_ws.settings.flag("server", "console", true));
        m_themes = load_themes(m_ws.diagnostics);
        setup_intel();
        for (OpenFile& f : m_files) {
            if (f.editor && !virtual_tab(f.path)) apply_editor_settings(*f.editor);
            if (f.editor && !virtual_tab(f.path)) schedule_semantic(f.path, 0.0);
        }
        refresh_viewers();
        apply_gui();
        refresh_theme_picker();
        if (m_settings) m_settings->refresh();
        for (const auto& t : m_terms) {
            if (t->term) t->term->set_scrollback_limit(terminal_scrollback());
            if (t->term) t->term->set_tab_width(terminal_tab_size());
        }
        refresh_terminal_ui(true);
        refresh_problems();
        refresh_status();
        build_layout();
    }

    bool is_settings_file(const std::string& path) const {
        const fs::path p(path);
        if (p.filename() == "settings.bark") return true;
        if (p.extension() != ".bark") return false;
        const std::string folder = normalized(p.parent_path().string());
        if (folder == normalized(user_config_dir()) || folder == normalized(user_themes_dir())) return true;
        return !m_ws.root.empty() && folder == normalized((fs::path(m_ws.root) / ".canopy").string());
    }

    void apply_editor_settings(grove::Node& editor) {
        editor.set_attribute("tab-size", m_ws.settings.get_or("editor", "tab-size", "4"));
        editor.set_attribute("indent", m_ws.settings.flag("editor", "insert-spaces", true) ? "spaces" : "tabs");
        editor.set_attribute("tab-backspace", m_ws.settings.flag("editor", "backspace-to-tab-stop", true) ? "true" : "false");
        editor.set_attribute("close-blocks", m_ws.settings.flag("editor", "close-blocks", true) ? "true" : "false");
        editor.set_attribute("complete-icons", m_ws.settings.flag("editor", "suggestion-icons", true) ? "true" : "false");
        editor.set_attribute("complete-kinds", m_ws.settings.flag("editor", "suggestion-kinds", true) ? "true" : "false");
        editor.set_attribute("line-numbers", m_ws.settings.flag("editor", "line-numbers", true) ? "true" : "false");
        grove::set_inline_style(m_doc, editor, "font-size", editor_font_size(m_ws.settings, m_zoom));
        const std::string family = m_ws.settings.get_or("editor", "font-family", "monospace");
        grove::set_inline_style(m_doc, editor, "font-family", family.empty() ? "" : "\"" + family + "\"");
        apply_highlight(m_doc, editor, m_ws.settings, m_themes, editor.attribute_or("language", "plain"));
        editor.set_attribute("hover-width", std::to_string(hover_pixels("hover-width", 900)));
        editor.set_attribute("hover-height", std::to_string(hover_pixels("hover-height", 560)));
        if (is_walnut_library_copy(editor.attribute_or("data-path", ""))) editor.set_attribute("readonly", "");
        const std::string language = editor.attribute_or("language", "plain");
        editor.set_attribute("autocomplete", m_ws.settings.flag("editor", "autocomplete", true) ? "true" : "false");
        editor.set_attribute("accept-on-enter", m_ws.settings.flag("editor", "accept-suggestion-on-enter", false) ? "true" : "false");
        editor.set_attribute("word-completions", m_ws.settings.flag("editor", "word-suggestions", true) ? "true" : "false");
        const std::string triggers = completion_triggers(language);
        if (triggers.empty()) editor.remove_attribute("complete-triggers");
        else editor.set_attribute("complete-triggers", triggers);
        const std::string word_chars = completion_word_chars(language);
        if (word_chars.empty()) editor.remove_attribute("word-chars");
        else editor.set_attribute("word-chars", word_chars);
        const std::string keys = editor_keymap(bindings());
        if (keys.empty()) editor.remove_attribute("keys");
        else editor.set_attribute("keys", keys);
    }

    long hover_pixels(const char* key, long fallback) const {
        const long value = static_cast<long>(grove::parse_integer(m_ws.settings.get_or("editor", key, std::to_string(fallback))).value_or(fallback));
        return std::lround(static_cast<double>(std::max(100L, value)) * m_zoom);
    }

    std::size_t hover_columns() const {
        double font = 14.0 * m_zoom;
        const std::string size = editor_font_size(m_ws.settings, m_zoom);
        if (size.size() > 2 && size.compare(size.size() - 2, 2, "px") == 0) font = std::strtod(size.c_str(), nullptr);
        const double char_w = std::max(4.0, font * 0.92 * 0.6);
        return static_cast<std::size_t>(std::max(30.0, (static_cast<double>(hover_pixels("hover-width", 900)) - 16.0) / char_w));
    }

    void apply_gui() {
        const double zoom = gui_zoom(m_ws.settings, m_doc.host().viewport_width, m_doc.host().viewport_height);
        if (grove::Node* body = m_doc.first_by_tag("body")) {
            grove::set_inline_style(m_doc, *body, "zoom", size_number(zoom));
            grove::set_inline_style(m_doc, *body, "tab-size", m_ws.settings.get_or("editor", "tab-size", "4"));
        }
        if (zoom == m_zoom) return;
        m_zoom = zoom;
        for (OpenFile& f : m_files) {
            if (f.editor && !virtual_tab(f.path)) apply_editor_settings(*f.editor);
        }
        refresh_viewers();
        if (m_settings) m_settings->refresh();
        for (const auto& t : m_terms) {
            if (t->view) apply_terminal_style(*t->view);
        }
    }

    void refresh_theme_picker() {
        grove::Node* picker = m_doc.find_by_id("theme-picker");
        if (!picker) return;
        std::vector<grove::SelectOption> options;
        for (const Theme& t : m_themes) options.push_back(grove::SelectOption{ t.id, t.builtin ? t.name : t.name + " (yours)", false });
        grove::set_select_options(m_doc, *picker, options);
        picker->set_attribute("value", theme_id(m_ws.settings));
        m_doc.touch();
    }

    void pick_theme(const std::string& id) {
        if (id.empty() || id == theme_id(m_ws.settings)) return;
        const Setting* s = m_ws.settings.find("editor", "theme");
        const std::string file = s && s->source == SettingSource::User && !s->file.empty() ? s->file : user_config_file(settings_file_for_section("editor"));
        write_setting(file, "editor", "theme", bark_setting_value(id, true));
        reload_settings();
        const Theme* theme = find_theme(m_themes, id);
        set_text(m_doc, "status-problems", "Theme: " + (theme ? theme->name : id));
    }

    std::vector<grove::CodeProblem> build_marks(const std::string& path, const std::vector<std::string>& lines) const {
        std::vector<grove::CodeProblem> out;
        std::vector<CompilerMessage> messages = m_messages;
        if (const auto web = m_web_problems.find(path); web != m_web_problems.end()) messages.insert(messages.end(), web->second.begin(), web->second.end());
        for (const CompilerMessage& m : messages) {
            if (m.line == 0 || m.line > lines.size() || m.severity == "note") continue;
            fs::path file(m.file);
            if (file.is_relative() && !m_ws.root.empty()) file = fs::path(m_ws.root) / file;
            if (normalized(file.string()) != path && normalized(m.file) != path) continue;
            const std::string& row = lines[m.line - 1];
            std::size_t begin = row.find_first_not_of(" \t");
            if (begin == std::string::npos) begin = 0;
            std::size_t end = row.size();
            if (m.column > 0 && m.column - 1 < row.size()) {
                const auto bounds = word_bounds(row, m.column - 1);
                begin = m.column - 1;
                end = bounds.second > begin ? bounds.second : begin + 1;
            }
            out.push_back({ m.line - 1, begin, std::max<std::size_t>(1, end - begin), m.severity == "warning" ? grove::CodeProblemKind::Warning : grove::CodeProblemKind::Error });
        }
        return out;
    }

    void update_marks(OpenFile& f) {
        if (!f.editor || virtual_tab(f.path)) return;
        const std::string text = grove::input_string(*f.editor).value_or("");
        const std::vector<std::string> lines = text_lines(text);
        std::vector<grove::CodeProblem> marks = build_marks(f.path, lines);
        if (const auto w = m_walnut.find(f.path); w != m_walnut.end() && m_ws.settings.flag("editor", "live-problems", true)) {
            for (const auto& p : w->second.problems) marks.push_back(p.first);
        }
        if (const auto extra = m_marks.find(f.path); extra != m_marks.end()) marks.insert(marks.end(), extra->second.begin(), extra->second.end());
        const std::string attr = grove::code_problems_text(lines, marks);
        if (attr == f.editor->attribute_or("problems", "")) return;
        if (attr.empty()) f.editor->remove_attribute("problems");
        else f.editor->set_attribute("problems", attr);
        m_doc.touch();
    }

    void update_all_marks() {
        for (OpenFile& f : m_files) update_marks(f);
    }

    void open_developer() {
        if (!find_file(k_developer_tab)) {
            grove::Node* view = begin_virtual_tab(k_developer_tab, "Developer", canopy_icon("files/settings.png"), "dev-view");
            if (!view) return;
            m_developer.build(*view);
            if (m_developer_timer) m_doc.events().clear_timer(m_developer_timer);
            m_developer_timer = m_doc.events().set_interval(1000.0, [this] { m_developer.refresh(); });
        }
        activate(k_developer_tab);
        m_doc.touch();
    }

    void open_settings() {
        if (!find_file(k_settings_tab)) {
            grove::Node* editors = m_doc.find_by_id("editors");
            grove::Node* tabs = m_doc.find_by_id("tabs");
            if (!editors || !tabs || !m_settings) return;
            grove::Node* page = m_settings->create();
            m_doc.append_child(*editors, *page);
            m_settings->show("");
            grove::Node* tab = m_doc.create_element("row");
            tab->set_attribute("class", "tab");
            tab->set_attribute("data-path", k_settings_tab);
            add_icon(m_doc, *tab, canopy_icon("files/settings.png"));
            add(m_doc, *tab, "text", "Settings", "tab-name");
            grove::Node* close = add(m_doc, *tab, "text", "\xC3\x97", "tab-close");
            close->set_attribute("data-close", k_settings_tab);
            m_doc.append_child(*tabs, *tab);
            m_files.push_back(OpenFile{ k_settings_tab, page, tab, "", false });
        }
        activate(k_settings_tab);
        m_doc.touch();
    }

    static const std::vector<std::string>& layout_views() {
        static const std::vector<std::string> views = { "explorer", "terminal", "problems", "warnings", "output", "stats", "console", "elements", "git", "history", "ports", "debug" };
        return views;
    }

    static const std::vector<std::string>& layout_areas() {
        static const std::vector<std::string> areas = { "sidebar", "panel", "aside" };
        return areas;
    }

    static std::string area_title(const std::string& area) {
        if (area == "sidebar") return "Side Bar";
        if (area == "aside") return "Secondary Side Bar";
        return "Panel";
    }

    static std::string view_title(const std::string& view) {
        if (view == "debug") return "Debug Console";
        std::string t = view;
        if (!t.empty()) t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])));
        return t;
    }

    std::string view_area(const std::string& view) const {
        const auto it = m_view_area.find(view);
        if (it != m_view_area.end()) return it->second;
        return view == "explorer" ? "sidebar" : "panel";
    }

    std::string area_position(const std::string& area) const {
        const std::string fallback = area == "sidebar" ? "left" : area == "aside" ? "right" : "bottom";
        const std::string pos = m_ws.settings.get_or("layout", area, fallback);
        if (pos == "left" || pos == "right" || pos == "top" || pos == "bottom") return pos;
        return fallback;
    }

    double area_size(const std::string& area) const {
        const double fallback = area == "sidebar" ? 250.0 : area == "aside" ? 340.0 : 230.0;
        const double v = m_ws.settings.number("layout", area + "-size").value_or(fallback);
        return std::clamp(v, 80.0, 4000.0);
    }

    bool area_has_views(const std::string& area) const {
        for (const std::string& v : layout_views()) {
            if (view_area(v) == area) return true;
        }
        return false;
    }

    bool area_showing(const std::string& view) const {
        const std::string area = view_area(view);
        const auto it = m_area_active.find(area);
        return it != m_area_active.end() && it->second == view && area_has_views(area);
    }

    static bool side_position(const std::string& pos) { return pos == "left" || pos == "right"; }

    void set_area_size(grove::Node& node, bool across, double px) {
        const std::string value = std::to_string(static_cast<long long>(std::lround(px))) + "px";
        grove::set_inline_style(m_doc, node, across ? "width" : "height", value);
        grove::set_inline_style(m_doc, node, across ? "height" : "width", "");
    }

    void clear_area_size(grove::Node& node) {
        grove::set_inline_style(m_doc, node, "width", "");
        grove::set_inline_style(m_doc, node, "height", "");
    }

    grove::Node* make_splitter(bool across, grove::Node* target, const std::string& key, int sign, bool target_across) {
        grove::Node* split = m_doc.create_element("div");
        split->set_attribute("class", across ? "splitter-x" : "splitter-y");
        split->set_attribute("data-splitter", key);
        split->set_attribute("title", "Drag to resize");
        m_splits[split] = Split{ target, key, sign, across, target_across };
        return split;
    }

    grove::Node* wrap_area(grove::Node* inner, grove::Node* area, const std::string& pos, const std::string& key) {
        const bool across = side_position(pos);
        grove::Node* box = m_doc.create_element(across ? "row" : "column");
        box->set_attribute("class", "layout-box");
        const bool first = pos == "left" || pos == "top";
        grove::Node* split = make_splitter(across, area, key, first ? 1 : -1, across);
        grove::add_class(m_doc, *inner, "layout-fill");
        grove::remove_class(m_doc, *area, "layout-fill");
        if (first) {
            m_doc.append_child(*box, *area);
            m_doc.append_child(*box, *split);
            m_doc.append_child(*box, *inner);
        } else {
            m_doc.append_child(*box, *inner);
            m_doc.append_child(*box, *split);
            m_doc.append_child(*box, *area);
        }
        return box;
    }

    void build_layout() {
        grove::Node* shell = m_doc.find_by_id("shell");
        grove::Node* main = m_doc.find_by_id("main");
        if (!shell || !main) return;
        std::map<std::string, grove::Node*> areas;
        for (const std::string& a : layout_areas()) areas[a] = m_doc.find_by_id(a);
        for (const auto& [name, node] : areas) {
            if (!node) return;
        }

        m_view_area.clear();
        for (const std::string& v : layout_views()) {
            std::string area = m_ws.settings.get_or("layout", v, v == "explorer" ? "sidebar" : "panel");
            if (std::find(layout_areas().begin(), layout_areas().end(), area) == layout_areas().end()) area = v == "explorer" ? "sidebar" : "panel";
            m_view_area[v] = area;
        }
        for (const std::string& v : layout_views()) {
            const std::string area = m_view_area[v];
            grove::Node* tab = m_doc.find_by_id("tab-" + v);
            grove::Node* view = m_doc.find_by_id("view-" + v);
            grove::Node* tabs = m_doc.find_by_id(area + "-tabs");
            if (tab && tabs) m_doc.append_child(*tabs, *tab);
            if (view) m_doc.append_child(*areas[area], *view);
        }

        m_splits.clear();
        for (grove::Node* n : { main, areas["sidebar"], areas["panel"], areas["aside"] }) {
            m_doc.append_child(*shell, *n);
            grove::remove_class(m_doc, *n, "layout-fill");
            clear_area_size(*n);
        }
        std::vector<grove::Node*> stale;
        for (const auto& child : shell->children()) {
            grove::Node* n = child.get();
            if (n != main && n != areas["sidebar"] && n != areas["panel"] && n != areas["aside"]) stale.push_back(n);
        }
        for (grove::Node* n : stale) m_doc.remove(*n);

        std::map<std::string, bool> shown;
        for (const std::string& a : layout_areas()) {
            shown[a] = area_has_views(a);
            if (shown[a]) grove::remove_class(m_doc, *areas[a], "hidden");
            else grove::add_class(m_doc, *areas[a], "hidden");
            for (const char* p : { "area-left", "area-right", "area-top", "area-bottom" }) grove::remove_class(m_doc, *areas[a], p);
            grove::add_class(m_doc, *areas[a], "area-" + area_position(a));
        }

        const std::string side = area_position("sidebar");
        const std::string bottom = area_position("panel");
        const std::string aside = area_position("aside");
        const bool span = m_ws.settings.get_or("layout", "panel-span", "editor") == "all";
        grove::Node* inner = main;
        auto place = [&](const std::string& area, const std::string& pos) {
            if (!shown[area]) return;
            set_area_size(*areas[area], side_position(pos), area_size(area));
            inner = wrap_area(inner, areas[area], pos, area);
        };
        if (shown["sidebar"] && shown["panel"] && side == bottom) {
            place("aside", aside);
            const bool across = side_position(side);
            grove::Node* strip = m_doc.create_element(across ? "column" : "row");
            strip->set_attribute("class", "layout-strip");
            set_area_size(*areas["sidebar"], !across, area_size("sidebar"));
            grove::Node* inner_split = make_splitter(!across, areas["sidebar"], "sidebar", 1, !across);
            m_doc.append_child(*strip, *areas["sidebar"]);
            m_doc.append_child(*strip, *inner_split);
            m_doc.append_child(*strip, *areas["panel"]);
            grove::add_class(m_doc, *areas["panel"], "layout-fill");
            set_area_size(*strip, across, area_size("panel"));
            inner = wrap_area(inner, strip, side, "panel");
        } else if (span) {
            place("aside", aside);
            place("sidebar", side);
            place("panel", bottom);
        } else {
            place("panel", bottom);
            place("aside", aside);
            place("sidebar", side);
        }
        if (inner != main) m_doc.insert_before(*shell, *inner, shell->children().empty() ? nullptr : shell->children().front().get());
        grove::add_class(m_doc, *inner, "layout-fill");

        for (const std::string& v : layout_views()) {
            grove::Node* view = m_doc.find_by_id("view-" + v);
            if (!view) continue;
            const std::string area = m_view_area[v];
            const bool narrow = side_position(area_position(area)) && !(area == "panel" && side == bottom && shown["sidebar"] && !side_position(side));
            narrow ? (void)grove::add_class(m_doc, *view, "narrow") : (void)grove::remove_class(m_doc, *view, "narrow");
        }
        if (grove::Node* pickers = m_doc.find_by_id("git-pickers")) {
            grove::Node* bar = m_doc.find_by_id("git-bar");
            grove::Node* view = m_doc.find_by_id("view-git");
            const bool narrow = view && view->has_class("narrow");
            grove::Node* repo = m_doc.find_by_id("git-repo");
            grove::Node* branch = m_doc.find_by_id("git-branch");
            if (bar && repo && branch) {
                if (narrow) {
                    m_doc.append_child(*pickers, *repo);
                    m_doc.append_child(*pickers, *branch);
                    grove::remove_class(m_doc, *pickers, "hidden");
                } else {
                    grove::Node* first = bar->children().empty() ? nullptr : bar->children().front().get();
                    m_doc.insert_before(*bar, *branch, first);
                    m_doc.insert_before(*bar, *repo, branch);
                    grove::add_class(m_doc, *pickers, "hidden");
                }
            }
        }

        for (const std::string& a : layout_areas()) {
            if (!shown[a]) continue;
            std::string active = m_area_active.count(a) ? m_area_active[a] : std::string();
            if (active.empty() || view_area(active) != a) {
                active.clear();
                for (const std::string& v : layout_views()) {
                    if (view_area(v) != a) continue;
                    if (active.empty() || (a == "panel" && v == "problems")) active = v;
                }
            }
            activate_view(active, false);
        }
        if (m_git) m_git->set_visible(area_showing("git"), area_showing("history"));
        m_doc.touch();
    }

    void write_layout(const std::string& key, const std::string& value) {
        const Setting* s = m_ws.settings.find("layout", key);
        const std::string file = s && s->source == SettingSource::User && !s->file.empty() ? s->file : user_config_file(settings_file_for_section("layout"));
        write_setting(file, "layout", key, value);
        reload_settings();
    }

    void move_view(const std::string& view, const std::string& area) {
        if (view_area(view) == area) return;
        m_area_active[area] = view;
        write_layout(view, area);
        set_text(m_doc, "status-problems", view_title(view) + " moved to the " + area_title(area));
    }

    void tab_menu(grove::Event& e) {
        const grove::Node* tab = with_attribute(e.target, "data-view");
        const grove::Node* row = with_attribute(e.target, "data-area");
        if (!row) return;
        grove::Node* owner = m_doc.find_by_id(row->attribute_or("data-area", "panel") + "-tabs");
        if (!owner) return;
        m_tab_menu_view = tab ? tab->attribute_or("data-view", "") : std::string();
        m_tab_menu_area = row->attribute_or("data-area", "panel");
        std::vector<grove::MenuItem> items;
        if (!m_tab_menu_view.empty()) {
            for (const std::string& a : layout_areas()) {
                if (a == view_area(m_tab_menu_view)) continue;
                items.push_back(grove::menu_item("move:" + a, "Move " + view_title(m_tab_menu_view) + " to the " + area_title(a)));
            }
            items.push_back(grove::menu_separator());
        }
        const std::string here = area_position(m_tab_menu_area);
        for (const char* pos : { "left", "right", "top", "bottom" }) {
            items.push_back(grove::menu_item(std::string("place:") + pos, "Put the " + area_title(m_tab_menu_area) + " on the " + pos, here == pos ? "\xE2\x9C\x93" : std::string(), here == pos));
        }
        if (m_tab_menu_area == "panel") {
            const bool span = m_ws.settings.get_or("layout", "panel-span", "editor") == "all";
            items.push_back(grove::menu_item("span", span ? "Panel Beside the Side Bars" : "Panel Across the Whole Window"));
        }
        items.push_back(grove::menu_separator());
        items.push_back(grove::menu_item("reset", "Reset Layout"));
        grove::open_menu(m_doc, *owner, items, e.x, e.y);
        e.default_prevented = true;
    }

    void tab_menu_action(const std::string& action) {
        if (action.rfind("move:", 0) == 0) {
            if (!m_tab_menu_view.empty()) move_view(m_tab_menu_view, action.substr(5));
        } else if (action.rfind("place:", 0) == 0) {
            write_layout(m_tab_menu_area, action.substr(6));
        } else if (action == "span") {
            const bool span = m_ws.settings.get_or("layout", "panel-span", "editor") == "all";
            write_layout("panel-span", span ? "editor" : "all");
        } else if (action == "reset") {
            confirm("Reset layout", "Put the side bar, panel and every tab back where they started, at their first sizes?", [this] {
                for (const std::string& v : layout_views()) write_setting_quiet("layout", v, v == "explorer" ? "sidebar" : "panel");
                for (const auto& [k, v] : std::vector<std::pair<std::string, std::string>>{ { "sidebar", "left" }, { "panel", "bottom" }, { "aside", "right" }, { "panel-span", "editor" }, { "sidebar-size", "250px" }, { "panel-size", "230px" }, { "aside-size", "340px" } }) write_setting_quiet("layout", k, v);
                m_area_active.clear();
                reload_settings();
            });
        }
    }

    void write_setting_quiet(const std::string& section, const std::string& key, const std::string& value) {
        const Setting* s = m_ws.settings.find(section, key);
        const std::string file = s && s->source == SettingSource::User && !s->file.empty() ? s->file : user_config_file(settings_file_for_section(section));
        write_setting(file, section, key, value);
    }

    void layout_mouse_down(grove::Event& e) {
        if (e.button != 1) return;
        if (const grove::Node* split = with_attribute(e.target, "data-splitter")) {
            const auto it = m_splits.find(split);
            if (it == m_splits.end()) return;
            m_split_drag = it->second;
            m_split_drag.node = split;
            m_split_drag.active = true;
            m_split_drag.start = it->second.across ? e.x : e.y;
            m_split_drag.size = area_size(it->second.key);
            grove::add_class(m_doc, *const_cast<grove::Node*>(split), "dragging");
            if (grove::Node* body = m_doc.first_by_tag("body")) grove::add_class(m_doc, *body, it->second.across ? "resizing-x" : "resizing-y");
            e.default_prevented = true;
            return;
        }
        if (const grove::Node* tab = with_attribute(e.target, "data-view"); tab && with_attribute(e.target, "data-area")) {
            m_tab_drag = TabDrag{ tab->attribute_or("data-view", ""), e.x, e.y, true, false };
        }
    }

    void layout_mouse_move(grove::Event& e) {
        if (m_split_drag.active && m_split_drag.target) {
            const double zoom = m_zoom > 0.0 ? m_zoom : 1.0;
            const double pos = m_split_drag.across ? e.x : e.y;
            const double size = std::clamp(m_split_drag.size + m_split_drag.sign * (pos - m_split_drag.start) / zoom, 80.0, 4000.0);
            m_split_drag.current = size;
            set_area_size(*m_split_drag.target, m_split_drag.target_across, size);
            m_doc.touch();
            return;
        }
        if (m_tab_drag.active) {
            if (!m_tab_drag.started && std::hypot(e.x - m_tab_drag.x, e.y - m_tab_drag.y) > 8.0) {
                m_tab_drag.started = true;
                if (grove::Node* body = m_doc.first_by_tag("body")) grove::add_class(m_doc, *body, "moving-tab");
                set_text(m_doc, "status-problems", "Drop " + view_title(m_tab_drag.view) + " on another tab bar to move it there");
            }
            if (m_tab_drag.started) {
                const grove::Node* over = with_attribute(e.target, "data-area");
                const std::string area = over ? over->attribute_or("data-area", "") : std::string();
                for (const std::string& a : layout_areas()) {
                    if (grove::Node* tabs = m_doc.find_by_id(a + "-tabs")) a == area ? (void)grove::add_class(m_doc, *tabs, "drop-here") : (void)grove::remove_class(m_doc, *tabs, "drop-here");
                }
            }
        }
    }

    void layout_mouse_up(grove::Event& e) {
        if (m_split_drag.active) {
            const Split done = m_split_drag;
            m_split_drag = Split{};
            if (done.node) grove::remove_class(m_doc, *const_cast<grove::Node*>(done.node), "dragging");
            if (grove::Node* body = m_doc.first_by_tag("body")) {
                grove::remove_class(m_doc, *body, "resizing-x");
                grove::remove_class(m_doc, *body, "resizing-y");
            }
            if (done.current > 0.0) write_setting_quiet("layout", done.key + "-size", std::to_string(static_cast<long long>(std::lround(done.current))) + "px");
            m_ws.settings = load_settings(m_ws.root, m_ws.diagnostics);
            m_doc.touch();
            return;
        }
        if (m_tab_drag.active) {
            const TabDrag drag = m_tab_drag;
            m_tab_drag = TabDrag{};
            if (grove::Node* body = m_doc.first_by_tag("body")) grove::remove_class(m_doc, *body, "moving-tab");
            for (const std::string& a : layout_areas()) {
                if (grove::Node* tabs = m_doc.find_by_id(a + "-tabs")) grove::remove_class(m_doc, *tabs, "drop-here");
            }
            if (!drag.started) return;
            const grove::Node* over = with_attribute(e.target, "data-area");
            if (!over) {
                refresh_status();
                return;
            }
            move_view(drag.view, over->attribute_or("data-area", ""));
        }
    }

    void setup_find() {
        FindBar::Hooks hooks;
        hooks.editor = [this]() -> grove::Node* {
            const OpenFile* f = find_file(m_active);
            return f && f->editor && !virtual_tab(f->path) ? f->editor : nullptr;
        };
        hooks.focus_editor = [this] {
            const OpenFile* f = find_file(m_active);
            if (f && f->editor && m_doc.host().focus) m_doc.host().focus(f->editor);
        };
        m_find = std::make_unique<FindBar>(m_doc, std::move(hooks));
        m_find->install();
    }

    void setup_console() {
        ConsolePanel::Hooks hooks;
        hooks.servers = [this] {
            std::vector<LiveServer*> out;
            for (const auto& s : m_servers) out.push_back(s.get());
            return out;
        };
        hooks.open_at = [this](const std::string& path, int line, int column) {
            open_file(path, static_cast<std::size_t>(std::max(1, line)), static_cast<std::size_t>(std::max(1, column)));
        };
        hooks.show_panel = [this](const std::string& name) { show_panel(name); };
        hooks.serve_workspace = [this] {
            show_panel("ports");
            start_server_from_bar();
        };
        m_console = std::make_unique<ConsolePanel>(m_doc, std::move(hooks));
        m_console->install();
        setup_elements();
    }

    void setup_elements() {
        ElementsPanel::Hooks hooks;
        hooks.servers = [this] {
            std::vector<LiveServer*> out;
            for (const auto& s : m_servers) out.push_back(s.get());
            return out;
        };
        hooks.visible = [this] { return area_showing("elements"); };
        hooks.serve_workspace = [this] {
            show_panel("ports");
            start_server_from_bar();
        };
        hooks.style_editor = [this](grove::Node& editor) {
            apply_editor_settings(editor);
            editor.set_attribute("line-numbers", "false");
            editor.set_attribute("autocomplete", "false");
        };
        m_elements = std::make_unique<ElementsPanel>(m_doc, std::move(hooks));
        m_elements->install();
    }

    void setup_git() {
        GitPanel::Hooks hooks;
        hooks.open_file = [this](const std::string& path) { open_file(path); };
        hooks.open_diff = [this](const std::string& key, const std::string& title, const std::string& subtitle, const std::string& file, const std::vector<DiffLine>& lines) { open_diff_tab(key, title, subtitle, file, lines); };
        hooks.open_change_diff = [this](const std::string& key, const std::string& title, const std::string& subtitle, const std::string& file, const std::vector<DiffLine>& lines, const std::string& mode) { open_diff_tab(key, title, subtitle, file, lines, mode); };
        hooks.close_diff = [this](const std::string& key) {
            if (find_file("canopy:diff:" + key)) close_file("canopy:diff:" + key);
        };
        hooks.roots = [this] { return m_roots.empty() ? std::vector<std::string>{ m_ws.root } : m_roots; };
        hooks.open_folder = [this](const std::string& path) { open_folder(path); };
        hooks.setting = [this](const std::string& section, const std::string& key, const std::string& fallback) { return m_ws.settings.get_or(section, key, fallback); };
        hooks.decorations_changed = [this] { refresh_explorer(); };
        hooks.open_version = [this](const std::string& key, const std::string& title, const std::string& file, const std::string& text) { open_version_tab(key, title, file, text); };
        hooks.output = [this](const std::string& text) {
            m_output += text;
            trim_output();
            sync_output();
        };
        hooks.show_output = [this] { show_panel("output"); };
        hooks.show_panel = [this](const std::string& name) { show_panel(name); };
        hooks.active_file = [this] { return m_active; };
        hooks.file_text = [this](const std::string& path) -> std::optional<std::string> {
            const OpenFile* f = find_file(normalized(path));
            if (!f || !f->editor || virtual_tab(f->path)) return std::nullopt;
            return grove::input_string(*f->editor).value_or("");
        };
        hooks.restore_file = [this](const std::string& path, const std::string& text) { return restore_file(path, text); };
        hooks.save_all = [this] { save_all(); };
        hooks.files_changed = [this] { reload_from_disk(); };
        hooks.refresh_seconds = [this] { return m_ws.settings.number("git", "refresh-seconds").value_or(4.0); };
        set_git_askpass(canopy_app_executable());
        m_git = std::make_unique<GitPanel>(m_doc, m_history, std::move(hooks));
        if (grove::Node* item = m_doc.find_by_id("status-git")) {
            if (grove::Node* label = m_doc.find_by_id("status-branch")) {
                grove::Node* img = m_doc.create_element("img");
                img->set_attribute("src", canopy_icon("files/git.png"));
                img->set_attribute("width", "14");
                img->set_attribute("class", "file-icon");
                m_doc.insert_before(*item, *img, label);
            }
            m_doc.events().add_listener(item, "click", [this](grove::Event&) { show_panel("git"); });
        }
        m_git->install();
        m_git->set_root(m_ws.root);
    }

    void record_history(const std::string& path, const std::string& before, const std::string& text) {
        if (!m_ws.settings.flag("history", "enabled", true)) return;
        const double limit = m_ws.settings.number("history", "limit").value_or(50.0);
        m_history.set_limit(static_cast<std::size_t>(std::clamp(limit, 1.0, 10000.0)));
        if (before != text && m_history.entries(path).empty()) m_history.record(path, before, "Before your first save");
        m_history.record(path, text, "Saved");
    }

    bool restore_file(const std::string& path, const std::string& text) {
        if (!write_text_file(path, text)) return false;
        if (m_ws.settings.flag("history", "enabled", true)) m_history.record(path, text, "Restored");
        if (OpenFile* f = find_file(normalized(path)); f && f->editor && !virtual_tab(f->path)) {
            f->editor->set_attribute("value", text);
            f->saved = text;
            set_dirty(*f, false);
            schedule_semantic(f->path, 0.0);
            if (f->kind == ViewerKind::Markdown || f->kind == ViewerKind::Math) m_markdown_due[f->path] = now_seconds();
        }
        for (const auto& s : m_servers) s->changed(path);
        m_doc.touch();
        return true;
    }

    void reload_from_disk() {
        for (OpenFile& f : m_files) {
            if (!f.editor || virtual_tab(f.path) || f.dirty) continue;
            const std::optional<std::string> text = read_text_file(f.path);
            if (!text || *text == f.saved) continue;
            f.editor->set_attribute("value", *text);
            f.saved = *text;
            schedule_semantic(f.path, 0.0);
            if (f.kind == ViewerKind::Markdown || f.kind == ViewerKind::Math) m_markdown_due[f.path] = now_seconds();
        }
        refresh_explorer();
        m_doc.touch();
    }

    static std::string padded_number(long long n, std::size_t width) {
        std::string t = n > 0 ? std::to_string(n) : std::string();
        if (t.size() < width) t.insert(0, width - t.size(), ' ');
        return t;
    }

    grove::Node* begin_virtual_tab(const std::string& path, const std::string& title, const std::string& icon, const std::string& classes) {
        if (find_file(path)) close_file(path);
        grove::Node* editors = m_doc.find_by_id("editors");
        if (!editors || !m_doc.find_by_id("tabs")) return nullptr;
        grove::Node* view = m_doc.create_element("column");
        view->set_attribute("class", classes + " hidden");
        m_doc.append_child(*editors, *view);
        virtual_titles()[path] = title;
        grove::Node* tab = create_tab(path, icon, title);
        tab->set_attribute("title", title);
        add_open_file(OpenFile{ path, nullptr, tab, "", false, view, ViewerKind::Text });
        return view;
    }

    void open_diff_tab(const std::string& key, const std::string& title, const std::string& subtitle, const std::string& file, const std::vector<DiffLine>& lines, const std::string& mode = std::string()) {
        const std::string path = "canopy:diff:" + key;
        grove::Node* view = begin_virtual_tab(path, title, canopy_icon(file.empty() ? "files/git.png" : "files/diff.png"), "diff-view");
        if (!view) return;
        std::size_t added = 0, removed = 0;
        long long widest = 1;
        for (const DiffLine& d : lines) {
            if (d.kind == DiffLine::Kind::Added) ++added;
            if (d.kind == DiffLine::Kind::Removed) ++removed;
            widest = std::max({ widest, d.old_line, d.new_line });
        }
        const std::size_t width = std::to_string(widest).size();
        grove::Node* bar = add(m_doc, *view, "row", "", "diff-bar");
        add(m_doc, *bar, "text", title, "diff-title");
        if (!subtitle.empty()) add(m_doc, *bar, "text", subtitle, "diff-sub");
        add(m_doc, *bar, "text", "+" + std::to_string(added), "diff-count add");
        add(m_doc, *bar, "text", "\xE2\x88\x92" + std::to_string(removed), "diff-count del");
        add(m_doc, *bar, "div", "", "viewer-spacer");
        std::error_code ec;
        const bool can_open = !file.empty() && fs::is_regular_file(file, ec);
        auto bar_button = [&](const std::string& label, const std::string& act, const std::string& tip, bool danger) {
            grove::Node* b = add(m_doc, *bar, "text", label, danger ? "view-button danger" : "view-button");
            b->set_attribute("data-diff-action", act);
            b->set_attribute("title", tip);
        };
        if (mode == "unstaged" || mode == "binary") {
            bar_button("Stage File", "stage-file", "Stage every change in this file", false);
            bar_button("Discard File", "discard-file", "Throw away the unstaged changes in this file (kept in local history)", true);
        } else if (mode == "untracked") {
            bar_button("Stage File", "stage-file", "Start tracking this file", false);
            bar_button("Delete File", "discard-file", "Delete this new file (kept in local history)", true);
        } else if (mode == "staged" || mode == "staged-binary") {
            bar_button("Unstage File", "unstage-file", "Take this file out of the next commit", false);
        }
        if (can_open) {
            grove::Node* open = add(m_doc, *bar, "text", "Open File", "view-button");
            open->set_attribute("data-diff-open", file);
        }
        grove::Node* body = add(m_doc, *view, "column", "", "diff-body");
        const std::size_t tab_size = static_cast<std::size_t>(m_ws.settings.number("editor", "tab-size").value_or(4.0));
        std::size_t shown = 0;
        std::size_t hunk_index = 0;
        for (const DiffLine& d : lines) {
            if (++shown > 8000) {
                add(m_doc, *body, "p", std::to_string(lines.size() - 8000) + " more lines aren't shown.", "muted");
                break;
            }
            const std::string text = grove::code_expand_tabs(d.text, std::max<std::size_t>(1, tab_size));
            std::string row;
            std::string kind;
            switch (d.kind) {
            case DiffLine::Kind::Added:
                row = padded_number(0, width) + " " + padded_number(d.new_line, width) + " + " + text;
                kind = "diff-line diff-add";
                break;
            case DiffLine::Kind::Removed:
                row = padded_number(d.old_line, width) + " " + padded_number(0, width) + " - " + text;
                kind = "diff-line diff-del";
                break;
            case DiffLine::Kind::Context:
                row = padded_number(d.old_line, width) + " " + padded_number(d.new_line, width) + "   " + text;
                kind = "diff-line";
                break;
            case DiffLine::Kind::Hunk:
                row = d.text;
                kind = "diff-line diff-hunk";
                break;
            case DiffLine::Kind::File:
                row = d.text;
                kind = "diff-line diff-file";
                break;
            case DiffLine::Kind::Meta:
                row = d.text;
                kind = "diff-line diff-meta";
                break;
            }
            if (d.kind == DiffLine::Kind::Hunk && (mode == "unstaged" || mode == "staged")) {
                grove::Node* hunk_row = add(m_doc, *body, "row", "", "diff-hunk-row");
                hunk_row->set_attribute("data-hunk", std::to_string(hunk_index));
                add(m_doc, *hunk_row, "pre", row, "diff-line diff-hunk");
                add(m_doc, *hunk_row, "div", "", "viewer-spacer");
                auto hunk_button = [&](const std::string& label, const std::string& act, const std::string& tip, bool danger) {
                    grove::Node* b = add(m_doc, *hunk_row, "text", label, danger ? "view-button danger" : "view-button");
                    b->set_attribute("data-diff-action", act);
                    b->set_attribute("title", tip);
                };
                if (mode == "unstaged") {
                    hunk_button("Stage", "stage-hunk", "Stage just this block of changes", false);
                    hunk_button("Revert", "revert-hunk", "Put this block back the way it is in the index (kept in local history)", true);
                } else {
                    hunk_button("Unstage", "unstage-hunk", "Take just this block out of the next commit", false);
                }
                ++hunk_index;
                continue;
            }
            if (d.kind == DiffLine::Kind::Hunk) ++hunk_index;
            grove::Node* line = add(m_doc, *body, "pre", row.empty() ? std::string(" ") : row, kind);
            if (can_open && d.new_line > 0) line->set_attribute("data-line", std::to_string(d.new_line));
        }
        if (lines.empty()) add(m_doc, *body, "p", "No differences.", "muted");
        m_doc.events().add_listener(view, "click", [this, file, key](grove::Event& e) {
            if (const grove::Node* act = with_attribute(e.target, "data-diff-action")) {
                const grove::Node* hunk = with_attribute(e.target, "data-hunk");
                const std::size_t index = hunk ? static_cast<std::size_t>(grove::parse_integer(hunk->attribute_or("data-hunk", "0")).value_or(0)) : 0;
                if (m_git) m_git->diff_action(key, index, act->attribute_or("data-diff-action", ""));
                return;
            }
            if (const grove::Node* open = with_attribute(e.target, "data-diff-open")) {
                open_file(open->attribute_or("data-diff-open", ""));
                return;
            }
            if (file.empty()) return;
            if (const grove::Node* line = with_attribute(e.target, "data-line")) {
                if (!grove::selected_text(m_doc).empty()) return;
                open_file(file, static_cast<std::size_t>(grove::parse_integer(line->attribute_or("data-line", "1")).value_or(1)), 1);
            }
        });
        activate(path);
        m_doc.touch();
    }

    void open_version_tab(const std::string& key, const std::string& title, const std::string& file, const std::string& text) {
        const std::string path = "canopy:show:" + key;
        grove::Node* view = begin_virtual_tab(path, title, file_icon(file, false, false), "version-view");
        if (!view) return;
        grove::Node* bar = add(m_doc, *view, "row", "", "diff-bar");
        add(m_doc, *bar, "text", title, "diff-title");
        add(m_doc, *bar, "text", "read-only", "diff-sub");
        add(m_doc, *bar, "div", "", "viewer-spacer");
        grove::Node* editor = m_doc.create_element("input_code");
        editor->set_attribute("class", "editor version-editor");
        editor->set_attribute("language", code_language_name(file));
        apply_editor_settings(*editor);
        editor->set_attribute("readonly", "true");
        editor->set_attribute("value", text);
        m_doc.append_child(*view, *editor);
        activate(path);
        m_doc.touch();
    }

    void notice(const std::string& message) {
        m_output += message + "\n";
        sync_output();
    }

    static double now_seconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool walnut_file(const OpenFile& f) const {
        return f.editor && !virtual_tab(f.path) && f.editor->attribute_or("language", "") == "walnut";
    }

    void schedule_semantic(const std::string& path, double delay) {
        const OpenFile* f = find_file(path);
        if (f && f->editor && !virtual_tab(path) && live_checked(f->editor->attribute_or("language", ""))) {
            m_web_due[path] = now_seconds() + delay;
            return;
        }
        if (!f || !walnut_file(*f)) return;
        m_semantic_due[path] = now_seconds() + delay;
    }

    void clear_semantic(OpenFile& f) {
        if (f.editor->has_attribute("semantic")) {
            f.editor->remove_attribute("semantic");
            m_doc.touch();
        }
        m_walnut.erase(f.path);
        update_marks(f);
    }

    void poll_semantic() {
        if (std::optional<SemanticJob::Done> done = m_semantic.take()) finish_semantic(*done);
        if (m_semantic.running() || m_semantic_due.empty()) return;
        const double now = now_seconds();
        std::string pick;
        for (const auto& [path, due] : m_semantic_due) {
            if (due > now) continue;
            if (pick.empty() || path == m_active) pick = path;
        }
        if (pick.empty()) return;
        m_semantic_due.erase(pick);
        OpenFile* f = find_file(pick);
        if (!f || !walnut_file(*f)) return;
        const bool colors = m_ws.settings.flag("editor", "semantic-highlighting", true);
        const bool problems = m_ws.settings.flag("editor", "live-problems", true);
        const Tool* walnut = m_ws.toolchain.get("walnut");
        if ((!colors && !problems) || !walnut) {
            clear_semantic(*f);
            if (m_live.erase(pick)) refresh_problems();
            return;
        }
        const std::string text = grove::input_string(*f->editor).value_or("");
        m_semantic.start(pick, walnut_highlight_command(walnut->path, pick, m_ws.settings.get_or("language.walnut", "flags", "") + (m_ws.settings.flag("language.walnut", "ownership", false) ? " --ownership" : "")), text);
    }

    void finish_semantic(const SemanticJob::Done& done) {
        OpenFile* f = find_file(done.path);
        if (!f || !walnut_file(*f)) return;
        const SemanticResult result = parse_walnut_highlight(done.output, done.text, done.path, false);
        if (!result.ok) return;
        const bool colors = m_ws.settings.flag("editor", "semantic-highlighting", true);
        const bool problems = m_ws.settings.flag("editor", "live-problems", true);
        const std::vector<std::string> lines = text_lines(done.text);
        const std::string spans = colors ? grove::code_spans_text(lines, result.spans) : std::string();
        if (spans.empty()) f->editor->remove_attribute("semantic");
        else f->editor->set_attribute("semantic", spans);
        if (problems && !result.messages.empty()) m_live[done.path] = result.messages;
        else m_live.erase(done.path);
        WalnutInfo& info = m_walnut[done.path];
        info.hovers = result.hovers;
        info.links = result.links;
        info.problems.clear();
        for (std::size_t i = 0; i < result.problems.size() && i < result.messages.size(); ++i) info.problems.push_back({ result.problems[i], result.messages[i] });
        if (OpenFile* again = find_file(done.path)) update_marks(*again);
        std::vector<grove::CodeSpan> link_spans;
        for (const WalnutLink& link : result.links) link_spans.push_back(grove::CodeSpan{ link.line, link.column, link.length, grove::CodeTokenKind::Plain });
        const std::string links = grove::code_spans_text(lines, link_spans);
        if (links.empty()) f->editor->remove_attribute("links");
        else f->editor->set_attribute("links", links);
        refresh_problems();
        m_doc.touch();
    }

    static bool live_checked(const std::string& language) {
        return is_web_language(language) || language == "python" || language == "lua" || language == "zig";
    }

    void poll_web_checks() {
        if (std::optional<SemanticJob::Done> done = m_web_check.take()) {
            const std::string source = m_web_check_source;
            m_web_check_source.clear();
            const OpenFile* f = find_file(done->path);
            if (f && f->editor && grove::input_string(*f->editor).value_or("") == source) {
                const std::string language = f->editor->attribute_or("language", "");
                if (is_web_language(language)) {
                    m_web_syntax[done->path] = parse_node_check(done->output, done->path);
                } else {
                    std::vector<CompilerMessage> found = parse_compiler_messages(done->output);
                    for (CompilerMessage& m : found) m.file = done->path;
                    if (found.size() > 1) found.resize(1);
                    m_web_syntax[done->path] = found;
                }
                publish_web_problems(done->path);
            }
        }
        if (m_web_due.empty()) return;
        const double now = now_seconds();
        std::string pick;
        for (const auto& [path, due] : m_web_due) {
            if (due > now) continue;
            if (pick.empty() || path == m_active) pick = path;
        }
        if (pick.empty()) return;
        m_web_due.erase(pick);
        OpenFile* f = find_file(pick);
        if (!f || !f->editor) return;
        const std::string language = f->editor->attribute_or("language", "");
        if (!m_ws.settings.flag("editor", "live-problems", true)) {
            m_web_lint.erase(pick);
            m_web_syntax.erase(pick);
            publish_web_problems(pick);
            return;
        }
        const std::string text = grove::input_string(*f->editor).value_or("");
        if (!is_web_language(language)) {
            std::vector<std::string> command;
            if (language == "python") {
                if (const Tool* python = m_ws.toolchain.get("python")) command = { python->path, "-c", "import ast,sys;ast.parse(sys.stdin.read(),sys.argv[1])", pick };
            } else if (language == "lua") {
                if (const Tool* luac = m_ws.toolchain.get("luac")) command = { luac->path, "-p", "-" };
                else if (const Tool* lua = m_ws.toolchain.get("lua")) command = { lua->path, "-e", "local f,e=load(io.read('a'),'=stdin') if not f then io.stderr:write('lua: '..e..'\\n') os.exit(1) end" };
            } else if (language == "zig") {
                if (const Tool* zig = m_ws.toolchain.get("zig")) command = { zig->path, "ast-check", "--color", "off" };
            }
            m_web_lint.erase(pick);
            if (command.empty()) {
                m_web_syntax.erase(pick);
            } else if (m_web_check.running()) {
                m_web_due[pick] = now + 0.3;
            } else {
                m_web_check_source = text;
                m_web_check.start_with_errors(pick, command, text);
            }
            publish_web_problems(pick);
            return;
        }
        m_web_lint[pick] = lint_web(language, text, pick);
        std::string js;
        bool module = false;
        if (language == "javascript") {
            js = text;
            module = javascript_is_module(text, pick);
        } else if (language == "html") {
            js = web_script_text(text, module);
        }
        const Tool* node = m_ws.toolchain.get("node");
        if (js.empty() || !node) {
            m_web_syntax.erase(pick);
        } else if (m_web_check.running()) {
            m_web_due[pick] = now + 0.3;
        } else {
            std::vector<std::string> command = { node->path, "--check" };
            if (module) command.push_back("--input-type=module");
            m_web_check_source = text;
            m_web_check.start_with_errors(pick, command, js);
        }
        publish_web_problems(pick);
    }

    void publish_web_problems(const std::string& path) {
        std::vector<CompilerMessage> all;
        if (const auto it = m_web_lint.find(path); it != m_web_lint.end()) all = it->second;
        if (const auto it = m_web_syntax.find(path); it != m_web_syntax.end()) all.insert(all.end(), it->second.begin(), it->second.end());
        const auto before = m_web_problems.find(path);
        bool same = before != m_web_problems.end() && before->second.size() == all.size();
        for (std::size_t i = 0; same && i < all.size(); ++i) same = all[i].line == before->second[i].line && all[i].column == before->second[i].column && all[i].message == before->second[i].message;
        if (same || (all.empty() && before == m_web_problems.end())) return;
        if (all.empty()) {
            m_web_problems.erase(path);
            m_live.erase(path);
        } else {
            m_web_problems[path] = all;
            m_live[path] = all;
        }
        if (OpenFile* f = find_file(path)) update_marks(*f);
        refresh_problems();
    }

    struct WalnutInfo {
        std::vector<WalnutHover> hovers;
        std::vector<WalnutLink> links;
        std::vector<std::pair<grove::CodeProblem, CompilerMessage>> problems;
    };

    struct Place {
        std::string path;
        std::size_t line = 0;
        std::size_t column = 0;
    };

    static bool intel_language(const std::string& language) {
        return language == "c" || language == "cpp" || language == "cmake" || language == "bark" || language == "grove";
    }

    static std::vector<std::string> top_sections(const std::string& text) {
        std::vector<std::string> out;
        int depth = 0;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            const std::size_t brace = line.find('{');
            if (depth == 0 && brace != std::string::npos) {
                std::string head = line.substr(0, brace);
                while (!head.empty() && std::isspace(static_cast<unsigned char>(head.back()))) head.pop_back();
                while (!head.empty() && std::isspace(static_cast<unsigned char>(head.front()))) head.erase(head.begin());
                if (!head.empty() && head.rfind("//", 0) != 0) out.push_back(head);
            }
            for (char c : line) {
                if (c == '{') ++depth;
                else if (c == '}') depth = std::max(0, depth - 1);
            }
        }
        return out;
    }

    bool settings_section(const std::string& section) const {
        if (section == "theme") return true;
        const std::vector<std::string> known = m_ws.settings.known_sections();
        return std::find(known.begin(), known.end(), section) != known.end();
    }

    bool settings_bark_file(const std::string& file, const std::string& text) const {
        const fs::path p(file);
        const std::string name = p.filename().string();
        const std::string folder = p.parent_path().filename().string();
        if (path_inside(normalized(file), normalized(user_config_dir())) || folder == ".canopy") return true;
        const std::vector<std::string>& names = settings_file_names();
        const std::vector<std::string> sections = top_sections(text);
        if (sections.empty()) return false;
        if (std::find(names.begin(), names.end(), name) != names.end()) return true;
        if (folder == "themes" && std::find(sections.begin(), sections.end(), "theme") != sections.end()) return true;
        for (const std::string& s : sections) {
            if (!settings_section(s)) return false;
        }
        return true;
    }

    std::optional<std::string> settings_hover(const std::string& section, const std::string& key, const std::string& file, const std::string& text) {
        if (!settings_bark_file(file, text)) return std::nullopt;
        std::vector<std::string> sections = m_ws.settings.known_sections();
        sections.push_back("theme");
        if (!settings_section(section)) {
            const std::string near = closest_name(section, sections);
            return "`" + section + "` isn't a Canopy settings section, so Canopy skips everything in it" + (near.empty() ? std::string(".") : "; did you mean `" + near + "`?") + "\n";
        }
        if (key.empty()) {
            std::string md = "**Canopy settings:** `" + section + "`\n";
            if (section == "theme") md += "The name and base colors of a color theme. The highlight sections below it color the code.\n";
            else md += "Usually kept in `" + settings_file_for_section(section) + "`. Settings (Ctrl+,) lists each one with what it does.\n";
            return md;
        }
        std::vector<std::string> keys = m_ws.settings.keys(section);
        if (section == "theme") keys = { "name", "background", "foreground" };
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) {
            const std::string near = closest_name(key, keys);
            return "Canopy has no `" + key + "` setting in `" + section + "`, so it's ignored" + (near.empty() ? std::string(".") : "; did you mean `" + near + "`?") + "\n";
        }
        const std::optional<SettingDoc> doc = find_setting_doc(section, key);
        std::string md = "**" + (doc ? doc->label : key) + "** \xC2\xB7 `" + section + "." + key + "`\n";
        if (doc && !doc->help.empty()) md += doc->help + "\n";
        static const Settings defaults = Settings::defaults();
        const std::optional<std::string> fallback = defaults.get(section, key);
        const Setting* now = m_ws.settings.find(section, key);
        std::string values;
        if (fallback) values += "**Default:** `" + *fallback + "`";
        if (now && now->source != SettingSource::Default) {
            if (!values.empty()) values += " \xC2\xB7 ";
            const CodeLocation where{ now->file, now->line, 0, 0 };
            values += "**In use:** `" + now->value + "` from " + hover_link(fs::path(now->file).filename().string() + ":" + std::to_string(now->line + 1), where);
        }
        if (!values.empty()) md += "---\n" + values + "\n";
        return md;
    }

    void setup_intel() {
        m_intel.set_workspace(m_ws.root);
        m_intel.set_bark_settings([this](const std::string& section, const std::string& key, const std::string& file, const std::string& text) { return settings_hover(section, key, file, text); });
        m_intel.set_open_texts([this](const std::string& path) -> std::optional<std::string> {
            const OpenFile* f = find_file(path);
            if (!f || !f->editor || virtual_tab(f->path)) return std::nullopt;
            return grove::input_string(*f->editor).value_or("");
        });
        std::vector<std::string> dirs;
        for (const char* section : { "language.c", "language.cpp" }) {
            const std::vector<std::string> flags = split_flags(m_ws.settings.get_or(section, "flags", ""));
            for (std::size_t i = 0; i < flags.size(); ++i) {
                if (flags[i] == "-I" && i + 1 < flags.size()) dirs.push_back(flags[++i]);
                else if (flags[i].rfind("-I", 0) == 0 && flags[i].size() > 2) dirs.push_back(flags[i].substr(2));
            }
        }
        for (std::string& d : dirs) {
            if (fs::path(d).is_relative() && !m_ws.root.empty()) d = (fs::path(m_ws.root) / d).string();
        }
        m_intel.set_include_dirs(dirs);
        const Tool* cpp = m_ws.toolchain.get("cpp");
        const std::string compiler = cpp ? cpp->path : std::string();
        m_intel.set_system_include_dirs([compiler]() {
            std::vector<std::string> out;
            if (compiler.empty() || fs::path(compiler).filename().string().find("cl") == 0) return out;
            std::string text;
            run_command_capture_input({ compiler, "-E", "-x", "c++", "-", "-v" }, "", text);
            std::istringstream in(text);
            std::string line;
            bool inside = false;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.find("#include <...> search starts here") != std::string::npos) {
                    inside = true;
                    continue;
                }
                if (line.find("End of search list") != std::string::npos) break;
                if (!inside) continue;
                std::string dir = line;
                while (!dir.empty() && dir.front() == ' ') dir.erase(dir.begin());
                const std::size_t framework = dir.find(" (framework");
                if (framework != std::string::npos) continue;
                if (!dir.empty()) out.push_back(dir);
            }
            return out;
        });
    }

    std::size_t tab_size_of(const grove::Node& editor) const {
        return static_cast<std::size_t>(std::max<std::int64_t>(1, grove::parse_integer(editor.attribute_or("tab-size", "4")).value_or(4)));
    }

    bool event_position(grove::Event& e, OpenFile*& file, std::string& text, std::size_t& line, std::size_t& column) {
        const grove::Node* editor = with_attribute(e.target, "data-path");
        if (!editor) return false;
        file = find_file(editor->attribute_or("data-path", ""));
        if (!file || !file->editor || virtual_tab(file->path)) return false;
        text = grove::input_string(*file->editor).value_or("");
        const std::vector<std::string> lines = text_lines(text);
        line = e.x > 0 ? static_cast<std::size_t>(e.x) - 1 : 0;
        if (line >= lines.size()) return false;
        const std::size_t display = e.y > 0 ? static_cast<std::size_t>(e.y) - 1 : 0;
        column = grove::code_byte_column(lines[line], display, tab_size_of(*file->editor));
        return true;
    }

    std::vector<std::pair<std::pair<std::size_t, std::size_t>, std::string>> problems_at(const std::string& path, const std::vector<std::string>& lines, std::size_t line, std::size_t column) {
        std::vector<std::pair<std::pair<std::size_t, std::size_t>, std::string>> out;
        if (line >= lines.size()) return out;
        const auto w = m_walnut.find(path);
        if (w != m_walnut.end()) {
            for (const auto& [problem, message] : w->second.problems) {
                if (problem.line != line) continue;
                const std::size_t end = problem.column + std::max<std::size_t>(1, problem.length);
                if (column < problem.column || column >= end) continue;
                out.push_back({ { problem.column, end - problem.column }, "**" + std::string(message.severity == "warning" ? "Warning" : "Error") + ":** " + message.message });
            }
        }
        std::vector<CompilerMessage> messages = m_messages;
        if (const auto web = m_web_problems.find(path); web != m_web_problems.end()) messages.insert(messages.end(), web->second.begin(), web->second.end());
        for (const CompilerMessage& m : messages) {
            if (m.line != line + 1 || normalized(m.file) != normalized(path)) continue;
            const std::string& row = lines[line];
            std::size_t begin = m.column > 0 ? m.column - 1 : 0;
            std::size_t end = row.size();
            if (m.column > 0 && begin < row.size()) {
                const auto bounds = word_bounds(row, begin);
                end = bounds.second > begin ? bounds.second : begin + 1;
            } else {
                begin = row.find_first_not_of(" \t");
                if (begin == std::string::npos) begin = 0;
            }
            if (column < begin || column >= end) continue;
            out.push_back({ { begin, end - begin }, "**" + std::string(m.severity == "warning" ? "Warning" : m.severity == "note" ? "Note" : "Error") + ":** " + m.message });
        }
        return out;
    }

    struct WalnutCompletionAsk {
        std::string key;
        std::string path;
        std::string text;
        std::size_t line = 0;
        std::size_t column = 0;
    };

    struct WalnutCompletionCache {
        double at = 0;
        std::vector<grove::CompletionItem> items;
    };

    void editor_complete(grove::Event& e) {
        OpenFile* f = nullptr;
        std::string text;
        std::size_t line = 0;
        std::size_t column = 0;
        if (!event_position(e, f, text, line, column)) return;
        if (!m_ws.settings.flag("editor", "autocomplete", true)) {
            e.default_prevented = true;
            return;
        }
        CompletionRequest request;
        request.language = f->editor->attribute_or("language", "plain");
        request.path = f->path;
        request.text = std::move(text);
        request.line = line;
        request.column = column;
        request.manual = e.button == 1;
        request.words = m_ws.settings.flag("editor", "word-suggestions", true);
        const grove::CompletionList list = answer_completion(request, true);
        f->editor->set_attribute("completions", grove::completions_text(list));
    }

    grove::CompletionList answer_completion(const CompletionRequest& request, bool ask_walnut) {
        CompletionSources sources;
        const CompletionContext ctx = completion_context(request);
        const std::string& language = request.language;
        if (ctx.site == CompletionSite::Module && !is_web_language(ctx.language)) {
            sources.extra = walnut_module_items(request.path, ctx.module_prefix);
            return complete_code(request, sources, ctx);
        }
        if (ctx.site == CompletionSite::Include) {
            sources.include_dirs = m_intel.include_dirs_for(request.path, ctx.angled);
        } else if (ctx.site != CompletionSite::None && (language == "c" || language == "cpp" || language == "cmake" || language == "bark" || (language == "grove" && ctx.language == "bark"))) {
            sources.symbols = m_intel.symbols_near(request.path, language == "cpp" || language == "c" ? "c" : ctx.language);
        }
        if (language == "walnut" && ctx.site != CompletionSite::None) {
            const std::vector<std::string> lines = text_lines(request.text);
            const std::string head = request.line < lines.size() ? lines[request.line].substr(0, std::min(ctx.start, lines[request.line].size())) : std::string();
            const std::string key = request.path + "#" + std::to_string(request.line) + ":" + std::to_string(ctx.start) + "|" + head;
            m_complete_last_key = key;
            m_complete_last = request;
            const auto cached = m_walnut_completions.find(key);
            if (cached != m_walnut_completions.end() && now_seconds() - cached->second.at < 20.0) {
                sources.extra = cached->second.items;
            } else if (ask_walnut && m_ws.toolchain.get("walnut")) {
                sources.pending = true;
                m_complete_ask = WalnutCompletionAsk{ key, request.path, request.text, request.line + 1, request.column + 1 };
                start_walnut_completion();
            }
            const auto w = m_walnut.find(request.path);
            if (w != m_walnut.end() && ctx.site == CompletionSite::Code) {
                std::set<std::string> seen;
                for (const WalnutLink& link : w->second.links) {
                    if (link.column + link.length > link.line_text.size()) continue;
                    std::string name = link.line_text.substr(link.column, link.length);
                    if (name.size() < 2 || !seen.insert(name).second) continue;
                    grove::CompletionItem item;
                    item.label = std::move(name);
                    item.kind = "variable";
                    sources.extra.push_back(std::move(item));
                }
            }
        }
        return complete_code(request, sources, ctx);
    }

    const std::vector<std::string>& walnut_library_modules() {
        if (!m_library_modules_checked) {
            m_library_modules_checked = true;
            if (const Tool* walnut = m_ws.toolchain.get("walnut")) {
                std::string output;
                if (run_command_capture({ walnut->path, "library" }, output) == 0) {
                    std::istringstream in(output);
                    std::string line;
                    while (std::getline(in, line)) {
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                        if (line.size() > 5 && line.compare(line.size() - 5, 5, ".walh") == 0 && line.find('/') == std::string::npos) m_library_modules.push_back(line.substr(0, line.size() - 5));
                    }
                }
            }
        }
        return m_library_modules;
    }

    std::vector<grove::CompletionItem> walnut_module_items(const std::string& path, const std::string& prefix) {
        std::vector<grove::CompletionItem> items;
        std::set<std::string> seen;
        std::string folder = prefix;
        std::replace(folder.begin(), folder.end(), '.', '/');
        if (prefix.empty()) {
            for (const std::string& name : walnut_library_modules()) {
                if (name == "core" || !seen.insert(name).second) continue;
                grove::CompletionItem item;
                item.label = name;
                item.kind = "module";
                item.detail = "Walnut library: std::" + name;
                item.documentation = "`import module." + name + "` gives `std::" + name + "::...`";
                items.push_back(std::move(item));
            }
        }
        std::vector<fs::path> roots = { fs::path(path).parent_path() };
        if (!m_ws.root.empty()) roots.push_back(m_ws.root);
        const std::string flags = m_ws.settings.get_or("language.walnut", "flags", "");
        std::istringstream words(flags);
        std::string word;
        bool next_dir = false;
        while (words >> word) {
            if (next_dir) roots.emplace_back(fs::path(m_ws.root) / word);
            next_dir = word == "-M" || word == "--module-dir";
            if (word.rfind("-M", 0) == 0 && word.size() > 2) roots.emplace_back(fs::path(m_ws.root) / word.substr(2));
        }
        for (const fs::path& root : roots) {
            std::error_code ec;
            const fs::path dir = folder.empty() ? root : root / folder;
            if (!fs::is_directory(dir, ec)) continue;
            for (const fs::directory_entry& e : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                const std::string name = e.path().filename().string();
                if (name.empty() || name[0] == '.') continue;
                const std::string ext = e.path().extension().string();
                grove::CompletionItem item;
                if (e.is_directory(ec)) {
                    bool has_module = false;
                    for (const fs::directory_entry& inner : fs::directory_iterator(e.path(), fs::directory_options::skip_permission_denied, ec)) {
                        const std::string x = inner.path().extension().string();
                        if (x == ".walh" || x == ".wal" || inner.is_directory(ec)) {
                            has_module = has_module || x == ".walh" || x == ".wal";
                        }
                    }
                    if (!has_module) continue;
                    item.label = name;
                    item.kind = "folder";
                    item.detail = "folder of modules";
                    item.insert = name + ".";
                } else if (ext == ".walh" || ext == ".wal") {
                    const std::string stem = e.path().stem().string();
                    if (fs::path(path).filename() == e.path().filename()) continue;
                    if (ext == ".wal") {
                        const std::optional<std::string> text = grove::read_file(e.path().string());
                        if (text && text->find("function main(") != std::string::npos) continue;
                    }
                    item.label = stem;
                    item.kind = "module";
                    item.detail = relative_to(e.path().string(), m_ws.root.empty() ? root.string() : m_ws.root);
                } else {
                    continue;
                }
                if (!seen.insert(item.label).second) continue;
                items.push_back(std::move(item));
            }
        }
        return items;
    }

    void start_walnut_completion() {
        if (!m_complete_ask || m_walnut_complete.running()) return;
        const Tool* walnut = m_ws.toolchain.get("walnut");
        if (!walnut) {
            m_complete_ask.reset();
            return;
        }
        const WalnutCompletionAsk ask = *m_complete_ask;
        m_complete_ask.reset();
        m_complete_running_key = ask.key;
        m_walnut_complete.start(ask.path, walnut_complete_command(walnut->path, ask.path, m_ws.settings.get_or("language.walnut", "flags", ""), ask.line, ask.column), ask.text);
    }

    void poll_completion() {
        if (std::optional<SemanticJob::Done> done = m_walnut_complete.take()) {
            const WalnutCompletions result = parse_walnut_complete(done->output);
            const std::string key = m_complete_running_key;
            m_complete_running_key.clear();
            if (m_walnut_completions.size() > 200) m_walnut_completions.clear();
            WalnutCompletionCache& entry = m_walnut_completions[key];
            entry.at = now_seconds();
            entry.items.clear();
            if (result.ok) entry.items = result.items;
            if (key == m_complete_last_key && !m_complete_ask) {
                OpenFile* f = find_file(m_complete_last.path);
                if (f && f->editor) {
                    const grove::CompletionList list = answer_completion(m_complete_last, false);
                    f->editor->set_attribute("completions", grove::completions_text(list));
                    m_doc.touch();
                }
            }
        }
        start_walnut_completion();
    }

    void editor_hover(grove::Event& e) {
        OpenFile* f = nullptr;
        std::string text;
        std::size_t line = 0;
        std::size_t column = 0;
        if (!event_position(e, f, text, line, column)) return;
        m_hover_path = f->path;
        m_hover_line = line;
        m_hover_column = column;
        show_hover(*f, text, line, column);
    }

    void show_hover(OpenFile& f, const std::string& text, std::size_t line, std::size_t column) {
        const std::vector<std::string> lines = text_lines(text);
        const std::string language = f.editor->attribute_or("language", "plain");
        std::optional<HoverResult> hover;
        if (language == "walnut") {
            hover = literal_hover("walnut", text, line, column);
            const auto w = m_walnut.find(f.path);
            if (!hover && w != m_walnut.end()) {
                const WalnutLink* link = walnut_link_at(w->second.links, lines, line, column);
                const WalnutHover* h = walnut_hover_at(w->second.hovers, lines, line, column);
                if (h) {
                    const std::string key = f.path + "#" + std::to_string(h->line) + ":" + std::to_string(h->column);
                    const std::size_t page = m_overload_page[key];
                    std::string md = walnut_hover_markdown(*h, page);
                    if (!md.empty() && link && link->definition.file != f.path && h->doc.find("Ctrl+click") == std::string::npos && (h->overloads.size() < 2 || page % h->overloads.size() == 0)) md += "---\n" + defined_in(link->definition) + "\n";
                    if (!md.empty()) hover = HoverResult{ line, h->column, h->length, md };
                }
                if (!hover && link) hover = HoverResult{ line, link->column, link->length, defined_in(link->definition) };
            }
        } else if (intel_language(language)) {
            IntelRequest request{ f.path, language, text, line, column };
            hover = m_intel.hover(request);
        } else if (is_web_language(language)) {
            hover = web_hover(language, text, line, column);
        } else if (is_more_language(language)) {
            hover = more_hover(language, text, f.path, line, column);
        }
        const auto problems = problems_at(f.path, lines, line, column);
        if (!problems.empty()) {
            std::string md;
            for (const auto& p : problems) md += p.second + "\n";
            if (hover) {
                hover->markdown = md + "---\n" + hover->markdown;
            } else {
                hover = HoverResult{ line, problems.front().first.first, problems.front().first.second, md };
            }
        }
        if (!hover || hover->markdown.empty()) {
            f.editor->remove_attribute("hover");
            return;
        }
        hover->markdown = format_hover_signatures(hover->markdown, hover_columns());
        f.editor->set_attribute("hover", std::to_string(hover->line) + " " + std::to_string(hover->column) + " " + std::to_string(hover->length) + "\n" + hover->markdown);
        m_doc.touch();
    }

    std::string defined_in(const CodeLocation& definition) const {
        const CodeLocation where = resolve_walnut(definition);
        const bool library = definition.file.rfind("<walnut>/", 0) == 0;
        const std::string label = library ? walnut_library_name(definition.file) + ":" + std::to_string(definition.line + 1) : display_location(where);
        if (where.file.empty()) return (library ? "From the Walnut library, in `" : "Defined in `") + label + "`";
        return (library ? "From the Walnut library, in " : "Defined in ") + hover_link(label, where) + ". Ctrl+click goes there.";
    }

    std::string display_location(const CodeLocation& where) const {
        std::error_code ec;
        std::string shown = where.file;
        if (!m_ws.root.empty()) {
            const fs::path rel = fs::relative(where.file, m_ws.root, ec);
            if (!ec && !rel.empty() && rel.generic_string().rfind("..", 0) != 0) shown = rel.generic_string();
        }
        return shown + ":" + std::to_string(where.line + 1);
    }

    CodeLocation resolve_walnut(const CodeLocation& where) const {
        CodeLocation out = where;
        if (where.file.rfind("<walnut>/", 0) == 0) {
            const Tool* walnut = m_ws.toolchain.get("walnut");
            out.file = walnut ? walnut_library_path(walnut->path, where.file) : std::string();
        }
        return out;
    }

    void editor_hover_action(grove::Event& e) {
        const grove::Node* editor = with_attribute(e.target, "data-path");
        if (!editor) return;
        OpenFile* f = find_file(editor->attribute_or("data-path", ""));
        if (!f) return;
        const std::string action = e.key;
        if (action.rfind("overload:", 0) == 0) {
            const std::string text = grove::input_string(*f->editor).value_or("");
            const std::vector<std::string> lines = text_lines(text);
            const auto w = m_walnut.find(f->path);
            if (w == m_walnut.end() || f->path != m_hover_path) return;
            const WalnutHover* h = walnut_hover_at(w->second.hovers, lines, m_hover_line, m_hover_column);
            if (!h || h->overloads.size() < 2) return;
            std::size_t& page = m_overload_page[f->path + "#" + std::to_string(h->line) + ":" + std::to_string(h->column)];
            const std::size_t count = h->overloads.size();
            page = action == "overload:next" ? (page + 1) % count : (page + count - 1) % count;
            show_hover(*f, text, m_hover_line, m_hover_column);
            return;
        }
        if (auto where = parse_hover_link(action)) navigate(*where, true);
    }

    void editor_goto(grove::Event& e) {
        OpenFile* f = nullptr;
        std::string text;
        std::size_t line = 0;
        std::size_t column = 0;
        if (!event_position(e, f, text, line, column)) return;
        goto_definition(*f, text, line, column);
    }

    void goto_caret() {
        OpenFile* f = find_file(m_active);
        if (!f || !f->editor || virtual_tab(f->path)) return;
        const auto caret = m_carets.find(f->path);
        if (caret == m_carets.end()) return;
        const std::string text = grove::input_string(*f->editor).value_or("");
        const std::vector<std::string> lines = text_lines(text);
        const std::size_t line = caret->second.first > 0 ? caret->second.first - 1 : 0;
        if (line >= lines.size()) return;
        std::size_t column = grove::code_byte_column(lines[line], caret->second.second > 0 ? caret->second.second - 1 : 0, tab_size_of(*f->editor));
        if (column >= lines[line].size() && column > 0) --column;
        goto_definition(*f, text, line, column);
    }

    void goto_definition(OpenFile& f, const std::string& text, std::size_t line, std::size_t column) {
        const std::string language = f.editor->attribute_or("language", "plain");
        std::optional<CodeLocation> where;
        std::string word;
        const std::vector<std::string> lines = text_lines(text);
        if (line < lines.size()) {
            const auto bounds = word_bounds(lines[line], column);
            word = lines[line].substr(bounds.first, bounds.second - bounds.first);
        }
        if (language == "walnut") {
            const auto w = m_walnut.find(f.path);
            if (w != m_walnut.end()) {
                if (const WalnutLink* link = walnut_link_at(w->second.links, lines, line, column)) {
                    CodeLocation target = link->definition;
                    if (target.file == f.path && target.line == line && target.column <= column && column < target.column + std::max<std::size_t>(1, target.length) && !link->declarations.empty()) target = link->declarations.front();
                    const CodeLocation resolved = resolve_walnut(target);
                    if (resolved.file.empty()) {
                        set_text(m_doc, "status-problems", "'" + word + "' is in the Walnut library's " + walnut_library_name(target.file) + ", which this walnut can't hand over. Walnut 0.1.11 or newer can (walnut library).");
                        return;
                    }
                    where = resolved;
                }
            }
        } else if (intel_language(language)) {
            where = m_intel.definition(IntelRequest{ f.path, language, text, line, column });
        }
        if (!where) {
            if (!word.empty()) set_text(m_doc, "status-problems", "No definition found for '" + word + "'");
            return;
        }
        navigate(*where, true);
    }

    void navigate(const CodeLocation& where, bool remember) {
        if (remember && !m_active.empty() && !virtual_tab(m_active)) {
            const auto caret = m_carets.find(m_active);
            m_back.push_back(Place{ m_active, caret == m_carets.end() ? 1 : caret->second.first, caret == m_carets.end() ? 1 : caret->second.second });
            if (m_back.size() > 50) m_back.erase(m_back.begin());
            m_forward.clear();
        }
        std::size_t display = where.column;
        if (const std::optional<std::string> text = m_intel.text_of(where.file)) {
            const std::vector<std::string> lines = text_lines(*text);
            if (where.line < lines.size()) display = grove::code_display_column(lines[where.line], std::min(where.column, lines[where.line].size()), 4);
        }
        open_file(where.file, where.line + 1, display + 1, true);
    }

    void jump(std::vector<Place>& from, std::vector<Place>& to) {
        if (from.empty()) return;
        const Place target = from.back();
        from.pop_back();
        if (!m_active.empty() && !virtual_tab(m_active)) {
            const auto caret = m_carets.find(m_active);
            to.push_back(Place{ m_active, caret == m_carets.end() ? 1 : caret->second.first, caret == m_carets.end() ? 1 : caret->second.second });
        }
        open_file(target.path, target.line, target.column);
    }

    void go_back() { jump(m_back, m_forward); }

    void go_forward() { jump(m_forward, m_back); }

    static std::string clock_text(std::int64_t ms) {
        const std::time_t t = static_cast<std::time_t>(ms / 1000);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &t);
#else
        localtime_r(&t, &local);
#endif
        char text[32];
        std::strftime(text, sizeof(text), "%H:%M:%S", &local);
        return text;
    }

    static std::string percent_text(std::int64_t part, std::int64_t whole) {
        if (whole <= 0) return "0%";
        const double p = 100.0 * static_cast<double>(part) / static_cast<double>(whole);
        char text[32];
        std::snprintf(text, sizeof(text), p >= 10.0 ? "%.0f%%" : "%.1f%%", p);
        return text;
    }

    static std::string short_command(const std::string& command) {
        std::string name;
        std::size_t rest = 0;
        if (!command.empty() && (command[0] == '\'' || command[0] == '"')) {
            const std::size_t end = command.find(command[0], 1);
            name = command.substr(1, end == std::string::npos ? std::string::npos : end - 1);
            rest = end == std::string::npos ? command.size() : end + 1;
        } else {
            rest = std::min(command.find(' '), command.size());
            name = command.substr(0, rest);
        }
        std::string out = fs::path(name).filename().string();
        std::string tail = command.substr(rest);
        if (tail.size() > 60) tail = tail.substr(0, 57) + "...";
        return out + tail;
    }

    void stats_row(grove::Node& table, const std::string& label, const std::string& value, const std::string& tip = {}) {
        grove::Node* row = add(m_doc, table, "row", "", "stats-row");
        add(m_doc, *row, "text", label, "stats-label");
        grove::Node* v = add(m_doc, *row, "text", value, "stats-value");
        if (!tip.empty()) v->set_attribute("title", tip);
    }

    void refresh_stats() {
        grove::Node* view = m_doc.find_by_id("stats");
        if (!view) return;
        clear(m_doc, *view);
        if (m_runs.empty()) {
            add(m_doc, *view, "p", "Run a program (F5) to see how long it took and how much memory it used.", "muted");
            m_doc.touch();
            return;
        }
        const RunRecord& last = m_runs.front();
        const RunStats& program = last.steps.back();
        std::int64_t total_us = 0;
        for (const RunStats& s : last.steps) total_us += s.wall_us;
        add(m_doc, *view, "text", display_name(last.file) + (last.program ? " \xC2\xB7 ran at " : " \xC2\xB7 built at ") + clock_text(last.steps.front().started_ms), "stats-title");
        grove::Node* columns = add(m_doc, *view, "row", "", "stats-columns");
        if (last.program) {
            grove::Node* table = add(m_doc, *columns, "column", "", "stats-table");
            add(m_doc, *table, "text", "Program", "stats-heading");
            stats_row(*table, "Command", short_command(program.command), program.command);
            stats_row(*table, "Exit code", std::to_string(program.exit_code));
            stats_row(*table, "Wall time", format_duration_us(program.wall_us), "From starting the program until it ended");
            stats_row(*table, "CPU time", format_duration_us(program.user_us + program.system_us), "Time the processor spent on the program, on every core added together");
            stats_row(*table, "  in the program", format_duration_us(program.user_us), "User time: running the program's own code");
            stats_row(*table, "  in the system", format_duration_us(program.system_us), "System time: the operating system working for the program (files, memory, output)");
            stats_row(*table, "CPU use", percent_text(program.user_us + program.system_us, program.wall_us), "CPU time divided by wall time; above 100% means it used more than one core");
            stats_row(*table, program.peak_is_commit ? "Peak memory (committed)" : "Peak memory", format_bytes(program.peak_bytes), program.peak_is_commit ? "The most memory the program had committed at once" : "The most memory the program had in RAM at once (peak resident set size)");
            if (program.minor_faults || program.major_faults) stats_row(*table, "Page faults", std::to_string(program.minor_faults) + " minor \xC2\xB7 " + std::to_string(program.major_faults) + " major", "Major faults had to read from disk");
            if (program.voluntary_switches || program.involuntary_switches) stats_row(*table, "Context switches", std::to_string(program.voluntary_switches) + " waiting \xC2\xB7 " + std::to_string(program.involuntary_switches) + " preempted", "Waiting: the program gave up the CPU to wait for something. Preempted: the system switched to another program");
            if (program.read_bytes || program.written_bytes) stats_row(*table, "Disk", format_bytes(program.read_bytes) + " read \xC2\xB7 " + format_bytes(program.written_bytes) + " written");
        }
        const std::size_t build_steps = last.program ? last.steps.size() - 1 : last.steps.size();
        if (build_steps > 0) {
            grove::Node* table = add(m_doc, *columns, "column", "", "stats-table");
            add(m_doc, *table, "text", "Build", "stats-heading");
            std::int64_t build_us = 0;
            for (std::size_t i = 0; i < build_steps; ++i) {
                const RunStats& s = last.steps[i];
                build_us += s.wall_us;
                const std::string name = short_command(s.command);
                stats_row(*table, name.substr(0, name.find(' ')), format_duration_us(s.wall_us) + " \xC2\xB7 " + format_bytes(s.peak_bytes), s.command);
            }
            stats_row(*table, "Build total", format_duration_us(build_us));
            stats_row(*table, "Everything", format_duration_us(total_us));
        }
        if (m_runs.size() > 1) {
            grove::Node* table = add(m_doc, *columns, "column", "", "stats-table");
            add(m_doc, *table, "text", "Earlier runs", "stats-heading");
            for (std::size_t i = 1; i < m_runs.size(); ++i) {
                const RunRecord& r = m_runs[i];
                const RunStats& s = r.steps.back();
                const std::string what = r.program ? format_duration_us(s.wall_us) + " \xC2\xB7 " + format_bytes(s.peak_bytes) + " \xC2\xB7 exit " + std::to_string(s.exit_code) : "build only";
                stats_row(*table, clock_text(r.steps.front().started_ms) + "  " + display_name(r.file), what);
            }
        }
        m_doc.touch();
    }

    void poll_job() {
        if (m_git) m_git->poll(now_seconds());
        poll_semantic();
        poll_web_checks();
        poll_viewers();
        poll_markdown();
        poll_servers();
        const std::string text = m_job.take_output();
        if (!text.empty()) {
            m_output += text;
            trim_output();
            sync_output();
        }
        const std::optional<int> result = m_job.take_result();
        if (!result) return;
        m_messages = parse_compiler_messages(m_output);
        if (!m_build_folder.empty()) {
            for (CompilerMessage& m : m_messages) {
                std::error_code ec;
                if (m.file.empty() || fs::path(m.file).is_absolute()) continue;
                const fs::path there = fs::path(m_build_folder) / m.file;
                if (fs::exists(there, ec)) m.file = there.lexically_normal().string();
            }
        }
        update_all_marks();
        std::vector<RunStats> stats = m_job.take_stats();
        const bool program_ran = m_job_program && !stats.empty() && stats.size() == m_job_steps;
        if (program_ran) m_output += "\n" + run_stats_summary(stats.back()) + "\n";
        if (!stats.empty()) {
            RunRecord record;
            record.file = m_job_file;
            record.program = program_ran;
            record.steps = std::move(stats);
            m_runs.insert(m_runs.begin(), std::move(record));
            if (m_runs.size() > 20) m_runs.resize(20);
            refresh_stats();
        }
        m_output += *result == 0 ? std::string("\n") + m_running_label + " finished.\n" : "\n" + m_running_label + " failed (exit " + std::to_string(*result) + ").\n";
        sync_output();
        refresh_problems();
        refresh_explorer();
        bool errors = false;
        for (const CompilerMessage& m : m_messages) errors = errors || m.severity == "error";
        if (errors) show_panel("problems");
        const std::string after_task = m_after_task;
        m_after_task.clear();
        if (*result == 0 && !after_task.empty() && m_git) m_git->run_task(after_task);
    }

    struct Viewer {
        enum class Phase {
            Idle,
            Building,
            Rendering
        };

        ViewerKind kind = ViewerKind::Text;
        grove::Node* root = nullptr;
        grove::Node* body = nullptr;
        int zoom = 0;
        ByteView bytes = ByteView::Hex;
        bool info = false;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        ImageInfo image;
        BinaryInfo binary;
        std::unique_ptr<BackgroundJob> job;
        Phase phase = Phase::Idle;
        bool again = false;
        bool collapsed = false;
        std::string source;
        std::string pdf;
        std::string render_dir;
        std::string pending_dir;
        std::vector<std::string> pages;
        ImageInfo page;
        int dpi = 144;
        std::string tool;
        std::string status;
        std::string log;
        std::size_t serial = 0;
        std::string md_text;
        std::vector<std::uint64_t> md_keys;
        std::vector<grove::Node*> md_nodes;
        grove::Node* md_note = nullptr;
    };

    static const std::vector<int>& zoom_steps() {
        static const std::vector<int> steps = { 10, 25, 33, 50, 67, 75, 90, 100, 125, 150, 200, 300, 400 };
        return steps;
    }

    grove::Node* view_button(grove::Node& parent, const std::string& label, const std::string& action, const std::string& title, bool on = false, bool enabled = true) {
        std::string classes = "view-button";
        if (on) classes += " on";
        if (!enabled) classes += " off";
        grove::Node* b = add(m_doc, parent, "text", label, classes);
        if (enabled) b->set_attribute("data-view-action", action);
        if (!title.empty()) b->set_attribute("title", title);
        return b;
    }

    bool open_viewer(const std::string& path, ViewerKind kind) {
        grove::Node* editors = m_doc.find_by_id("editors");
        if (!editors || !m_doc.find_by_id("tabs")) return false;
        const std::optional<std::uint64_t> size = file_size(path);
        if (!size) {
            notice("Can't open " + path);
            return false;
        }
        grove::Node* root = m_doc.create_element("column");
        root->set_attribute("class", "viewer hidden");
        root->set_attribute("data-viewer", path);
        m_doc.append_child(*editors, *root);
        Viewer& v = m_viewers[path];
        v = Viewer{};
        v.kind = kind;
        v.root = root;
        v.size = *size;
        if (kind == ViewerKind::Image) {
            v.image = image_info(read_file_range(path, 0, 512 * 1024).value_or(""), path);
        } else if (kind == ViewerKind::Binary) {
            const std::size_t limit = 256u * 1024u * 1024u;
            v.binary = describe_binary(read_file_range(path, 0, v.size <= limit ? static_cast<std::size_t>(v.size) : 8u * 1024u * 1024u).value_or(""), path);
        } else if (kind == ViewerKind::Pdf) {
            v.source = path;
            v.pdf = path;
        }
        grove::Node* tab = create_tab(path, file_icon(path, false, false), display_name(path));
        add_open_file(OpenFile{ path, nullptr, tab, "", false, root, kind });
        render_viewer(path);
        if (kind == ViewerKind::Pdf) start_render(path);
        return true;
    }

    void forget_viewer(const std::string& path) {
        const auto it = m_viewers.find(path);
        if (it == m_viewers.end()) return;
        std::error_code ec;
        if (!it->second.render_dir.empty()) fs::remove_all(it->second.render_dir, ec);
        if (!it->second.pending_dir.empty()) fs::remove_all(it->second.pending_dir, ec);
        m_viewers.erase(it);
    }

    void reopen_as(const std::string& path, std::optional<ViewerKind> kind) {
        OpenFile* f = find_file(path);
        if (f && f->dirty) {
            notice(display_name(path) + " has unsaved changes; save it before opening it another way");
            show_panel("output");
            return;
        }
        if (kind) m_open_as[path] = *kind;
        else m_open_as.erase(path);
        if (f) {
            const auto at = std::find_if(m_files.begin(), m_files.end(), [&](const OpenFile& o) { return o.path == path; });
            m_file_slot = static_cast<std::size_t>(at - m_files.begin());
            grove::Node* tabs = m_doc.find_by_id("tabs");
            m_tab_anchor = nullptr;
            if (tabs) {
                const auto& kids = tabs->children();
                for (std::size_t i = 0; i < kids.size(); ++i) {
                    if (kids[i].get() == f->tab && i + 1 < kids.size()) m_tab_anchor = kids[i + 1].get();
                }
            }
            close_file(path);
        }
        open_file(path);
        m_tab_anchor = nullptr;
        m_file_slot = static_cast<std::size_t>(-1);
    }

    void render_viewer(const std::string& path) {
        const auto it = m_viewers.find(path);
        if (it == m_viewers.end() || !it->second.root) return;
        Viewer& v = it->second;
        clear(m_doc, *v.root);
        if (v.kind == ViewerKind::Image) render_image(path, v);
        else if (v.kind == ViewerKind::Binary) render_binary(path, v);
        else if (v.kind == ViewerKind::Markdown) render_markdown(path, v);
        else if (v.kind == ViewerKind::Math) render_math(path, v);
        else render_pages(path, v);
        m_doc.touch();
    }

    void refresh_viewers() {
        for (auto& [path, v] : m_viewers) {
            if (v.kind == ViewerKind::Binary) render_viewer(path);
        }
    }

    grove::Node* viewer_bar(Viewer& v, const std::string& title, const std::string& meta) {
        grove::Node* bar = add(m_doc, *v.root, "row", "", "viewer-bar");
        add(m_doc, *bar, "text", title, "viewer-title");
        if (!meta.empty()) add(m_doc, *bar, "text", meta, "viewer-meta");
        add(m_doc, *bar, "div", "", "viewer-spacer");
        return bar;
    }

    void zoom_controls(grove::Node& bar, const Viewer& v, bool known, bool compact = false) {
        view_button(bar, "\xE2\x88\x92", "zoom-out", "Zoom out", false, known);
        grove::Node* level = view_button(bar, v.zoom == 0 ? std::string("Fit") : std::to_string(v.zoom) + "%", "zoom-fit", "Fit to the editor", v.zoom == 0);
        grove::add_class(m_doc, *level, "zoom-level");
        view_button(bar, "+", "zoom-in", "Zoom in", false, known);
        if (!compact) view_button(bar, "1:1", "zoom-100", "Actual size", v.zoom == 100, known);
    }

    void render_image(const std::string& path, Viewer& v) {
        std::string meta = v.image.format.empty() ? std::string("Image") : v.image.format;
        if (v.image.known_size()) meta += "  \xC2\xB7  " + std::to_string(v.image.width) + " \xC3\x97 " + std::to_string(v.image.height);
        if (!v.image.detail.empty()) meta += "  \xC2\xB7  " + v.image.detail;
        meta += "  \xC2\xB7  " + human_size(v.size);
        grove::Node* bar = viewer_bar(v, display_name(path), meta);
        zoom_controls(*bar, v, v.image.known_size());
        add(m_doc, *bar, "div", "", "viewer-gap");
        if (v.image.format == "SVG") view_button(*bar, "Source", "open-text", "Open the SVG as text");
        view_button(*bar, "Hex", "open-hex", "Show the bytes");
        v.body = add(m_doc, *v.root, "column", "", v.zoom == 0 ? "viewer-body image-stage fit" : "viewer-body image-stage");
        grove::Node* img = m_doc.create_element("img");
        img->set_attribute("src", path);
        img->set_attribute("class", "viewer-image");
        if (v.zoom > 0 && v.image.known_size()) img->set_attribute("width", std::to_string(std::max(1u, v.image.width * static_cast<unsigned>(v.zoom) / 100u)));
        m_doc.append_child(*v.body, *img);
    }

    void render_pages(const std::string& path, Viewer& v) {
        const bool latex = v.kind == ViewerKind::Latex;
        if (latex && v.collapsed) {
            grove::add_class(m_doc, *v.root, "collapsed");
            grove::Node* show = view_button(*v.root, "\xE2\x80\xB9", "toggle-preview", "Show the preview");
            grove::add_class(m_doc, *show, "show-preview");
            return;
        }
        grove::remove_class(m_doc, *v.root, "collapsed");
        std::string meta;
        if (!v.pages.empty()) meta = std::to_string(v.pages.size()) + (v.pages.size() == 1 ? " page" : " pages");
        if (!latex) {
            meta += (meta.empty() ? "" : "  \xC2\xB7  ") + human_size(v.size);
        }
        if (v.phase == Viewer::Phase::Building) meta += (meta.empty() ? "" : "  \xC2\xB7  ") + std::string("building with ") + v.tool + "\xE2\x80\xA6";
        else if (v.phase == Viewer::Phase::Rendering) meta += (meta.empty() ? "" : "  \xC2\xB7  ") + std::string("drawing pages\xE2\x80\xA6");
        else if (!latex && !v.tool.empty() && !v.pages.empty()) meta += "  \xC2\xB7  " + v.tool;
        grove::Node* bar = viewer_bar(v, latex ? std::string("Preview") : display_name(path), meta);
        zoom_controls(*bar, v, v.page.known_size(), latex);
        add(m_doc, *bar, "div", "", "viewer-gap");
        if (latex) {
            view_button(*bar, "Rebuild", "rebuild", "Build the document again with " + (v.tool.empty() ? std::string("LaTeX") : v.tool) + " (Ctrl+B)");
            if (!v.pdf.empty() && fs::exists(v.pdf)) view_button(*bar, "PDF", "open-pdf", "Open the built PDF in its own tab");
            view_button(*bar, "\xE2\x80\xBA", "toggle-preview", "Hide the preview");
        } else {
            view_button(*bar, "Reload", "rebuild", "Draw the pages again");
            view_button(*bar, "Hex", "open-hex", "Show the bytes");
        }
        v.body = add(m_doc, *v.root, "column", "", latex ? "viewer-body page-stack narrow" : "viewer-body page-stack");
        if (v.pages.empty()) {
            std::string status = v.status;
            if (status.empty()) status = v.phase == Viewer::Phase::Idle ? std::string("Nothing to show yet.") : v.phase == Viewer::Phase::Building ? "Building with " + v.tool + "\xE2\x80\xA6" : std::string("Drawing the pages\xE2\x80\xA6");
            std::istringstream lines(status);
            std::string line;
            bool first = true;
            while (std::getline(lines, line)) {
                add(m_doc, *v.body, "p", line, first ? "viewer-status" : "viewer-status muted");
                first = false;
            }
            return;
        }
        if (!v.status.empty()) add(m_doc, *v.body, "p", v.status, "viewer-status warn");
        for (std::size_t i = 0; i < v.pages.size(); ++i) {
            grove::Node* img = m_doc.create_element("img");
            img->set_attribute("src", v.pages[i]);
            img->set_attribute("class", "pdf-page");
            if (v.zoom > 0 && v.page.known_size()) {
                const double scale = 96.0 / static_cast<double>(std::max(1, v.dpi)) * static_cast<double>(v.zoom) / 100.0;
                img->set_attribute("width", std::to_string(std::max(1L, std::lround(static_cast<double>(v.page.width) * scale))));
            } else {
                img->set_attribute("style", "width: 100%");
            }
            m_doc.append_child(*v.body, *img);
            add(m_doc, *v.body, "p", std::to_string(i + 1) + " / " + std::to_string(v.pages.size()), "page-number");
        }
    }

    void render_math(const std::string& path, Viewer& v) {
        if (v.collapsed) {
            v.body = nullptr;
            grove::add_class(m_doc, *v.root, "collapsed");
            grove::Node* show = view_button(*v.root, "\xE2\x80\xB9", "toggle-preview", "Show the preview");
            grove::add_class(m_doc, *show, "show-preview");
            return;
        }
        grove::remove_class(m_doc, *v.root, "collapsed");
        grove::Node* bar = viewer_bar(v, "Preview", is_brace_math_path(path) ? "MathText, brace form" : "MathText");
        add(m_doc, *bar, "div", "", "viewer-gap");
        view_button(*bar, "\xE2\x80\xBA", "toggle-preview", "Hide the preview");
        v.body = add(m_doc, *v.root, "column", "", "viewer-body md-body math-body");
        v.md_text.clear();
        v.md_nodes.clear();
        fill_math(path, v);
    }

    void fill_math(const std::string& path, Viewer& v) {
        if (!v.body) return;
        const OpenFile* f = find_file(path);
        const std::string text = f && f->editor ? grove::input_string(*f->editor).value_or("") : read_text_file(path).value_or("");
        if (text == v.md_text && !v.body->children().empty()) return;
        v.md_text = text;
        clear(m_doc, *v.body);
        v.md_nodes.clear();
        const std::vector<std::string> blocks = math_blocks(text);
        if (blocks.empty()) {
            add(m_doc, *v.body, "p", "Nothing to show yet. Write MathText like (x+2)/(3*exp(4)) \\approxeq\\ 0.054 and it's drawn here as you type; a blank line starts a new block.", "muted");
            m_doc.touch();
            return;
        }
        const bool brace = is_brace_math_path(path);
        for (const std::string& block : blocks) {
            grove::Node* box = add(m_doc, *v.body, "div", "", "math-block");
            grove::Node* math = m_doc.create_element("math");
            if (brace) math->set_attribute("syntax", "brace");
            math->set_inner_text(block);
            m_doc.append_child(*box, *math);
            v.md_nodes.push_back(box);
        }
        m_doc.touch();
    }

    void render_markdown(const std::string& path, Viewer& v) {
        if (v.collapsed) {
            v.body = nullptr;
            grove::add_class(m_doc, *v.root, "collapsed");
            grove::Node* show = view_button(*v.root, "\xE2\x80\xB9", "toggle-preview", "Show the preview");
            grove::add_class(m_doc, *show, "show-preview");
            return;
        }
        grove::remove_class(m_doc, *v.root, "collapsed");
        grove::Node* bar = viewer_bar(v, "Preview", "Markdown");
        add(m_doc, *bar, "div", "", "viewer-gap");
        view_button(*bar, "\xE2\x80\xBA", "toggle-preview", "Hide the preview");
        v.body = add(m_doc, *v.root, "column", "", "viewer-body md-body");
        v.md_text.clear();
        v.md_keys.clear();
        v.md_nodes.clear();
        v.md_note = nullptr;
        fill_markdown(path, v);
    }

    static std::uint64_t markdown_key(const std::vector<std::string>& lines, std::size_t from, std::size_t to) {
        std::uint64_t h = 14695981039346656037ull;
        for (std::size_t i = from; i < to && i < lines.size(); ++i) {
            for (const unsigned char c : lines[i]) {
                h ^= c;
                h *= 1099511628211ull;
            }
            h ^= '\n';
            h *= 1099511628211ull;
        }
        return h;
    }

    void fill_markdown(const std::string& path, Viewer& v) {
        if (!v.body) return;
        const OpenFile* f = find_file(path);
        const std::string text = f && f->editor ? grove::input_string(*f->editor).value_or("") : read_text_file(path).value_or("");
        bool intact = v.md_note == nullptr || m_doc.is_attached(*v.md_note);
        for (const grove::Node* n : v.md_nodes) intact = intact && m_doc.is_attached(*n) && n->parent() == v.body;
        if (intact && (!v.md_nodes.empty() || v.md_note) && text == v.md_text) return;
        if (!intact) {
            clear(m_doc, *v.body);
            v.md_keys.clear();
            v.md_nodes.clear();
            v.md_note = nullptr;
        }
        v.md_text = text;
        const std::vector<MdBlock> blocks = parse_markdown(text);
        const std::vector<std::string> lines = text_lines(text);
        std::vector<std::uint64_t> keys;
        keys.reserve(blocks.size());
        for (std::size_t i = 0; i < blocks.size(); ++i) keys.push_back(markdown_key(lines, blocks[i].line, i + 1 < blocks.size() ? blocks[i + 1].line : lines.size()));

        if (v.md_note) {
            m_doc.remove(*v.md_note);
            v.md_note = nullptr;
        }
        const std::size_t old_n = v.md_keys.size();
        const std::size_t new_n = keys.size();
        std::size_t prefix = 0;
        while (prefix < old_n && prefix < new_n && v.md_keys[prefix] == keys[prefix]) ++prefix;
        std::size_t suffix = 0;
        while (suffix < old_n - prefix && suffix < new_n - prefix && v.md_keys[old_n - 1 - suffix] == keys[new_n - 1 - suffix]) ++suffix;

        for (std::size_t i = prefix; i < old_n - suffix; ++i) m_doc.remove(*v.md_nodes[i]);
        grove::Node* anchor = suffix > 0 ? v.md_nodes[old_n - suffix] : nullptr;
        const std::string dir = fs::path(path).parent_path().string();
        std::vector<grove::Node*> nodes(v.md_nodes.begin(), v.md_nodes.begin() + static_cast<std::ptrdiff_t>(prefix));
        for (std::size_t i = prefix; i < new_n - suffix; ++i) {
            grove::Node* wrapper = m_doc.create_element("column");
            wrapper->set_attribute("class", "md-block");
            markdown_block(*wrapper, blocks[i], dir, 0);
            m_doc.insert_before(*v.body, *wrapper, anchor);
            nodes.push_back(wrapper);
        }
        nodes.insert(nodes.end(), v.md_nodes.end() - static_cast<std::ptrdiff_t>(suffix), v.md_nodes.end());
        v.md_nodes = std::move(nodes);
        v.md_keys = std::move(keys);
        if (blocks.empty()) v.md_note = add(m_doc, *v.body, "p", "Nothing to preview yet.", "viewer-status muted");
        m_doc.touch();
    }

    void poll_markdown() {
        if (m_markdown_due.empty()) return;
        const double now = now_seconds();
        std::vector<std::string> ready;
        for (const auto& [path, due] : m_markdown_due) {
            if (due <= now) ready.push_back(path);
        }
        for (const std::string& path : ready) {
            m_markdown_due.erase(path);
            const auto it = m_viewers.find(path);
            if (it == m_viewers.end() || it->second.collapsed) continue;
            if (it->second.kind == ViewerKind::Math) {
                if (it->second.body) fill_math(path, it->second);
                else render_viewer(path);
                continue;
            }
            if (it->second.kind != ViewerKind::Markdown) continue;
            if (it->second.body) fill_markdown(path, it->second);
            else render_viewer(path);
        }
    }

    void markdown_block(grove::Node& parent, const MdBlock& b, const std::string& dir, int depth) {
        switch (b.kind) {
        case MdBlock::Kind::Heading: {
            const int level = std::clamp(b.level, 1, 6);
            grove::Node* h = add(m_doc, parent, "h" + std::to_string(std::min(level, 4)), "", "md-h md-h" + std::to_string(level));
            h->set_attribute("data-md-anchor", markdown_anchor(markdown_plain_text(b.spans)));
            markdown_inline(*h, b.spans, dir);
            break;
        }
        case MdBlock::Kind::Paragraph:
            markdown_inline(*add(m_doc, parent, "p", "", "md-p"), b.spans, dir);
            break;
        case MdBlock::Kind::Code: {
            grove::Node* pre = add(m_doc, parent, "pre", b.code.empty() ? std::string(" ") : b.code, "md-code");
            if (!b.language.empty()) pre->set_attribute("title", b.language);
            break;
        }
        case MdBlock::Kind::Quote: {
            grove::Node* quote = add(m_doc, parent, "column", "", "md-quote");
            for (const MdBlock& c : b.children) markdown_block(*quote, c, dir, depth);
            break;
        }
        case MdBlock::Kind::List: {
            grove::Node* list = add(m_doc, parent, "column", "", b.ordered ? "md-list md-ordered" : "md-list");
            long long number = b.start;
            for (const MdItem& item : b.items) {
                grove::Node* row = add(m_doc, *list, "row", "", item.task ? "md-item md-task" : "md-item");
                std::string marker;
                if (item.task) marker = item.checked ? "\xE2\x98\x91" : "\xE2\x98\x90";
                else if (b.ordered) marker = std::to_string(number++) + ".";
                else marker = depth % 3 == 0 ? "\xE2\x80\xA2" : depth % 3 == 1 ? "\xE2\x97\xA6" : "\xE2\x96\xAA";
                add(m_doc, *row, "text", marker, "md-marker");
                grove::Node* body = add(m_doc, *row, "column", "", item.checked ? "md-item-body md-done" : "md-item-body");
                for (const MdBlock& c : item.blocks) markdown_block(*body, c, dir, depth + 1);
            }
            break;
        }
        case MdBlock::Kind::Rule:
            add(m_doc, parent, "hr", "", "md-rule");
            break;
        case MdBlock::Kind::Table: {
            grove::Node* table = add(m_doc, parent, "column", "", "md-table");
            for (std::size_t r = 0; r < b.rows.size(); ++r) {
                grove::Node* row = add(m_doc, *table, "row", "", r == 0 ? "md-tr md-th" : "md-tr");
                for (std::size_t c = 0; c < b.rows[r].size(); ++c) {
                    grove::Node* cell = add(m_doc, *row, "column", "", "md-cell");
                    grove::Node* p = add(m_doc, *cell, "p", "", "md-cell-text");
                    const MdAlign a = c < b.align.size() ? b.align[c] : MdAlign::None;
                    if (a == MdAlign::Center) p->set_attribute("style", "text-align: center");
                    else if (a == MdAlign::Right) p->set_attribute("style", "text-align: right");
                    markdown_inline(*p, b.rows[r][c], dir);
                }
            }
            break;
        }
        case MdBlock::Kind::Html: {
            std::size_t at = 0;
            const std::string& raw = b.code;
            while ((at = raw.find("<img", at)) != std::string::npos) {
                const std::size_t end = raw.find('>', at);
                const std::string tag = raw.substr(at, end == std::string::npos ? std::string::npos : end - at);
                std::string src;
                std::string alt;
                for (const char* name : { "src", "alt" }) {
                    const std::string key = std::string(name) + "=";
                    const std::size_t k = tag.find(key);
                    if (k == std::string::npos || k + key.size() >= tag.size()) continue;
                    const char q = tag[k + key.size()];
                    std::string value;
                    if (q == '"' || q == '\'') {
                        const std::size_t close = tag.find(q, k + key.size() + 1);
                        value = tag.substr(k + key.size() + 1, close == std::string::npos ? std::string::npos : close - k - key.size() - 1);
                    } else {
                        const std::size_t close = tag.find_first_of(" />", k + key.size());
                        value = tag.substr(k + key.size(), close == std::string::npos ? std::string::npos : close - k - key.size());
                    }
                    (std::string(name) == "src" ? src : alt) = value;
                }
                if (!src.empty()) markdown_image(*add(m_doc, parent, "p", "", "md-p md-html-image"), src, alt, dir);
                at = end == std::string::npos ? raw.size() : end;
            }
            if (!b.spans.empty()) markdown_inline(*add(m_doc, parent, "p", "", "md-p"), b.spans, dir);
            break;
        }
        }
    }

    void markdown_inline(grove::Node& parent, const std::vector<MdSpan>& spans, const std::string& dir) {
        for (const MdSpan& s : spans) {
            if (s.line_break) {
                add(m_doc, parent, "br", "");
                continue;
            }
            if (s.image) {
                markdown_image(parent, s.href, s.text, dir);
                continue;
            }
            grove::Node* at = &parent;
            if (!s.href.empty()) {
                at = add(m_doc, *at, "link", "", "md-link");
                at->set_attribute("data-md-href", s.href);
                at->set_attribute("title", s.href);
            }
            if (s.code) {
                add(m_doc, *at, "code", s.text, "md-inline-code");
                continue;
            }
            if (s.bold) at = add(m_doc, *at, "b", "");
            if (s.italic) at = add(m_doc, *at, "i", "");
            if (s.strike) at = add(m_doc, *at, "span", "", "md-strike");
            if (at == &parent) add(m_doc, parent, "text", s.text);
            else at->set_inner_text(s.text);
        }
    }

    static std::string percent_decode(const std::string& s) {
        std::string out;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
                out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
                i += 2;
            } else {
                out += s[i];
            }
        }
        return out;
    }

    static bool has_scheme(const std::string& href) {
        const std::size_t colon = href.find(':');
        if (colon == std::string::npos || colon < 2) return false;
        for (std::size_t i = 0; i < colon; ++i) {
            const char c = href[i];
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') return false;
        }
        return true;
    }

    static std::string local_target(const std::string& href, const std::string& dir) {
        std::string target = href.substr(0, href.find_first_of("?#"));
        target = percent_decode(target);
        if (target.empty()) return std::string();
        const fs::path p(target);
        return (p.is_absolute() ? p : fs::path(dir) / p).lexically_normal().string();
    }

    void markdown_image(grove::Node& parent, const std::string& src, const std::string& alt, const std::string& dir) {
        std::error_code ec;
        const std::string local = has_scheme(src) ? std::string() : local_target(src, dir);
        if (!local.empty() && fs::is_regular_file(local, ec) && is_image_path(local)) {
            grove::Node* img = m_doc.create_element("img");
            img->set_attribute("src", local);
            img->set_attribute("class", "md-img");
            if (!alt.empty()) img->set_attribute("title", alt);
            m_doc.append_child(parent, *img);
            return;
        }
        const std::string label = "[" + (alt.empty() ? std::string("image") : alt) + "]";
        if (has_scheme(src)) {
            grove::Node* link = add(m_doc, parent, "link", label, "md-link md-image-link");
            link->set_attribute("data-md-href", src);
            link->set_attribute("title", src);
        } else {
            add(m_doc, parent, "text", label, "md-missing")->set_attribute("title", "Can't find " + src);
        }
    }

    void open_markdown_link(const std::string& from, const std::string& href) {
        if (href.empty() || href[0] == '#') return;
        if (has_scheme(href)) {
            if (!open_in_browser(href)) notice("Couldn't open " + href);
            return;
        }
        const std::string target = local_target(href, fs::path(from).parent_path().string());
        std::error_code ec;
        if (target.empty()) return;
        if (fs::is_regular_file(target, ec)) open_file(target);
        else if (fs::is_directory(target, ec)) reveal_in_file_manager(target);
        else notice("Can't find " + target);
    }

    void render_binary(const std::string& path, Viewer& v) {
        viewer_bar(v, display_name(path), v.binary.summary);
        grove::Node* bar = add(m_doc, *v.root, "row", "", "viewer-bar sub-bar");
        grove::Node* tabs = add(m_doc, *bar, "row", "", "view-tabs");
        view_button(*tabs, "Info", "view-info", "What this file is", v.info);
        view_button(*tabs, "Hex", "view-hex", "Bytes in hexadecimal", !v.info && v.bytes == ByteView::Hex);
        view_button(*tabs, "Binary", "view-bits", "Bytes as bits", !v.info && v.bytes == ByteView::Bits);
        view_button(*tabs, "Strings", "view-strings", "Readable text inside the file", !v.info && v.bytes == ByteView::Strings);
        grove::Node* pager = add(m_doc, *bar, "row", "", "view-pager");
        add(m_doc, *bar, "div", "", "viewer-spacer");
        const ViewerKind natural = viewer_kind_for(path);
        if (natural == ViewerKind::Image || natural == ViewerKind::Pdf) view_button(*bar, "Preview", "open-preview", "Show it as a picture");
        view_button(*bar, "Text", "open-text", "Open it in the text editor anyway");

        if (v.info) {
            v.body = add(m_doc, *v.root, "column", "", "viewer-body binary-info");
            for (const FileFact& fact : v.binary.facts) {
                grove::Node* row = add(m_doc, *v.body, "row", "", "fact");
                add(m_doc, *row, "text", fact.label, "fact-label");
                add(m_doc, *row, "text", fact.value, "fact-value");
            }
            if (!v.binary.sections.empty()) {
                add(m_doc, *v.body, "h3", "Sections", "info-heading");
                add(m_doc, *v.body, "pre", sections_table(v.binary.sections), "info-table");
            }
            if (!v.binary.libraries.empty()) {
                const std::string kind = v.binary.kind;
                const bool archive = kind.find("archive") != std::string::npos || kind.find("ZIP") != std::string::npos || kind.find("package") != std::string::npos || kind.find("document") != std::string::npos || kind.find("book") != std::string::npos || kind.find("wheel") != std::string::npos || kind.find("extension") != std::string::npos;
                add(m_doc, *v.body, "h3", archive ? "Files" : "Libraries it loads", "info-heading");
                std::string list;
                for (const std::string& lib : v.binary.libraries) list += lib + "\n";
                add(m_doc, *v.body, "pre", list, "info-table");
            }
            return;
        }

        const std::size_t page = byte_view_page_size(v.bytes);
        if (v.offset >= v.size) v.offset = v.size == 0 ? 0 : (v.size - 1) / page * page;
        v.offset = v.offset / page * page;
        const std::uint64_t end = std::min<std::uint64_t>(v.size, v.offset + page);
        const bool first = v.offset == 0;
        const bool last = end >= v.size;
        view_button(*pager, "\xE2\x8F\xAE", "page-first", "Start of the file", false, !first);
        view_button(*pager, "\xE2\x97\x82", "page-prev", "Previous " + human_size(page), false, !first);
        add(m_doc, *pager, "text", v.size == 0 ? std::string("empty") : hex_offset(v.offset, v.size) + "\xE2\x80\x93" + hex_offset(end == 0 ? 0 : end - 1, v.size), "viewer-meta");
        view_button(*pager, "\xE2\x96\xB8", "page-next", "Next " + human_size(page), false, !last);
        view_button(*pager, "\xE2\x8F\xAD", "page-last", "End of the file", false, !last);

        const std::string bytes = read_file_range(path, v.offset, static_cast<std::size_t>(end - v.offset)).value_or("");
        const ByteDump dump = byte_dump(v.bytes, bytes, v.offset, v.size);
        grove::Node* code = m_doc.create_element("input_code");
        code->set_attribute("class", "dump");
        code->set_attribute("readonly", "");
        code->set_attribute("language", "plain");
        code->set_attribute("line-numbers", "false");
        grove::set_inline_style(m_doc, *code, "font-size", editor_font_size(m_ws.settings, m_zoom));
        const std::string family = m_ws.settings.get_or("editor", "font-family", "monospace");
        grove::set_inline_style(m_doc, *code, "font-family", family.empty() ? "" : "\"" + family + "\"");
        apply_highlight(m_doc, *code, m_ws.settings, m_themes, "plain");
        std::string text = dump.text;
        if (text.empty()) text = v.bytes == ByteView::Strings ? "No readable text in this part of the file.\n" : "The file is empty.\n";
        code->set_attribute("value", text);
        if (!dump.spans.empty()) code->set_attribute("semantic", grove::code_spans_text(text_lines(text), dump.spans));
        v.body = code;
        m_doc.append_child(*v.root, *code);
    }

    void start_render(const std::string& path) {
        const auto it = m_viewers.find(path);
        if (it == m_viewers.end()) return;
        Viewer& v = it->second;
        if (v.job && v.job->running()) {
            v.again = true;
            return;
        }
        const std::optional<ViewerTool> tool = find_viewer_tool(m_ws.settings, "pdf");
        if (!tool) {
            const std::string chosen = m_ws.settings.get_or("viewer", "pdf", "auto");
            v.status = chosen != "auto" && !chosen.empty() ? "viewer { pdf: \"" + chosen + "\"; } isn't a program Canopy can run.\n" + viewer_install_hint("pdf")
                                                           : "Canopy needs a PDF renderer to show the pages, and none was found.\n" + viewer_install_hint("pdf");
            v.phase = Viewer::Phase::Idle;
            render_viewer(path);
            return;
        }
        std::error_code ec;
        const fs::path dir = fs::path(viewer_cache_dir(v.source.empty() ? path : v.source)) / ("pages-" + std::to_string(++v.serial));
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        v.pending_dir = dir.string();
        v.dpi = pdf_resolution(m_ws.settings);
        v.tool = tool->name;
        v.phase = Viewer::Phase::Rendering;
        if (!v.job) v.job = std::make_unique<BackgroundJob>();
        v.log.clear();
        v.job->start({ pdf_render_command(*tool, v.pdf, v.pending_dir, v.dpi) });
        render_viewer(path);
    }

    void start_latex(const std::string& path) {
        const auto it = m_viewers.find(path);
        OpenFile* f = find_file(path);
        if (it == m_viewers.end() || !f || !f->editor) return;
        Viewer& v = it->second;
        if (v.job && v.job->running()) {
            v.again = true;
            return;
        }
        const std::string text = grove::input_string(*f->editor).value_or("");
        const std::string root = latex_root_file(path, text);
        const std::string root_text = normalized(root) == path ? text : read_text_file(root).value_or("");
        if (!latex_is_document(root_text)) {
            v.status = root == path ? std::string("This file has no \\documentclass, so it can't be previewed on its own.\nIf it's part of a bigger document, add a first line like: % !TEX root = main.tex")
                                    : "The root file " + display_name(root) + " (from % !TEX root) has no \\documentclass, or couldn't be read.";
            v.phase = Viewer::Phase::Idle;
            render_viewer(path);
            return;
        }
        const std::optional<ViewerTool> tool = find_viewer_tool(m_ws.settings, "latex");
        if (!tool) {
            const std::string chosen = m_ws.settings.get_or("viewer", "latex", "auto");
            v.status = chosen != "auto" && !chosen.empty() ? "viewer { latex: \"" + chosen + "\"; } isn't a program Canopy can run.\n" + viewer_install_hint("latex")
                                                           : "Canopy needs TeX to preview LaTeX, and none was found.\n" + viewer_install_hint("latex");
            v.phase = Viewer::Phase::Idle;
            render_viewer(path);
            return;
        }
        std::error_code ec;
        const std::string out = (fs::path(viewer_cache_dir(root)) / "build").string();
        fs::create_directories(out, ec);
        v.source = root;
        v.pdf = latex_pdf_path(root, out);
        v.tool = tool->name;
        v.phase = Viewer::Phase::Building;
        v.log.clear();
        if (!v.job) v.job = std::make_unique<BackgroundJob>();
        v.job->start({ latex_build_command(*tool, root, out, m_ws.settings.get_or("viewer", "latex-flags", "")) }, fs::path(root).parent_path().string());
        render_viewer(path);
    }

    void latex_saved(const std::string& saved) {
        const std::string ext = fs::path(saved).extension().string();
        if (ext != ".tex" && ext != ".bib" && ext != ".sty" && ext != ".cls" && ext != ".latex" && ext != ".ltx") return;
        for (auto& [path, v] : m_viewers) {
            if (v.kind != ViewerKind::Latex || v.collapsed) continue;
            const std::string folder = fs::path(v.source.empty() ? path : v.source).parent_path().string();
            if (path == saved || fs::path(saved).parent_path().string().rfind(folder, 0) == 0) start_latex(path);
        }
    }

    void poll_viewers() {
        std::vector<std::string> paths;
        for (auto& [path, v] : m_viewers) {
            if (v.job) paths.push_back(path);
        }
        for (const std::string& path : paths) {
            const auto it = m_viewers.find(path);
            if (it == m_viewers.end() || !it->second.job) continue;
            Viewer& v = it->second;
            v.log += v.job->take_output();
            const std::optional<int> result = v.job->take_result();
            if (!result) continue;
            if (v.phase == Viewer::Phase::Building) finish_latex(path, *result);
            else if (v.phase == Viewer::Phase::Rendering) finish_render(path, *result);
        }
    }

    void finish_render(const std::string& path, int code) {
        Viewer& v = m_viewers[path];
        std::error_code ec;
        const std::vector<std::string> pages = rendered_pages(v.pending_dir);
        if (!pages.empty()) {
            if (!v.render_dir.empty() && v.render_dir != v.pending_dir) fs::remove_all(v.render_dir, ec);
            v.render_dir = v.pending_dir;
            v.pages = pages;
            v.page = image_info(read_file_range(pages.front(), 0, 4096).value_or(""), pages.front());
            if (v.kind == ViewerKind::Pdf || v.status.rfind("Couldn't draw", 0) == 0) v.status.clear();
        } else {
            fs::remove_all(v.pending_dir, ec);
            std::string tail = v.log;
            if (tail.size() > 600) tail = "\xE2\x80\xA6" + tail.substr(tail.size() - 600);
            v.status = "Couldn't draw the pages (" + v.tool + " exited with " + std::to_string(code) + ").\n" + tail;
        }
        v.pending_dir.clear();
        v.phase = Viewer::Phase::Idle;
        render_viewer(path);
        if (v.again) {
            v.again = false;
            if (v.kind == ViewerKind::Latex) start_latex(path);
            else start_render(path);
        }
    }

    void finish_latex(const std::string& path, int code) {
        Viewer& v = m_viewers[path];
        const std::vector<CompilerMessage> messages = parse_latex_log(v.log, v.source);
        if (messages.empty()) m_live.erase(path);
        else m_live[path] = messages;
        if (OpenFile* f = find_file(path); f && f->editor) {
            const std::string text = grove::input_string(*f->editor).value_or("");
            const std::vector<std::string> lines = text_lines(text);
            std::vector<grove::CodeProblem> marks;
            for (const CompilerMessage& m : messages) {
                if (m.line == 0 || m.line > lines.size() || normalized(m.file) != path) continue;
                const std::string& row = lines[m.line - 1];
                std::size_t start = row.find_first_not_of(" \t");
                if (start == std::string::npos) start = 0;
                marks.push_back({ m.line - 1, start, row.size() > start ? row.size() - start : 1, m.severity == "warning" ? grove::CodeProblemKind::Warning : grove::CodeProblemKind::Error });
            }
            m_marks[path] = marks;
            update_marks(*f);
        }
        refresh_problems();
        std::size_t errors = 0;
        for (const CompilerMessage& m : messages) errors += m.severity == "error" ? 1 : 0;
        std::error_code ec;
        if (fs::is_regular_file(v.pdf, ec)) {
            v.status = code != 0 ? "Couldn't build it cleanly: showing the last PDF LaTeX produced. The errors are in Problems." : std::string();
            v.phase = Viewer::Phase::Idle;
            if (code != 0) {
                m_output += "LaTeX (" + v.tool + ") for " + display_name(v.source) + ":\n" + v.log + "\n";
                sync_output();
            }
            const bool again = v.again;
            v.again = false;
            start_render(path);
            v.again = again;
            return;
        }
        m_output += "LaTeX (" + v.tool + ") for " + display_name(v.source) + ":\n" + v.log + "\n";
        sync_output();
        std::string tail = v.log;
        if (tail.size() > 600) tail = "\xE2\x80\xA6" + tail.substr(tail.size() - 600);
        v.status = errors > 0 ? "LaTeX couldn't build " + display_name(v.source) + ". The errors are in Problems, and the full log is in Output." : "LaTeX couldn't build " + display_name(v.source) + " (" + v.tool + " exited with " + std::to_string(code) + ").\n" + tail;
        v.pages.clear();
        v.phase = Viewer::Phase::Idle;
        render_viewer(path);
        if (errors > 0) show_panel("problems");
        if (v.again) {
            v.again = false;
            start_latex(path);
        }
    }

    void set_collapsed(const std::string& path, bool collapsed) {
        Viewer& v = m_viewers[path];
        v.collapsed = collapsed;
        render_viewer(path);
        if (v.kind == ViewerKind::Latex && !collapsed && v.pages.empty() && v.phase == Viewer::Phase::Idle) start_latex(path);
    }

    void step_zoom(Viewer& v, bool in) {
        const std::vector<int>& steps = zoom_steps();
        const int current = v.zoom == 0 ? 100 : v.zoom;
        if (in) {
            const auto next = std::upper_bound(steps.begin(), steps.end(), current);
            v.zoom = next == steps.end() ? steps.back() : *next;
        } else {
            const auto next = std::lower_bound(steps.begin(), steps.end(), current);
            v.zoom = next == steps.begin() ? steps.front() : *(next - 1);
        }
    }

    void viewer_click(grove::Event& e) {
        if (const grove::Node* link = with_attribute(e.target, "data-md-href")) {
            const grove::Node* owner = with_attribute(link, "data-viewer");
            open_markdown_link(owner ? owner->attribute_or("data-viewer", "") : m_active, link->attribute_or("data-md-href", ""));
            e.default_prevented = true;
            return;
        }
        const grove::Node* button = with_attribute(e.target, "data-view-action");
        if (!button) return;
        const grove::Node* owner = with_attribute(button, "data-viewer");
        if (!owner) return;
        const std::string path = owner->attribute_or("data-viewer", "");
        const std::string action = button->attribute_or("data-view-action", "");
        const auto it = m_viewers.find(path);
        if (it == m_viewers.end()) return;
        Viewer& v = it->second;
        if (action == "zoom-in") step_zoom(v, true);
        else if (action == "zoom-out") step_zoom(v, false);
        else if (action == "zoom-fit") v.zoom = 0;
        else if (action == "zoom-100") v.zoom = 100;
        else if (action == "view-info") v.info = true;
        else if (action == "view-hex" || action == "view-bits" || action == "view-strings") {
            const ByteView view = action == "view-hex" ? ByteView::Hex : action == "view-bits" ? ByteView::Bits : ByteView::Strings;
            v.info = false;
            v.bytes = view;
            const std::size_t page = byte_view_page_size(view);
            v.offset = v.offset / page * page;
        } else if (action == "page-first") v.offset = 0;
        else if (action == "page-prev") {
            const std::size_t page = byte_view_page_size(v.bytes);
            v.offset = v.offset >= page ? v.offset - page : 0;
        } else if (action == "page-next") v.offset += byte_view_page_size(v.bytes);
        else if (action == "page-last") v.offset = v.size;
        else if (action == "rebuild") {
            if (v.kind == ViewerKind::Latex) {
                if (m_ws.settings.flag("build", "save-before-build", true)) save(path);
                start_latex(path);
            } else {
                start_render(path);
            }
            e.default_prevented = true;
            return;
        } else if (action == "toggle-preview") {
            set_collapsed(path, !v.collapsed);
            return;
        } else if (action == "open-pdf") {
            const std::string pdf = v.pdf;
            if (!pdf.empty()) open_file(pdf);
            return;
        } else if (action == "open-text") {
            reopen_as(path, ViewerKind::Text);
            return;
        } else if (action == "open-hex") {
            reopen_as(path, ViewerKind::Binary);
            return;
        } else if (action == "open-preview") {
            reopen_as(path, std::nullopt);
            return;
        } else {
            return;
        }
        e.default_prevented = true;
        render_viewer(path);
    }

    void register_natives() {
        grove::register_native(m_doc, "canopy.version", [](const std::vector<std::string>&) { return std::string(CANOPY_VERSION_STRING); });
        grove::register_native(m_doc, "canopy.workspace", [this](const std::vector<std::string>&) { return m_ws.root; });
        grove::register_native(m_doc, "canopy.active_file", [this](const std::vector<std::string>&) { return m_active; });
        grove::register_native(m_doc, "canopy.settings_path", [](const std::vector<std::string>&) { return user_settings_path(); });
        grove::register_native(m_doc, "canopy.open_folder", [this](const std::vector<std::string>& args) {
            if (args.empty()) return std::string();
            open_folder(args[0]);
            return m_ws.root;
        });
        grove::register_native(m_doc, "canopy.open_file", [this](const std::vector<std::string>& args) {
            if (args.empty()) return std::string();
            const std::size_t line = args.size() > 1 ? static_cast<std::size_t>(grove::parse_integer(args[1]).value_or(0)) : 0;
            open_file(args[0], line);
            return m_active;
        });
        grove::register_native(m_doc, "canopy.open_as", [this](const std::vector<std::string>& args) {
            if (args.empty()) return std::string();
            const std::string path = normalized(args[0]);
            std::optional<ViewerKind> kind;
            if (args.size() > 1 && args[1] != "auto") kind = viewer_kind_from_name(args[1]);
            if (args.size() > 1 && args[1] != "auto" && !kind) return std::string();
            reopen_as(path, kind);
            return m_active;
        });
        grove::register_native(m_doc, "canopy.save", [this](const std::vector<std::string>&) { return std::string(save_active() ? "saved" : ""); });
        grove::register_native(m_doc, "canopy.build", [this](const std::vector<std::string>&) {
            build(false);
            return std::string("started");
        });
        grove::register_native(m_doc, "canopy.run", [this](const std::vector<std::string>&) {
            build(true);
            return std::string("started");
        });
        grove::register_native(m_doc, "canopy.show_panel", [this](const std::vector<std::string>& args) {
            if (!args.empty()) show_panel(args[0]);
            return std::string();
        });
    }

    grove::Project& m_project;
    grove::Document& m_doc;
    Workspace m_ws;
    std::vector<OpenFile> m_files;
    std::vector<Theme> m_themes;
    std::unique_ptr<SettingsPage> m_settings;
    std::vector<std::string> m_library_modules;
    bool m_library_modules_checked = false;
    DeveloperPage m_developer{ m_doc };
    std::uint64_t m_developer_timer = 0;
    LocalHistory m_history;
    struct Split {
        grove::Node* target = nullptr;
        std::string key;
        int sign = 1;
        bool across = true;
        bool target_across = true;
        const grove::Node* node = nullptr;
        bool active = false;
        double start = 0.0;
        double size = 0.0;
        double current = 0.0;
    };
    struct TabDrag {
        std::string view;
        double x = 0.0;
        double y = 0.0;
        bool active = false;
        bool started = false;
    };
    std::map<const grove::Node*, Split> m_splits;
    Split m_split_drag;
    TabDrag m_tab_drag;
    std::map<std::string, std::string> m_view_area;
    std::map<std::string, std::string> m_area_active;
    std::string m_tab_menu_view;
    std::string m_tab_menu_area;
    std::unique_ptr<GitPanel> m_git;
    std::unique_ptr<ConsolePanel> m_console;
    std::unique_ptr<ElementsPanel> m_elements;
    SemanticJob m_web_check;
    std::string m_web_check_source;
    std::map<std::string, double> m_web_due;
    std::map<std::string, std::vector<CompilerMessage>> m_web_lint;
    std::map<std::string, std::vector<CompilerMessage>> m_web_syntax;
    std::map<std::string, std::vector<CompilerMessage>> m_web_problems;
    std::unique_ptr<FindBar> m_find;
    std::string m_names_file;
    ProjectNames m_names;
    fs::file_time_type m_names_time{};
    std::string m_after_task;
    std::string m_build_folder;
    std::string m_active;
    std::set<std::string> m_expanded;
    std::vector<CompilerMessage> m_messages;
    BackgroundJob m_job;
    SemanticJob m_semantic;
    SemanticJob m_walnut_complete;
    std::optional<WalnutCompletionAsk> m_complete_ask;
    std::string m_complete_running_key;
    std::string m_complete_last_key;
    CompletionRequest m_complete_last;
    std::map<std::string, WalnutCompletionCache> m_walnut_completions;
    CodeIntel m_intel;
    std::map<std::string, WalnutInfo> m_walnut;
    std::map<std::string, std::size_t> m_overload_page;
    std::map<std::string, std::pair<std::size_t, std::size_t>> m_carets;
    std::vector<Place> m_back;
    std::vector<Place> m_forward;
    std::string m_hover_path;
    std::size_t m_hover_line = 0;
    std::size_t m_hover_column = 0;
    std::map<std::string, double> m_semantic_due;
    std::map<std::string, double> m_markdown_due;
    std::vector<std::unique_ptr<LiveServer>> m_servers;
    std::map<std::size_t, std::string> m_server_last;
    bool m_ports_ready = false;
    std::string m_menu_terminal;
    std::map<std::string, std::vector<CompilerMessage>> m_live;
    std::string m_output;
    std::string m_running_label;
    struct RunRecord {
        std::string file;
        bool program = false;
        std::vector<RunStats> steps;
    };
    std::vector<RunRecord> m_runs;
    std::size_t m_job_steps = 0;
    bool m_job_program = false;
    std::string m_job_file;
    std::size_t m_caret_serial = 0;
    std::map<std::string, Viewer> m_viewers;
    std::map<std::string, std::vector<grove::CodeProblem>> m_marks;
    double m_zoom = 1.0;
    std::map<std::string, ViewerKind> m_open_as;
    const grove::Node* m_tab_anchor = nullptr;
    std::size_t m_file_slot = static_cast<std::size_t>(-1);
    std::vector<std::string> m_roots;
    std::string m_panel_menu_view;
    std::string m_panel_menu_item;
    std::string m_explorer_sig;
    std::set<std::string> m_collapsed_roots;
    std::set<std::string> m_selected;
    std::string m_anchor;
    std::vector<std::string> m_visible;
    std::map<std::string, grove::Node*> m_items;
    std::size_t m_explorer_at = 0;
    std::vector<ShortcutBinding> m_bindings;
    std::uint64_t m_bindings_revision = 0;
    std::map<std::string, fs::file_time_type> m_explorer_times;
    std::map<std::string, std::string> m_explorer_lists;
    std::map<std::string, grove::Node*> m_explorer_old;
    Drag m_drag;
    grove::Node* m_drop_node = nullptr;
    double m_suppress_click_until = 0.0;
    std::vector<std::string> m_clip;
    bool m_clip_cut = false;
    std::vector<std::string> m_menu_paths;
    std::string m_menu_folder;
    std::vector<std::unique_ptr<TerminalSession>> m_terms;
    std::size_t m_term = 0;
    std::size_t m_term_serial = 0;
    std::size_t m_log_serial = 0;
    std::string m_log_owner;
    std::string m_log_text;
};

} // namespace

std::string find_ui_document() {
    std::error_code ec;
    if (const char* over = std::getenv("CANOPY_UI_DIR")) {
        const fs::path p = fs::path(over) / "canopy.grove";
        if (fs::is_regular_file(p, ec)) return p.string();
    }
    const fs::path exe_dir = executable_dir();
    const fs::path installed = exe_dir.parent_path() / "share" / "canopy" / "ui" / "canopy.grove";
    if (fs::is_regular_file(installed, ec)) return installed.string();
    const fs::path beside = exe_dir / "ui" / "canopy.grove";
    if (fs::is_regular_file(beside, ec)) return beside.string();
#ifdef CANOPY_SOURCE_UI_DIR
    const fs::path source = fs::path(CANOPY_SOURCE_UI_DIR) / "canopy.grove";
    if (fs::is_regular_file(source, ec)) return source.string();
#endif
    return std::string();
}

std::string toolchain_summary(const Toolchain& toolchain) {
    return tool_label(toolchain, "cpp", "C++") + "  \xC2\xB7  " + tool_label(toolchain, "c", "C") + "  \xC2\xB7  " +
           tool_label(toolchain, "walnut", "Walnut") + "  \xC2\xB7  " + tool_label(toolchain, "grove", "Grove");
}

void populate_shell(grove::Project& project, const Workspace& workspace) {
    grove::Document& doc = project.document;
    const std::string name = fs::path(workspace.root).filename().string();

    set_text(doc, "workspace-name", name.empty() ? workspace.root : name);
    set_text(doc, "status-toolchain", toolchain_summary(workspace.toolchain));
    set_text(doc, "status-version", std::string("Canopy ") + CANOPY_VERSION_STRING);

    if (grove::Node* explorer = doc.find_by_id("explorer")) {
        clear(doc, *explorer);
        for (const WorkspaceEntry& e : workspace.list(workspace.root)) {
            std::string classes = e.directory ? "entry folder" : "entry file";
            if (!e.directory) {
                if (const Language* lang = language_for_path(e.path)) classes += " lang-" + lang->key;
            }
            grove::Node* item = add(doc, *explorer, "item", e.directory ? e.name + "/" : e.name, classes);
            item->set_attribute("data-path", e.path);
        }
    }

    if (grove::Node* problems = doc.find_by_id("problems")) {
        clear(doc, *problems);
        std::size_t count = 0;
        for (const ToolProblem& p : workspace.toolchain.problems) {
            add(doc, *problems, "p", p.message, "problem");
            ++count;
        }
        for (const grove::Diagnostic& d : workspace.diagnostics.items()) {
            add(doc, *problems, "p", d.file + ":" + std::to_string(d.span.begin.line) + ": " + d.message, "problem");
            ++count;
        }
        if (count == 0) add(doc, *problems, "p", "Everything's set up and ready to build.", "ok");
    }
}

int run_shell(const Workspace& workspace) {
    return run_shell(workspace, {});
}

int run_shell(const Workspace& workspace, const std::vector<std::string>& files) {
    const std::string ui = find_ui_document();
    if (ui.empty()) {
        std::cerr << "canopy: can't find the editor UI (canopy.grove); set CANOPY_UI_DIR to the folder that holds it\n";
        return 1;
    }
    grove::Project project = grove::load_project(ui);
    if (!project.diagnostics.ok()) {
        project.diagnostics.print(std::cerr);
        return 1;
    }
    std::error_code ec;
    if (fs::is_directory(workspace.root, ec)) remember_folder(workspace.root);
    auto shell = std::make_unique<EditorShell>(project, workspace);
    shell->install();
    for (const std::string& f : files) shell->open_file(f);
    grove::runtime::RunOptions options;
    options.scripts = false;
    return grove::runtime::run(project, options);
}

int dump_shell(const Workspace& workspace, std::ostream& os) {
    const std::string ui = find_ui_document();
    if (ui.empty()) {
        std::cerr << "canopy: can't find the editor UI (canopy.grove)\n";
        return 1;
    }
    grove::Project project = grove::load_project(ui);
    project.diagnostics.print(std::cerr);
    if (!project.diagnostics.ok()) return 1;
    EditorShell shell(project, workspace);
    shell.install();
    grove::dump(project.document.root(), os, grove::DumpOptions::full());
    return 0;
}

} // namespace canopy
