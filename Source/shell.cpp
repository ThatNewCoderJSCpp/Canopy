#include <canopy/shell.hpp>

#include <canopy/build.hpp>
#include <canopy/code_intel.hpp>
#include <canopy/desktop.hpp>
#include <canopy/languages.hpp>
#include <canopy/settings_page.hpp>
#include <canopy/shortcuts.hpp>
#include <canopy/version.hpp>
#include <canopy/viewers.hpp>
#include <canopy/walnut_semantic.hpp>

#include <grove/dom_tools.hpp>
#include <grove/input/code_buffer.hpp>
#include <grove/input/input.hpp>
#include <grove/native.hpp>
#include <grove/runtime/runtime.hpp>
#include <grove/windowing.hpp>

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
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

const std::string k_settings_tab = "canopy:settings";

grove::Node* add(grove::Document& doc, grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes = {}) {
    grove::Node* el = doc.create_element(tag);
    if (!classes.empty()) el->set_attribute("class", classes);
    if (!text.empty()) el->set_inner_text(text);
    return doc.append_child(parent, *el);
}

void set_text(grove::Document& doc, const std::string& id, const std::string& text) {
    if (grove::Node* el = doc.find_by_id(id)) {
        el->set_inner_text(text);
        doc.touch();
    }
}

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
        populate_shell(m_project, m_ws);
        refresh_all();
        listen_by_id("explorer", "click", [this](grove::Event& e) { explorer_click(e); });
        listen_by_id("recent", "click", [this](grove::Event& e) {
            if (const grove::Node* item = with_attribute(e.target, "data-path")) open_folder(item->attribute_or("data-path", ""));
        });
        listen_by_id("tabs", "click", [this](grove::Event& e) { tab_click(e); });
        listen_by_id("panel-tabs", "click", [this](grove::Event& e) {
            for (const grove::Node* n = e.target; n; n = n->parent()) {
                const std::string id = n->id();
                if (id.rfind("tab-", 0) == 0) {
                    show_panel(id.substr(4));
                    return;
                }
            }
        });
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
        listen_by_id("welcome-settings", "click", [this](grove::Event&) { open_settings(); });
        listen_by_id("build-button", "click", [this](grove::Event&) { build(false); });
        listen_by_id("run-button", "click", [this](grove::Event&) { build(true); });
        listen_by_id("editors", "input", [this](grove::Event& e) { editor_input(e); });
        listen_by_id("editors", "cursor", [this](grove::Event& e) {
            set_text(m_doc, "status-cursor", "Ln " + std::to_string(static_cast<long>(e.x)) + ", Col " + std::to_string(static_cast<long>(e.y)));
            if (const grove::Node* editor = with_attribute(e.target, "data-path")) m_carets[editor->attribute_or("data-path", "")] = { static_cast<std::size_t>(e.x), static_cast<std::size_t>(e.y) };
        });
        listen_by_id("editors", "hover", [this](grove::Event& e) { editor_hover(e); });
        listen_by_id("editors", "goto", [this](grove::Event& e) { editor_goto(e); });
        listen_by_id("editors", "hover_action", [this](grove::Event& e) { editor_hover_action(e); });
        listen_by_id("editors", "click", [this](grove::Event& e) { viewer_click(e); });
        setup_intel();
        if (grove::Node* body = m_doc.first_by_tag("body")) {
            m_doc.events().add_listener(body, "key_down", [this](grove::Event& e) { key_down(e); });
        }
        SettingsPage::Hooks hooks;
        hooks.changed = [this] { reload_settings(); };
        hooks.open_file = [this](const std::string& path) { open_file(path); };
        m_settings = std::make_unique<SettingsPage>(m_doc, m_ws, m_themes, std::move(hooks));
        m_doc.events().set_interval(100.0, [this] { poll_job(); });
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
        remember_folder(m_ws.root);
        populate_shell(m_project, m_ws);
        for (OpenFile& f : m_files) {
            if (f.editor && f.path != k_settings_tab) apply_editor_settings(*f.editor);
        }
        refresh_viewers();
        apply_gui();
        refresh_theme_picker();
        if (m_settings) m_settings->refresh();
        refresh_all();
    }

    void open_file(const std::string& raw_path, std::size_t line = 0, std::size_t column = 0) {
        if (raw_path.empty()) return;
        const std::string path = normalized(raw_path);
        OpenFile* file = find_file(path);
        if (!file) {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) {
                notice("Can't open " + path);
                return;
            }
            const auto chosen = m_open_as.find(path);
            const ViewerKind kind = chosen != m_open_as.end() ? chosen->second : viewer_kind_for(path);
            if (kind == ViewerKind::Image || kind == ViewerKind::Pdf || kind == ViewerKind::Binary) {
                if (!open_viewer(path, kind)) return;
            } else if (!open_text(path, kind)) {
                return;
            }
            file = find_file(path);
            if (!file) return;
        }
        activate(path);
        if (line > 0 && file->editor && file->path != k_settings_tab) file->editor->set_attribute("caret", std::to_string(line) + ":" + std::to_string(std::max<std::size_t>(1, column)) + "#" + std::to_string(++m_caret_serial));
        grove::Document& doc = m_doc;
        if (doc.host().focus && file->editor) doc.host().focus(file->editor);
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
        if (kind == ViewerKind::Latex) {
            view = m_doc.create_element("row");
            view->set_attribute("class", "tex-split hidden");
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
    }

    void close_file(const std::string& path) {
        auto it = std::find_if(m_files.begin(), m_files.end(), [&](const OpenFile& f) { return f.path == path; });
        if (it == m_files.end()) return;
        if (path == k_settings_tab && m_settings) m_settings->forget();
        m_semantic_due.erase(path);
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
        if (path == k_settings_tab) return true;
        OpenFile* f = find_file(path);
        if (!f) return false;
        if (!f->editor) return true;
        const std::string text = grove::input_string(*f->editor).value_or("");
        if (!write_text_file(f->path, text)) {
            notice("Couldn't save " + f->path);
            return false;
        }
        f->saved = text;
        set_dirty(*f, false);
        if (is_settings_file(f->path)) reload_settings();
        latex_saved(f->path);
        return true;
    }

    bool save_active() { return !m_active.empty() && save(m_active); }

    void build(bool run) {
        if (m_job.running()) {
            notice("A build is already running");
            return;
        }
        if (m_active.empty() || m_active == k_settings_tab) {
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
        if (m_ws.settings.flag("build", "save-before-build", true)) save_active();
        BuildRequest request;
        request.file = m_active;
        request.workspace_root = m_ws.root;
        const BuildPlan plan = plan_build(request, m_ws.settings, m_ws.toolchain);
        m_output.clear();
        m_messages.clear();
        if (!plan.ok()) {
            m_output = plan.error + "\n";
            set_text(m_doc, "output-log", m_output);
            show_panel("output");
            refresh_problems();
            return;
        }
        std::error_code ec;
        fs::create_directories(fs::path(plan.output).parent_path(), ec);
        std::vector<std::vector<std::string>> steps{ plan.command };
        if (run) steps.push_back({ fs::absolute(plan.output, ec).string() });
        m_running_label = std::string(run ? "Running " : "Building ") + display_name(m_active);
        m_output = m_running_label + "\xE2\x80\xA6\n";
        set_text(m_doc, "output-log", m_output);
        set_text(m_doc, "status-problems", m_running_label + "\xE2\x80\xA6");
        show_panel("output");
        refresh_problems();
        m_job.start(std::move(steps));
    }

    void show_panel(const std::string& name) {
        for (const char* p : { "terminal", "problems", "warnings", "output", "ports", "debug" }) {
            const bool on = name == p;
            if (grove::Node* tab = m_doc.find_by_id(std::string("tab-") + p)) on ? (void)grove::add_class(m_doc, *tab, "active") : (void)grove::remove_class(m_doc, *tab, "active");
            if (grove::Node* view = m_doc.find_by_id(std::string("view-") + p)) on ? (void)grove::add_class(m_doc, *view, "active") : (void)grove::remove_class(m_doc, *view, "active");
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
    };

    static grove::Node* shown(const OpenFile& f) { return f.view ? f.view : f.editor; }

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
        schedule_semantic(f->path, 0.35);
    }

    void key_down(grove::Event& e) {
        if (m_settings && m_settings->record_key(e)) {
            e.default_prevented = true;
            return;
        }
        const std::optional<std::string> command = command_for(shortcut_bindings(m_ws.settings), grove::chord_of(e.key, e.modifiers));
        if (!command || !run_command(*command)) return;
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
        else if (command == "definition") goto_caret();
        else if (command == "go-back") go_back();
        else if (command == "go-forward") go_forward();
        else return false;
        return true;
    }

    void explorer_click(grove::Event& e) {
        const grove::Node* item = with_attribute(e.target, "data-path");
        if (!item) return;
        const std::string path = item->attribute_or("data-path", "");
        if (item->has_class("folder")) {
            if (m_expanded.count(path)) m_expanded.erase(path);
            else m_expanded.insert(path);
            refresh_explorer();
        } else {
            open_file(path);
        }
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

    void add_entries(grove::Node& list, const std::string& folder, int depth, std::size_t& budget) {
        for (const WorkspaceEntry& entry : m_ws.list(folder)) {
            if (budget == 0) return;
            --budget;
            const bool open = entry.directory && m_expanded.count(entry.path);
            std::string classes = entry.directory ? "entry folder" : "entry file";
            if (!entry.directory && normalized(entry.path) == m_active) classes += " open";
            grove::Node* item = add(m_doc, list, "item", "", classes);
            if (entry.directory) add(m_doc, *item, "text", open ? "\xE2\x96\xBE" : "\xE2\x96\xB8", "twisty");
            add_icon(m_doc, *item, file_icon(entry.path, entry.directory, open));
            add(m_doc, *item, "text", entry.name, "entry-name");
            item->set_attribute("data-path", entry.directory ? entry.path : normalized(entry.path));
            const int indent = 8 + depth * 14 + (entry.directory ? 0 : 14);
            item->set_attribute("style", "padding: 2px 8px 2px " + std::to_string(indent) + "px");
            if (open) add_entries(list, entry.path, depth + 1, budget);
        }
    }

    void refresh_explorer() {
        grove::Node* explorer = m_doc.find_by_id("explorer");
        if (!explorer) return;
        clear(m_doc, *explorer);
        std::size_t budget = 3000;
        add_entries(*explorer, m_ws.root, 0, budget);
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
        clear(m_doc, *problems);
        if (warnings) clear(m_doc, *warnings);
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
            grove::Node* list = error || !warnings ? problems : warnings;
            (error ? errors : warns) += 1;
            grove::Node* p = add(m_doc, *list, "p", display_name(m.file) + ":" + std::to_string(m.line) + (m.column > 0 ? ":" + std::to_string(m.column) : std::string()) + "  " + m.message, error ? "problem error" : "problem");
            fs::path file(m.file);
            if (file.is_relative()) file = fs::path(m_ws.root) / file;
            p->set_attribute("data-path", file.string());
            p->set_attribute("data-line", std::to_string(m.line));
            p->set_attribute("data-column", std::to_string(std::max<std::size_t>(1, m.column)));
        }
        grove::Node* setup = warnings ? warnings : problems;
        for (const ToolProblem& p : m_ws.toolchain.problems) {
            add(m_doc, *setup, "p", p.message, "problem");
            ++warns;
        }
        for (const grove::Diagnostic& d : m_ws.diagnostics.items()) {
            const bool error = d.severity == grove::Severity::Error;
            grove::Node* p = add(m_doc, error ? *problems : *setup, "p", d.file + ":" + std::to_string(d.span.begin.line) + ": " + d.message, error ? "problem error" : "problem");
            p->set_attribute("data-path", d.file);
            p->set_attribute("data-line", std::to_string(d.span.begin.line));
            (error ? errors : warns) += 1;
        }
        const std::vector<ShortcutBinding> bindings = shortcut_bindings(m_ws.settings);
        for (const std::string& message : shortcut_errors(bindings)) {
            grove::Node* p = add(m_doc, *problems, "p", "Shortcuts: " + message, "problem error");
            p->set_attribute("data-settings-page", "shortcuts");
            ++errors;
        }
        for (const ShortcutConflict& c : shortcut_conflicts(bindings)) {
            grove::Node* p = add(m_doc, *problems, "p", "Shortcut conflict: " + c.message, "problem error");
            if (!c.file.empty()) {
                p->set_attribute("data-path", c.file);
                p->set_attribute("data-line", std::to_string(std::max<std::size_t>(1, c.line)));
            } else {
                p->set_attribute("data-settings-page", "shortcuts");
            }
            ++errors;
        }
        if (problems->children().empty()) add(m_doc, *problems, "p", "No errors. C, C++, Walnut and Grove are ready to build.", "ok");
        if (warnings && warnings->children().empty()) add(m_doc, *warnings, "p", "No warnings.", "ok");
        auto count = [](std::size_t n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); };
        set_text(m_doc, "status-problems", errors + warns == 0 ? std::string("No problems") : count(errors, "error") + ", " + count(warns, "warning"));
        set_text(m_doc, "tab-problems", errors == 0 ? std::string("PROBLEMS") : "PROBLEMS " + std::to_string(errors));
        set_text(m_doc, "tab-warnings", warns == 0 ? std::string("WARNINGS") : "WARNINGS " + std::to_string(warns));
    }

    void refresh_status() {
        const std::string name = fs::path(m_ws.root).filename().string();
        set_text(m_doc, "workspace-name", name.empty() ? m_ws.root : name);
        set_text(m_doc, "status-workspace", m_ws.root);
        set_text(m_doc, "status-toolchain", toolchain_summary(m_ws.toolchain));
        set_text(m_doc, "status-version", std::string("Canopy ") + CANOPY_VERSION_STRING);
        if (m_active.empty()) {
            set_text(m_doc, "status-language", "");
            set_text(m_doc, "status-cursor", "");
        } else {
            const Language* lang = language_for_path(m_active);
            const grove::CodeLanguage code = grove::code_language_for_path(m_active);
            std::string label = lang ? lang->name : code == grove::CodeLanguage::CMake ? std::string("CMake") : std::string("Plain text");
            if (m_active == k_settings_tab) label = "Settings";
            if (const OpenFile* f = find_file(m_active)) {
                if (f->kind == ViewerKind::Image) label = "Image";
                else if (f->kind == ViewerKind::Pdf) label = "PDF";
                else if (f->kind == ViewerKind::Binary) label = "Binary";
                else if (f->kind == ViewerKind::Latex) label = "LaTeX";
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
    }

    void reload_settings() {
        m_ws.diagnostics = grove::Diagnostics{};
        m_ws.settings = load_settings(m_ws.root, m_ws.diagnostics);
        m_ws.toolchain = detect_toolchain(m_ws.settings);
        m_themes = load_themes(m_ws.diagnostics);
        setup_intel();
        for (OpenFile& f : m_files) {
            if (f.editor && f.path != k_settings_tab) apply_editor_settings(*f.editor);
            if (f.editor && f.path != k_settings_tab) schedule_semantic(f.path, 0.0);
        }
        refresh_viewers();
        apply_gui();
        refresh_theme_picker();
        if (m_settings) m_settings->refresh();
        refresh_problems();
        refresh_status();
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
        editor.set_attribute("line-numbers", m_ws.settings.flag("editor", "line-numbers", true) ? "true" : "false");
        grove::set_inline_style(m_doc, editor, "font-size", editor_font_size(m_ws.settings, m_zoom));
        const std::string family = m_ws.settings.get_or("editor", "font-family", "monospace");
        grove::set_inline_style(m_doc, editor, "font-family", family.empty() ? "" : "\"" + family + "\"");
        apply_highlight(m_doc, editor, m_ws.settings, m_themes, editor.attribute_or("language", "plain"));
        if (is_walnut_library_copy(editor.attribute_or("data-path", ""))) editor.set_attribute("readonly", "");
        const std::string keys = editor_keymap(shortcut_bindings(m_ws.settings));
        if (keys.empty()) editor.remove_attribute("keys");
        else editor.set_attribute("keys", keys);
    }

    void apply_gui() {
        const double zoom = gui_zoom(m_ws.settings, m_doc.host().viewport_width, m_doc.host().viewport_height);
        if (grove::Node* body = m_doc.first_by_tag("body")) grove::set_inline_style(m_doc, *body, "zoom", size_number(zoom));
        if (zoom == m_zoom) return;
        m_zoom = zoom;
        for (OpenFile& f : m_files) {
            if (f.editor && f.path != k_settings_tab) apply_editor_settings(*f.editor);
        }
        refresh_viewers();
        if (m_settings) m_settings->refresh();
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
        for (const CompilerMessage& m : m_messages) {
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
        if (!f.editor || f.path == k_settings_tab) return;
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

    void notice(const std::string& message) {
        m_output += message + "\n";
        set_text(m_doc, "output-log", m_output);
    }

    static double now_seconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool walnut_file(const OpenFile& f) const {
        return f.editor && f.path != k_settings_tab && f.editor->attribute_or("language", "") == "walnut";
    }

    void schedule_semantic(const std::string& path, double delay) {
        const OpenFile* f = find_file(path);
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
        m_semantic.start(pick, walnut_highlight_command(walnut->path, pick, m_ws.settings.get_or("language.walnut", "flags", "")), text);
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

    void setup_intel() {
        m_intel.set_workspace(m_ws.root);
        m_intel.set_open_texts([this](const std::string& path) -> std::optional<std::string> {
            const OpenFile* f = find_file(path);
            if (!f || !f->editor || f->path == k_settings_tab) return std::nullopt;
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
        if (!file || !file->editor || file->path == k_settings_tab) return false;
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
        for (const CompilerMessage& m : m_messages) {
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
            const auto w = m_walnut.find(f.path);
            if (w != m_walnut.end()) {
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
        if (!f || !f->editor || f->path == k_settings_tab) return;
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
        if (remember && !m_active.empty() && m_active != k_settings_tab) {
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
        open_file(where.file, where.line + 1, display + 1);
    }

    void jump(std::vector<Place>& from, std::vector<Place>& to) {
        if (from.empty()) return;
        const Place target = from.back();
        from.pop_back();
        if (!m_active.empty() && m_active != k_settings_tab) {
            const auto caret = m_carets.find(m_active);
            to.push_back(Place{ m_active, caret == m_carets.end() ? 1 : caret->second.first, caret == m_carets.end() ? 1 : caret->second.second });
        }
        open_file(target.path, target.line, target.column);
    }

    void go_back() { jump(m_back, m_forward); }

    void go_forward() { jump(m_forward, m_back); }

    void poll_job() {
        poll_semantic();
        poll_viewers();
        const std::string text = m_job.take_output();
        if (!text.empty()) {
            m_output += text;
            if (m_output.size() > 400000) m_output.erase(0, m_output.size() - 400000);
            set_text(m_doc, "output-log", m_output);
        }
        const std::optional<int> result = m_job.take_result();
        if (!result) return;
        m_messages = parse_compiler_messages(m_output);
        update_all_marks();
        m_output += *result == 0 ? std::string("\n") + m_running_label + " finished.\n" : "\n" + m_running_label + " failed (exit " + std::to_string(*result) + ").\n";
        set_text(m_doc, "output-log", m_output);
        refresh_problems();
        refresh_explorer();
        bool errors = false;
        for (const CompilerMessage& m : m_messages) errors = errors || m.severity == "error";
        if (errors) show_panel("problems");
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
                set_text(m_doc, "output-log", m_output);
            }
            const bool again = v.again;
            v.again = false;
            start_render(path);
            v.again = again;
            return;
        }
        m_output += "LaTeX (" + v.tool + ") for " + display_name(v.source) + ":\n" + v.log + "\n";
        set_text(m_doc, "output-log", m_output);
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
        if (!collapsed && v.pages.empty() && v.phase == Viewer::Phase::Idle) start_latex(path);
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
    std::string m_active;
    std::set<std::string> m_expanded;
    std::vector<CompilerMessage> m_messages;
    BackgroundJob m_job;
    SemanticJob m_semantic;
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
    std::map<std::string, std::vector<CompilerMessage>> m_live;
    std::string m_output;
    std::string m_running_label;
    std::size_t m_caret_serial = 0;
    std::map<std::string, Viewer> m_viewers;
    std::map<std::string, std::vector<grove::CodeProblem>> m_marks;
    double m_zoom = 1.0;
    std::map<std::string, ViewerKind> m_open_as;
    const grove::Node* m_tab_anchor = nullptr;
    std::size_t m_file_slot = static_cast<std::size_t>(-1);
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
        if (count == 0) add(doc, *problems, "p", "Everything's set up: C, C++, Walnut and Grove are ready to build.", "ok");
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
    grove::dump(project.document.root(), os);
    return 0;
}

} // namespace canopy
