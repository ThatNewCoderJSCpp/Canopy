#include <canopy/shell.hpp>

#include <canopy/languages.hpp>
#include <canopy/version.hpp>

#include <grove/runtime/runtime.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

grove::Node* add(grove::Document& doc, grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes = {}) {
    grove::Node* el = doc.create_element(tag);
    if (!classes.empty()) el->set_attribute("class", classes);
    if (!text.empty()) el->set_inner_text(text);
    return doc.append_child(parent, *el);
}

void set_text(grove::Document& doc, const std::string& id, const std::string& text) {
    if (grove::Node* el = doc.find_by_id(id)) el->set_inner_text(text);
}

std::string tool_label(const Toolchain& toolchain, const std::string& key, const std::string& name) {
    const Tool* t = toolchain.get(key);
    if (!t) return name + ": missing";
    return name + ": " + fs::path(t->path).filename().string();
}

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
    return tool_label(toolchain, "cpp", "C++") + "  ·  " + tool_label(toolchain, "c", "C") + "  ·  " +
           tool_label(toolchain, "walnut", "Walnut") + "  ·  " + tool_label(toolchain, "grove", "Grove");
}

void populate_shell(grove::Project& project, const Workspace& workspace) {
    grove::Document& doc = project.document;
    const std::string name = fs::path(workspace.root).filename().string();

    set_text(doc, "workspace-name", name.empty() ? workspace.root : name);
    set_text(doc, "status-toolchain", toolchain_summary(workspace.toolchain));
    set_text(doc, "status-version", std::string("Canopy ") + CANOPY_VERSION_STRING);

    if (grove::Node* explorer = doc.find_by_id("explorer")) {
        explorer->clear_children();
        for (const WorkspaceEntry& e : workspace.list(workspace.root)) {
            std::string classes = e.directory ? "entry folder" : "entry file";
            if (!e.directory) {
                if (const Language* lang = language_for_path(e.path)) classes += " lang-" + lang->key;
            }
            add(doc, *explorer, "item", e.directory ? e.name + "/" : e.name, classes);
        }
    }

    if (grove::Node* problems = doc.find_by_id("problems")) {
        problems->clear_children();
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
    populate_shell(project, workspace);
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
    populate_shell(project, workspace);
    grove::dump(project.document.root(), os);
    return 0;
}

} // namespace canopy
