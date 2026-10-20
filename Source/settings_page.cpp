#include <canopy/settings_page.hpp>
#include <canopy/shortcuts.hpp>
#include <canopy/viewers.hpp>
#include <canopy/walnut_semantic.hpp>

#include <grove/dom_tools.hpp>
#include <grove/input/code_buffer.hpp>
#include <grove/input/input.hpp>
#include <grove/native.hpp>
#include <grove/runtime/runtime.hpp>
#include <grove/windowing.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

struct KindInfo {
    const char* kind;
    const char* name;
    const char* what;
};

const std::vector<KindInfo>& kind_infos() {
    static const std::vector<KindInfo> infos = {
        { "keyword", "Keywords", "if, return, class, function" },
        { "type", "Types", "int, std::string, Player" },
        { "function", "Functions", "names called with ( )" },
        { "namespace", "Namespaces", "std, app before ::" },
        { "variable", "Variables", "${NAME} and CMAKE_* in CMake" },
        { "constant", "Constants", "true, nullptr, macros, LIMIT" },
        { "string", "Strings", "\"text\" and 'c'" },
        { "escape", "Escapes", "\\n and \\t inside strings" },
        { "number", "Numbers", "42, 3.14, 0xff" },
        { "comment", "Comments", "// and /* */" },
        { "preprocessor", "Preprocessor", "#include, #define" },
        { "operator", "Operators", "+ - < > = ::" },
        { "tag", "Tags", "<p> and Bark selectors" },
        { "attribute", "Attributes", "[[nodiscard]], class=, PRIVATE" },
        { "property", "Properties", "Bark's color:, padding:" },
        { "plain", "Everything else", "names and punctuation" },
        { "control-keyword", "Control keywords", "if, for, while, return" },
        { "builtin-type", "Built-in types", "int, bool, string" },
        { "declaration", "Declarations", "names where they're declared" },
        { "self", "self", "self inside a class" },
        { "this", "this", "this inside a class" },
        { "cast", "Casts", "static_cast and friends" },
        { "builtin-operator", "Built-in operators", "+= on numbers" },
        { "overloaded-operator", "Overloaded operators", "operators a type defines" },
        { "enum-member", "Enum members", "Color::Red" },
        { "template-scope", "Template scopes", "names inside template< >" },
        { "variable-template", "Variable templates", "templated variables" },
        { "unresolved", "Unknown names", "names the compiler can't find" },
        { "error-underline", "Error squiggle", "the wavy line under errors" },
        { "warning-underline", "Warning squiggle", "the wavy line under warnings" },
        { "error-line", "Error lines", "the text of a line with an error" },
        { "background", "Background", "behind the code" },
        { "foreground", "Text", "everything without its own color" },
    };
    return infos;
}

const KindInfo* kind_info(const std::string& kind) {
    for (const KindInfo& k : kind_infos()) {
        if (kind == k.kind) return &k;
    }
    return nullptr;
}



std::string kind_what(const std::string& language, const std::string& kind) {
    static const std::vector<std::pair<std::string, std::string>> special = {
        { "cmake:function", "commands: set, add_executable" },
        { "cmake:keyword", "if, endif, foreach, NOT, AND" },
        { "cmake:variable", "${NAME}, $<...>, CMAKE_*" },
        { "cmake:attribute", "PRIVATE, VERSION, STATUS" },
        { "cmake:constant", "ON, OFF, TRUE" },
        { "cmake:comment", "# and #[[ ]]" },
        { "grove:tag", "element names" },
        { "grove:attribute", "attribute names" },
        { "grove:constant", "&amp; and other entities" },
        { "grove:operator", "< / > =" },
        { "grove:comment", "<!-- -->" },
        { "bark:tag", "element selectors" },
        { "bark:attribute", ".class and #id selectors" },
        { "bark:property", "color:, padding:" },
        { "bark:keyword", "values like bold and :hover" },
        { "bark:constant", "$variables and #colors" },
        { "bark:function", "calc(), rgb()" },
        { "c:attribute", "[[attributes]]" },
        { "c:constant", "true, NULL, macros" },
        { "walnut:keyword", "function, if, then, end" },
        { "walnut:type", "int, string, let" },
    };
    const std::string key = language + ":" + kind;
    for (const auto& s : special) {
        if (s.first == key) return s.second;
    }
    const KindInfo* info = kind_info(kind);
    return info ? info->what : "";
}

std::vector<std::string> kinds_for(const std::string& language) {
    if (language == "all") {
        std::vector<std::string> kinds = { "background", "foreground" };
        for (const std::string& kind : highlight_kinds()) {
            if (!is_theme_color_kind(kind) && kind != "unresolved") kinds.push_back(kind);
        }
        return kinds;
    }
    if (language == "c") return { "keyword", "type", "function", "constant", "string", "escape", "number", "comment", "preprocessor", "operator", "attribute", "plain", "error-underline", "warning-underline", "error-line" };
    if (language == "cpp") return { "keyword", "type", "function", "namespace", "constant", "string", "escape", "number", "comment", "preprocessor", "operator", "attribute", "plain", "error-underline", "warning-underline", "error-line" };
    if (language == "walnut") return { "keyword", "control-keyword", "type", "builtin-type", "function", "namespace", "declaration", "variable", "variable-template", "template-scope",
                                       "enum-member", "constant", "string", "escape", "number", "comment", "preprocessor", "operator", "builtin-operator", "overloaded-operator",
                                       "self", "this", "cast", "plain", "error-underline", "warning-underline", "error-line" };
    if (language == "grove") return { "tag", "attribute", "string", "constant", "comment", "operator", "plain" };
    if (language == "bark") return { "tag", "attribute", "property", "keyword", "function", "constant", "number", "string", "comment", "operator", "plain" };
    if (language == "cmake") return { "function", "keyword", "variable", "attribute", "constant", "string", "escape", "number", "comment", "plain" };
    return highlight_kinds();
}

const std::vector<std::pair<std::string, std::string>>& language_names() {
    static const std::vector<std::pair<std::string, std::string>> names = {
        { "all", "All languages" }, { "c", "C" }, { "cpp", "C++" }, { "walnut", "Walnut" }, { "grove", "Grove" }, { "bark", "Bark" }, { "cmake", "CMake" },
    };
    return names;
}

std::string hex_color(const grove::bark::Rgba& c) {
    char buffer[16];
    if (c.a == 255) std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x", c.r, c.g, c.b);
    else std::snprintf(buffer, sizeof buffer, "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return buffer;
}

const grove::Node* with_attribute(const grove::Node* node, const char* name) {
    for (const grove::Node* n = node; n; n = n->parent()) {
        if (n->is_element() && n->has_attribute(name)) return n;
    }
    return nullptr;
}

std::string origin_text(ToolOrigin origin) {
    switch (origin) {
    case ToolOrigin::Settings: return "from your settings";
    case ToolOrigin::Bundled: return "shipped with Canopy";
    case ToolOrigin::Path: return "on your PATH";
    case ToolOrigin::KnownFolder: return "found in a usual install folder";
    }
    return "";
}

void clear_children(grove::Document& doc, grove::Node& node) {
    while (!node.children().empty()) {
        grove::Node* last = node.children().back().get();
        if (!doc.remove(*last)) {
            node.clear_children();
            break;
        }
    }
    doc.touch();
}

} // namespace

const std::vector<SettingDoc>& setting_docs() {
    static const std::vector<SettingDoc> docs = {
        { "editor", "font-family", "Font", "The font the code is drawn in. A font that isn't installed falls back to another monospace font." },
        { "editor", "font-size", "Font size", "Size of the code. px and pt are fixed sizes; vmin, vmax, vdiag, vw and vh are a fraction (0 to 1) of the window, and % is a percentage of the window's smaller side." },
        { "editor", "tab-size", "Tab size", "How many columns a tab takes up: in the editor, and in the Output and other panels, Markdown previews and hovers too." },
        { "editor", "insert-spaces", "Insert spaces", "Tab and Enter insert spaces instead of tab characters." },
        { "editor", "backspace-to-tab-stop", "Backspace to tab stop", "At a tab stop with a whole level of spaces before it, Backspace deletes the level at once, like deleting a tab. Between tab stops it deletes one space at a time. Turn it off to always delete one space." },
        { "editor", "close-blocks", "Close blocks on Enter", "Pressing Enter after an opening { adds the closing line for you, based on the language: } end function name in Walnut, }; after a C++ class, and </tag> in Grove markup. Turn it off to only indent." },
        { "editor", "line-numbers", "Line numbers", "Show line numbers beside the code." },
        { "editor", "hover-width", "Hover width", "Widest the popup gets when you rest the mouse on code, in pixels. Function signatures that don't fit put a few parameters on each line." },
        { "editor", "hover-height", "Hover height", "Tallest the hover popup gets, in pixels. Longer ones scroll with the mouse wheel." },
        { "editor", "autocomplete", "Suggestions", "Show a list of names that fit while you type. Tab puts the highlighted one in, Escape closes the list until you start another word, and Ctrl+Space opens it any time." },
        { "editor", "accept-suggestion-on-enter", "Accept suggestions with Enter", "Enter puts the highlighted suggestion in too, instead of starting a new line." },
        { "editor", "word-suggestions", "Suggest words from the file", "Besides keywords and names the editor understands, suggest other words that appear in the file." },
        { "editor", "suggestion-icons", "Suggestion icons", "Show the small colored letter beside each suggestion: F for functions, T for types, V for variables and so on." },
        { "editor", "suggestion-kinds", "Suggestion kinds", "Write what each suggestion is (function, struct, enum, keyword, ...) beside its name." },
        { "gui", "size", "Size", "13px is the normal size; everything grows or shrinks with it. The code's size is under Editor. px and pt are fixed; vmin, vmax, vdiag, vw and vh are a fraction (0 to 1) of the window, and % is a percentage of the window's smaller side, so the GUI follows the window's size." },
        { "language.c", "standard", "Standard", "Which version of C to compile as." },
        { "language.cpp", "standard", "Standard", "Which version of C++ to compile as." },
        { "viewer", "pdf-resolution", "Resolution", "Dots per inch the pages are drawn at. Higher is sharper and slower." },
        { "viewer", "latex-flags", "Flags", "Added to the LaTeX command, like -xelatex for latexmk." },
        { "viewer", "latex-preview", "Live preview", "Show the preview beside .tex files and rebuild it when you save." },
        { "viewer", "markdown-preview", "Live preview", "Show the rendered page beside .md files and update it as you type." },
        { "build", "mode", "Mode", "Debug builds are quick to make and easy to debug; release builds run fast." },
        { "build", "output-dir", "Output folder", "Where programs are built, inside the open folder." },
        { "build", "save-before-build", "Save before building", "Save the file you're building first." },
        { "explorer", "preview-tabs", "Preview tabs", "Clicking a file once opens it in a preview tab (its name is in italics) that the next file you click replaces. Double-click a file or its tab, or edit it, to keep it open." },
        { "explorer", "confirm-delete", "Confirm delete", "Ask before moving files and folders to the trash." },
        { "explorer", "confirm-drag-and-drop", "Confirm moving and copying", "Ask before a drag in the explorer moves or copies files (hold Ctrl while dropping to copy)." },
        { "explorer", "confirm-replace", "Confirm replacing", "Ask before a move, copy, paste or rename replaces a file or folder that's already there (Replace, Keep both or Skip). Turned off, what's there goes to the trash without asking." },
        { "terminal", "shell", "Shell", "The program the terminal runs. auto picks bash on Linux and macOS (or your $SHELL) and cmd.exe on Windows; you can name another, like zsh, powershell.exe or a full path. New terminals use it." },
        { "terminal", "prompt", "Prompt", "canopy shows bash's prompt as you@host:folder$ in color; shell keeps the prompt from your own .bashrc. New terminals use it." },
        { "terminal", "font-size", "Font size", "Size of the terminal's text. The font is the editor's." },
        { "terminal", "scrollback", "Scrollback", "How many lines each terminal keeps above the screen." },
        { "terminal", "tab-size", "Tab size", "How many columns a tab character moves in the terminal, like in programs' output. Terminals usually use 8; the code editor's own tab size is under Editor." },
        { "server", "port", "Port", "The port a new local server tries first. If it's busy, the next free one is used." },
        { "server", "host", "Address", "127.0.0.1 keeps the server on this computer; 0.0.0.0 lets other devices on your network open it too." },
        { "server", "live-reload", "Live reload", "Reload pages the server sent when you save a file in the folder it serves." },
        { "git", "refresh-seconds", "Git refresh", "How often, in seconds, the Git tab checks for changes made outside Canopy while it's showing (three times slower while it's hidden)." },
        { "git", "protected-branches", "Protected branches", "Branches that need their name typed before Canopy deletes them or undoes a pushed commit on them, separated by spaces. Real protection belongs on the server (GitHub's branch rules); this guards against slips." },
        { "history", "enabled", "Local history", "Keep a copy of a file each time you save it, so the History tab can compare and restore older versions." },
        { "history", "limit", "Versions kept", "How many saved copies of each file to keep; the oldest go first." },
        { "language.*", "flags", "Flags", "Passed on every build." },
        { "language.*", "debug-flags", "Debug flags", "Added when building in debug mode." },
        { "language.*", "release-flags", "Release flags", "Added when building in release mode." },
        { "toolchain", "c", "C compiler", "The C compiler builds use. auto finds one for you; or give a path." },
        { "toolchain", "cpp", "C++ compiler", "The C++ compiler builds use. auto finds one for you; or give a path." },
        { "toolchain", "walnut", "Walnut", "The Walnut compiler. auto finds it for you; or give a path." },
        { "toolchain", "grove", "Grove", "The Grove tool that runs .grove apps. auto finds it for you; or give a path." },
        { "viewer", "pdf", "PDF renderer", "The program that draws PDF pages: pdftoppm, mutool, pdftocairo or Ghostscript. auto picks the first one installed." },
        { "viewer", "latex", "LaTeX", "The program that builds .tex files: latexmk, tectonic, pdflatex, xelatex or lualatex. auto picks the first one installed." },
        { "layout", "sidebar", "Sidebar side", "Where the sidebar sits: left, right, top or bottom." },
        { "layout", "panel", "Panel side", "Where the panel (terminal, problems, output) sits: left, right, top or bottom." },
        { "layout", "aside", "Aside side", "Where the aside area sits: left, right, top or bottom. It only shows when a view is moved there." },
        { "layout", "panel-span", "Panel span", "editor keeps the panel under the editor only; full stretches it across the whole window." },
        { "layout", "sidebar-size", "Sidebar size", "How wide (or tall) the sidebar is. Dragging its edge saves a new size here." },
        { "layout", "panel-size", "Panel size", "How tall (or wide) the panel is. Dragging its edge saves a new size here." },
        { "layout", "aside-size", "Aside size", "How wide (or tall) the aside area is. Dragging its edge saves a new size here." },
        { "layout", "*", "View area", "Which area this view lives in: sidebar, panel or aside. Right-click a tab, or drag it, to move it." },
        { "theme", "name", "Theme name", "The name shown in the theme picker." },
        { "theme", "background", "Background", "The color behind the code." },
        { "theme", "foreground", "Text", "The color of text that has no color of its own." },
        { "editor", "theme", "Color theme", "Which color theme the editor uses, by its id (like canopy-dark). The picker in the status bar and Settings > Colors change it." },
        { "editor", "word-wrap", "Word wrap", "Saved for wrapping long lines to the editor's width; Canopy doesn't wrap lines yet, so it has no effect." }
    };
    return docs;
}

std::optional<SettingDoc> find_setting_doc(const std::string& section, const std::string& key) {
    for (const SettingDoc& d : setting_docs()) {
        if (d.section == section && d.key == key) return d;
    }
    if (section.rfind("language.", 0) == 0) {
        for (const SettingDoc& d : setting_docs()) {
            if (d.section == "language.*" && d.key == key) return d;
        }
    }
    if (section == "keys") {
        for (const Shortcut& s : shortcuts()) {
            if (s.id == key) return SettingDoc{ section, key, s.label, s.help };
        }
    }
    if (section == "highlight" || section.rfind("highlight.", 0) == 0) {
        if (const KindInfo* k = kind_info(key)) {
            const std::string where = section == "highlight" ? std::string("every language") : section.substr(10);
            return SettingDoc{ section, key, k->name, std::string("Color for ") + k->what + " in " + where + ": a color, then any of bold, italic, underline and strikethrough." };
        }
    }
    for (const SettingDoc& d : setting_docs()) {
        if (d.section == section && d.key == "*") return SettingDoc{ section, key, d.label, d.help };
    }
    return std::nullopt;
}

HighlightLook parse_highlight_look(const std::string& value) {
    HighlightLook look;
    std::istringstream in(value);
    std::string word;
    while (in >> word) {
        if (word == "bold") look.bold = true;
        else if (word == "italic") look.italic = true;
        else if (word == "underline") look.underline = true;
        else if (word == "strikethrough" || word == "line-through" || word == "strike") look.strikethrough = true;
        else if (!word.empty() && word[0] == '#' && look.color.empty()) look.color = word;
    }
    return look;
}

std::string highlight_look_text(const HighlightLook& look) {
    std::string out = look.color;
    for (const auto& [on, word] : { std::pair<bool, const char*>{ look.bold, "bold" }, { look.italic, "italic" }, { look.underline, "underline" }, { look.strikethrough, "strikethrough" } }) {
        if (!on) continue;
        if (!out.empty()) out += ' ';
        out += word;
    }
    return out;
}

std::string theme_id(const Settings& settings) {
    const std::string id = settings.get_or("editor", "theme", "canopy-dark");
    return id.empty() ? std::string("canopy-dark") : id;
}

void apply_highlight(grove::Document& doc, grove::Node& editor, const Settings& settings, const std::vector<Theme>& themes, const std::string& language) {
    const Theme* theme = find_theme(themes, theme_id(settings));
    if (!theme && !themes.empty()) theme = &themes.front();
    apply_highlight_colors(doc, editor, resolve_highlight(settings, theme, language), theme);
}

void apply_highlight_colors(grove::Document& doc, grove::Node& editor, const std::map<std::string, std::string>& colors, const Theme* theme) {
    for (const std::string& kind : highlight_kinds()) {
        if (is_theme_color_kind(kind)) continue;
        const auto it = colors.find(kind);
        grove::set_inline_style(doc, editor, "code-" + kind, it == colors.end() ? std::string() : it->second);
    }
    const auto bg = colors.find("background");
    const auto fg = colors.find("foreground");
    const std::string background = bg != colors.end() && !bg->second.empty() ? parse_highlight_look(bg->second).color : theme ? theme->background : std::string();
    const std::string foreground = fg != colors.end() && !fg->second.empty() ? parse_highlight_look(fg->second).color : theme ? theme->foreground : std::string();
    grove::set_inline_style(doc, editor, "background", background);
    grove::set_inline_style(doc, editor, "color", foreground);
    doc.touch();
}

std::string theme_slug(const std::string& name) {
    std::string out;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u)) out += static_cast<char>(std::tolower(u));
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? std::string("my-theme") : out;
}

std::string theme_bark_text(const std::string& name, const std::string& background, const std::string& foreground, const std::map<std::string, std::map<std::string, std::string>>& sections, const std::string& made_from) {
    std::string out;
    if (!made_from.empty()) out += "// Made in Canopy's Settings, starting from " + made_from + ".\n\n";
    out += "theme {\n    name: " + bark_setting_value(name, true) + ";\n";
    if (!background.empty()) out += "    background: " + background + ";\n";
    if (!foreground.empty()) out += "    foreground: " + foreground + ";\n";
    out += "}\n";
    std::vector<std::string> order = { "highlight" };
    for (const std::string& language : highlight_languages()) order.push_back("highlight." + language);
    for (const std::string& section : order) {
        const auto it = sections.find(section);
        if (it == sections.end() || it->second.empty()) continue;
        out += "\n" + section + " {\n";
        for (const std::string& kind : highlight_kinds()) {
            if (is_theme_color_kind(kind)) continue;
            const auto v = it->second.find(kind);
            if (v != it->second.end() && !v->second.empty()) out += "    " + kind + ": " + v->second + ";\n";
        }
        out += "}\n";
    }
    return out;
}

std::string theme_json_text(const std::string& name, const std::string& background, const std::string& foreground, const std::map<std::string, std::map<std::string, std::string>>& sections) {
    auto quote = [](const std::string& text) {
        std::string out = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out + "\"";
    };
    std::vector<std::string> fields;
    fields.push_back("    \"name\": " + quote(name));
    if (!background.empty()) fields.push_back("    \"background\": " + quote(background));
    if (!foreground.empty()) fields.push_back("    \"foreground\": " + quote(foreground));
    std::vector<std::string> order = { "highlight" };
    for (const std::string& language : highlight_languages()) order.push_back("highlight." + language);
    for (const std::string& section : order) {
        const auto it = sections.find(section);
        if (it == sections.end() || it->second.empty()) continue;
        std::vector<std::string> entries;
        for (const std::string& kind : highlight_kinds()) {
            if (is_theme_color_kind(kind)) continue;
            const auto v = it->second.find(kind);
            if (v == it->second.end() || v->second.empty()) continue;
            const HighlightLook look = parse_highlight_look(v->second);
            std::string value;
            if (!look.bold && !look.italic && !look.underline && !look.strikethrough) {
                value = quote(look.color);
            } else {
                value = "{ \"color\": " + quote(look.color);
                if (look.bold) value += ", \"bold\": true";
                if (look.italic) value += ", \"italic\": true";
                if (look.underline) value += ", \"underline\": true";
                if (look.strikethrough) value += ", \"strikethrough\": true";
                value += " }";
            }
            entries.push_back("        " + quote(kind) + ": " + value);
        }
        std::string block = "    " + quote(section) + ": {\n";
        for (std::size_t i = 0; i < entries.size(); ++i) block += entries[i] + (i + 1 < entries.size() ? ",\n" : "\n");
        fields.push_back(block + "    }");
    }
    std::string out = "{\n";
    for (std::size_t i = 0; i < fields.size(); ++i) out += fields[i] + (i + 1 < fields.size() ? ",\n" : "\n");
    return out + "}\n";
}

namespace {

const char* const k_mark_open = "\xE2\x9F\xA6";
const char* const k_mark_close = "\xE2\x9F\xA7";

std::string walnut_marked_sample() {
    return "import module.io\n"
           "\n"
           "enum \xE2\x9F\xA6" "type:Color\xE2\x9F\xA7" " { \xE2\x9F\xA6" "enum-member:Red\xE2\x9F\xA7" ", \xE2\x9F\xA6" "enum-member:Green\xE2\x9F\xA7" " }\n"
           "\n"
           "namespace \xE2\x9F\xA6" "namespace:game\xE2\x9F\xA7" " {\n"
           "    // Adds up the scores that count\n"
           "    function \xE2\x9F\xA6" "function:total\xE2\x9F\xA7" "(const string&: \xE2\x9F\xA6" "declaration:name\xE2\x9F\xA7" ", int: \xE2\x9F\xA6" "declaration:limit\xE2\x9F\xA7" ") -> int {\n"
           "        int: \xE2\x9F\xA6" "declaration:sum\xE2\x9F\xA7" " = 0;\n"
           "        for (int: \xE2\x9F\xA6" "declaration:score\xE2\x9F\xA7" " in {40, 75, 120}) {\n"
           "            if (\xE2\x9F\xA6" "variable:score\xE2\x9F\xA7" " <= \xE2\x9F\xA6" "variable:limit\xE2\x9F\xA7" ") then {\n"
           "                \xE2\x9F\xA6" "variable:sum\xE2\x9F\xA7" " \xE2\x9F\xA6" "builtin-operator:+=\xE2\x9F\xA7" " \xE2\x9F\xA6" "variable:score\xE2\x9F\xA7" ";\n"
           "            }\n"
           "        }\n"
           "        std::io::println(\xE2\x9F\xA6" "variable:name\xE2\x9F\xA7" " + \": done\\n\");\n"
           "        return \xE2\x9F\xA6" "variable:sum\xE2\x9F\xA7" ";\n"
           "    } end function total\n"
           "}\n"
           "\n"
           "function \xE2\x9F\xA6" "function:main\xE2\x9F\xA7" "() -> int {\n"
           "    \xE2\x9F\xA6" "type:Color\xE2\x9F\xA7" ": \xE2\x9F\xA6" "declaration:pick\xE2\x9F\xA7" " = \xE2\x9F\xA6" "type:Color\xE2\x9F\xA7" "::\xE2\x9F\xA6" "enum-member:Red\xE2\x9F\xA7" ";\n"
           "    bool: \xE2\x9F\xA6" "declaration:ok\xE2\x9F\xA7" " = true;\n"
           "    int: \xE2\x9F\xA6" "declaration:best\xE2\x9F\xA7" " = \xE2\x9F\xA6" "error:totl\xE2\x9F\xA7" ";\n"
           "    return \xE2\x9F\xA6" "namespace:game\xE2\x9F\xA7" "::\xE2\x9F\xA6" "function:total\xE2\x9F\xA7" "(\"Ada\", 100) > 0 ? 0 : 1;\n"
           "} end function main\n";
}

std::string marked_sample(const std::string& language) {
    if (language == "walnut") return walnut_marked_sample();
    return "";
}

struct SampleMarks {
    std::string text;
    std::vector<grove::CodeSpan> spans;
    std::vector<grove::CodeProblem> problems;
};

SampleMarks read_marks(const std::string& marked) {
    SampleMarks out;
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t i = 0;
    const std::string open = k_mark_open;
    const std::string close = k_mark_close;
    while (i < marked.size()) {
        if (marked.compare(i, open.size(), open) == 0) {
            const std::size_t colon = marked.find(':', i + open.size());
            const std::size_t end = colon == std::string::npos ? std::string::npos : marked.find(close, colon + 1);
            if (end != std::string::npos) {
                const std::string names = marked.substr(i + open.size(), colon - i - open.size());
                const std::string word = marked.substr(colon + 1, end - colon - 1);
                std::istringstream in(names);
                std::string name;
                while (in >> name) {
                    if (name == "error" || name == "warning") {
                        out.problems.push_back({ line, column, word.size(), name == "error" ? grove::CodeProblemKind::Error : grove::CodeProblemKind::Warning });
                    } else if (auto kind = grove::code_token_kind_from_name(name)) {
                        out.spans.push_back({ line, column, word.size(), *kind });
                    }
                }
                out.text += word;
                column += word.size();
                i = end + close.size();
                continue;
            }
        }
        const char c = marked[i++];
        out.text += c;
        if (c == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    return out;
}

std::string sample_text(const std::string& marked) {
    return read_marks(marked).text;
}

} // namespace

std::string highlight_sample_spans(const std::string& language) {
    const std::string marked = marked_sample(language);
    if (marked.empty()) return "";
    const SampleMarks marks = read_marks(marked);
    return grove::code_spans_text(text_lines(marks.text), marks.spans);
}

std::string highlight_sample_problems(const std::string& language) {
    const std::string marked = marked_sample(language);
    if (marked.empty()) return "";
    const SampleMarks marks = read_marks(marked);
    return grove::code_problems_text(text_lines(marks.text), marks.problems);
}

std::string highlight_sample(const std::string& language) {
    if (language == "c") {
        return "#include <stdio.h>\n"
               "#define LIMIT 100\n"
               "\n"
               "/* Adds up the scores that count */\n"
               "static int total(const int* scores, size_t count) {\n"
               "    int sum = 0;\n"
               "    for (size_t i = 0; i < count; ++i) {\n"
               "        if (scores[i] <= LIMIT) sum += scores[i];\n"
               "    }\n"
               "    return sum;\n"
               "}\n"
               "\n"
               "[[nodiscard]] int main(void) {\n"
               "    int scores[] = { 40, 75, 0x7f };\n"
               "    printf(\"total: %d\\n\", total(scores, 3));\n"
               "    return true ? 0 : 1;\n"
               "}\n";
    }
    if (language == "walnut") return sample_text(walnut_marked_sample());
    if (language == "grove") {
        return "<!grove>\n"
               "<grove>\n"
               "    <head>\n"
               "        <title>Scores</title>\n"
               "        <style src=\"scores.bark\" />\n"
               "    </head>\n"
               "    <body title=\"Scores\">\n"
               "        <!-- the list fills in from the script -->\n"
               "        <h2 class=\"title\">High scores</h2>\n"
               "        <list id=\"scores\"></list>\n"
               "        <button id=\"add\" class=\"primary\">Add &amp; save</button>\n"
               "    </body>\n"
               "</grove>\n";
    }
    if (language == "bark") {
        return "$accent = #89b4fa;\n"
               "\n"
               "/* The page */\n"
               "body {\n"
               "    background: #1e1e2e;\n"
               "    font-size: 14px;\n"
               "}\n"
               "\n"
               "#scores .row:hover {\n"
               "    color: $accent;\n"
               "    font-family: \"JetBrains Mono\";\n"
               "    font-weight: bold;\n"
               "    padding: 4px 8px;\n"
               "}\n"
               "\n"
               "button.primary {\n"
               "    width: calc(100% - 16px);\n"
               "}\n";
    }
    if (language == "cmake") {
        return "cmake_minimum_required(VERSION 3.18)\n"
               "project(scores VERSION 1.0 LANGUAGES CXX)\n"
               "\n"
               "# Build with warnings on\n"
               "set(CMAKE_CXX_STANDARD 17)\n"
               "option(SCORES_TESTS \"Build the tests\" ON)\n"
               "\n"
               "add_executable(scores src/main.cpp)\n"
               "target_compile_options(scores PRIVATE -Wall)\n"
               "\n"
               "if(SCORES_TESTS AND NOT WIN32)\n"
               "    message(STATUS \"Tests for ${PROJECT_NAME}\\n\")\n"
               "    target_compile_definitions(scores PRIVATE $<$<CONFIG:Debug>:DEBUG>)\n"
               "endif()\n";
    }
    return "#include <vector>\n"
           "#define LIMIT 100\n"
           "\n"
           "namespace app {\n"
           "\n"
           "// Keeps the scores for one player\n"
           "struct [[nodiscard]] Player {\n"
           "    std::string name;\n"
           "    std::vector<int> scores;\n"
           "};\n"
           "\n"
           "int best(const Player& p) {\n"
           "    int top = 0;\n"
           "    for (int s : p.scores) {\n"
           "        if (s > top && s <= LIMIT) top = s;\n"
           "    }\n"
           "    std::printf(\"%s: %d\\n\", p.name.c_str(), top);\n"
           "    return top != 0 ? top : 0x7f;\n"
           "}\n"
           "\n"
           "} // namespace app\n";
}

SettingsPage::SettingsPage(grove::Document& doc, Workspace& workspace, std::vector<Theme>& themes, Hooks hooks)
    : m_doc(doc), m_ws(workspace), m_themes(themes), m_hooks(std::move(hooks)) {}

grove::Node* SettingsPage::add(grove::Node& parent, const std::string& tag, const std::string& text, const std::string& classes) {
    grove::Node* el = m_doc.create_element(tag);
    if (!classes.empty()) el->set_attribute("class", classes);
    if (!text.empty()) el->set_inner_text(text);
    return m_doc.append_child(parent, *el);
}

grove::Node* SettingsPage::create() {
    m_root = m_doc.create_element("row");
    m_root->set_attribute("class", "settings-page hidden");
    m_doc.events().add_listener(m_root, "click", [this](grove::Event& e) { on_click(e); });
    m_doc.events().add_listener(m_root, "change", [this](grove::Event& e) { on_change(e); });
    m_doc.events().add_listener(m_root, "input", [this](grove::Event& e) { on_color_input(e); });
    m_doc.events().add_listener(m_root, "cursor", [this](grove::Event& e) { on_preview_cursor(e); });
    return m_root;
}

void SettingsPage::show(const std::string& page) {
    m_recording.clear();
    if (!page.empty()) m_page = page;
    m_marked.clear();
    m_picked.clear();
    rebuild();
}

void SettingsPage::refresh() {
    if (m_draft_ready) {
        const bool dirty = draft_dirty();
        const Sections saved = saved_draft();
        const std::string saved_theme = theme_id(m_ws.settings);
        if (!dirty) {
            m_draft = saved;
            m_draft_theme = saved_theme;
        }
        m_saved = saved;
        m_saved_theme = saved_theme;
    }
    if (!m_root || m_pending) return;
    m_pending = true;
    m_doc.events().set_timeout(0.0, [this] {
        m_pending = false;
        rebuild();
    });
}

void SettingsPage::rebuild() {
    if (!m_root) return;
    clear_children(m_doc, *m_root);
    m_preview = nullptr;
    grove::Node* nav = add(*m_root, "column", "", "settings-nav");
    build_nav(*nav);
    m_body = add(*m_root, "column", "", "settings-body");
    if (m_page == "highlighting") build_highlighting(*m_body);
    else if (m_page == "compilers") build_compilers(*m_body);
    else if (m_page == "build") build_build(*m_body);
    else if (m_page == "workbench") build_workbench(*m_body);
    else if (m_page == "viewers") build_viewers(*m_body);
    else if (m_page == "gui") build_gui(*m_body);
    else if (m_page == "shortcuts") build_shortcuts(*m_body);
    else build_editor(*m_body);
    m_doc.touch();
}

void SettingsPage::build_nav(grove::Node& nav) {
    add(nav, "div", "SETTINGS", "settings-nav-title");
    const std::vector<std::pair<std::string, std::string>> pages = {
        { "editor", "Editor" }, { "gui", "GUI" }, { "highlighting", "Highlighting" }, { "compilers", "Compilers" }, { "build", "Build and Run" }, { "workbench", "Explorer, Terminal and Git" }, { "viewers", "Viewers" }, { "shortcuts", "Shortcuts" },
    };
    for (const auto& [id, label] : pages) {
        grove::Node* item = add(nav, "p", label, id == m_page ? "settings-nav-item active" : "settings-nav-item");
        item->set_attribute("data-page", id);
    }
    grove::Node* spacer = add(nav, "div", "", "settings-nav-spacer");
    (void)spacer;
    grove::Node* files = add(nav, "p", "Open the settings file", "settings-nav-file");
    files->set_attribute("data-open-file", m_page == "highlighting" ? "highlighting.bark" : m_page == "compilers" ? "compilers.bark" : m_page == "build" ? "build.bark" : m_page == "shortcuts" ? "keys.bark" : m_page == "workbench" ? "workbench.bark" : "editor.bark");
    add(nav, "p", user_config_dir(), "settings-nav-path");
}

void SettingsPage::build_shortcuts(grove::Node& body) {
    heading(body, "Shortcuts", "What every key and mouse shortcut does. Type new keys into a box (several with commas, or none), or press Record and then the keys. Saved to your keys.bark.");
    const std::vector<ShortcutBinding> bindings = shortcut_bindings(m_ws.settings);
    const std::vector<ShortcutConflict> conflicts = shortcut_conflicts(bindings);
    const std::vector<std::string> errors = shortcut_errors(bindings);
    if (!conflicts.empty() || !errors.empty()) {
        grove::Node* banner = add(body, "column", "", "shortcut-banner");
        const std::size_t n = conflicts.size() + errors.size();
        add(*banner, "div", std::to_string(n) + (n == 1 ? " problem" : " problems") + " with your shortcuts." + (conflicts.empty() ? std::string() : std::string(" Keys that clash do the first one listed until it's fixed.")), "shortcut-banner-title");
        for (const std::string& message : errors) add(*banner, "p", message, "shortcut-banner-item");
        for (const ShortcutConflict& c : conflicts) {
            grove::Node* item = add(*banner, "p", c.message, "shortcut-banner-item");
            if (!c.file.empty()) {
                item->set_attribute("data-open-path", c.file);
                item->set_attribute("title", "Open " + fs::path(c.file).filename().string());
            }
        }
    }
    for (const std::string& group : shortcut_groups()) {
        add(body, "p", group == "General" ? "General" : group == "Editor" ? "Code editor keys" : "Mouse", "settings-group");
        for (const Shortcut& s : shortcuts()) {
            if (s.group != group) continue;
            const ShortcutBinding* b = s.fixed ? nullptr : binding_for(bindings, s.id);
            std::vector<const ShortcutConflict*> clashes;
            for (const ShortcutConflict& c : conflicts) {
                if (c.first == &s || c.second == &s) clashes.push_back(&c);
            }
            grove::Node* row = setting_row(body, s.fixed ? std::string() : "keys", s.fixed ? std::string() : s.id, s.label, s.help);
            row->set_attribute("class", clashes.empty() && !(b && b->invalid) ? "setting shortcut-row" : "setting shortcut-row clash");
            grove::Node* text = nullptr;
            for (const auto& child : row->children()) {
                if (child->is_element() && child->has_class("setting-text")) text = child.get();
            }
            for (const ShortcutConflict* c : clashes) {
                const Shortcut* other = c->first == &s ? c->second : c->first;
                if (text) add(*text, "div", grove::chord_text(c->chord) + " is also " + other->label + " (" + other->group + ")", "shortcut-clash");
            }
            if (b && b->invalid && text) add(*text, "div", "\"" + b->text + "\" isn't a shortcut Canopy understands", "shortcut-clash");
            if (s.fixed) {
                add(*row, "text", s.fixed_text, "shortcut-fixed");
                continue;
            }
            grove::Node* input = m_doc.create_element("input_string");
            input->set_attribute("class", "setting-control shortcut-input");
            input->set_attribute("style", "width: 200px");
            input->set_attribute("value", b ? b->text : grove::chords_text(s.defaults));
            input->set_attribute("placeholder", "none");
            input->set_attribute("data-section", "keys");
            input->set_attribute("data-key", s.id);
            m_doc.append_child(*row, *input);
            const bool recording = m_recording == s.id;
            grove::Node* record = add(*row, "button", recording ? (s.mouse ? "Click here with the keys held" : "Press the keys\xE2\x80\xA6") : "Record", recording ? "shortcut-record recording" : "shortcut-record");
            record->set_attribute("data-record", s.id);
            record->set_attribute("title", recording ? (s.mouse ? "Hold Ctrl, Shift or Alt and click here. Escape cancels." : "Press the keys you want. Escape cancels.") : "Record a new shortcut");
        }
    }
}

void SettingsPage::write_shortcut(const std::string& id, const std::string& text) {
    std::string value = text;
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::size_t a = lowered.find_first_not_of(" \t");
    if (a == std::string::npos || lowered.substr(a) == "none") {
        value = "none";
    } else {
        const std::vector<grove::Chord> chords = grove::parse_chords(value);
        std::size_t parts = 1;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == ',' && !(i > 0 && value[i - 1] == '+')) ++parts;
        }
        if (!chords.empty() && chords.size() == parts) value = grove::chords_text(chords);
    }
    write("keys", id, bark_setting_value(value, true));
}

bool SettingsPage::record_key(grove::Event& e) {
    if (m_recording.empty()) return false;
    const Shortcut* s = find_shortcut(m_recording);
    if (!s || grove::is_modifier_key(e.key)) return s != nullptr;
    const grove::Chord chord = grove::chord_of(e.key, e.modifiers);
    if (chord.key == "Escape" && !chord.ctrl && !chord.shift && !chord.alt) {
        m_recording.clear();
        rebuild();
        return true;
    }
    if (s->mouse || chord.empty()) return true;
    const std::string id = m_recording;
    m_recording.clear();
    write_shortcut(id, grove::chord_text(chord));
    return true;
}

void SettingsPage::heading(grove::Node& body, const std::string& title, const std::string& note) {
    add(body, "h2", title, "settings-heading");
    if (!note.empty()) add(body, "p", note, "settings-note");
}

grove::Node* SettingsPage::setting_row(grove::Node& parent, const std::string& section, const std::string& key) {
    const std::optional<SettingDoc> doc = find_setting_doc(section, key);
    return setting_row(parent, section, key, doc ? doc->label : key, doc ? doc->help : std::string());
}

grove::Node* SettingsPage::setting_row(grove::Node& parent, const std::string& section, const std::string& key, const std::string& label, const std::string& help) {
    grove::Node* row = add(parent, "row", "", "setting");
    grove::Node* text = add(*row, "column", "", "setting-text");
    add(*text, "div", label, "setting-name");
    if (!help.empty()) add(*text, "div", help, "setting-help");
    const Setting* s = m_ws.settings.find(section, key);
    if (s && s->source != SettingSource::Default) {
        grove::Node* where = add(*text, "row", "", "setting-where");
        const std::string file = fs::path(s->file).filename().string();
        add(*where, "text", s->source == SettingSource::Workspace ? "Set by this folder in .canopy/" + file : "Changed in " + file, "setting-source");
        if (s->source == SettingSource::User) {
            grove::Node* reset_link = add(*where, "text", "Reset", "setting-reset");
            reset_link->set_attribute("data-reset-section", section);
            reset_link->set_attribute("data-reset-key", key);
        }
    }
    return row;
}

void SettingsPage::text_control(grove::Node& row, const std::string& section, const std::string& key, const std::string& width) {
    grove::Node* input = m_doc.create_element("input_string");
    input->set_attribute("class", "setting-control");
    input->set_attribute("style", "width: " + width);
    input->set_attribute("value", m_ws.settings.get_or(section, key, ""));
    input->set_attribute("data-section", section);
    input->set_attribute("data-key", key);
    m_doc.append_child(row, *input);
}

void SettingsPage::integer_control(grove::Node& row, const std::string& section, const std::string& key, int min, int max) {
    grove::Node* input = m_doc.create_element("input_integer");
    input->set_attribute("class", "setting-control");
    input->set_attribute("style", "width: 90px");
    const double value = m_ws.settings.number(section, key).value_or(min);
    input->set_attribute("value", std::to_string(static_cast<long long>(value)));
    input->set_attribute("min", std::to_string(min));
    input->set_attribute("max", std::to_string(max));
    input->set_attribute("data-section", section);
    input->set_attribute("data-key", key);
    m_doc.append_child(row, *input);
}

void SettingsPage::bool_control(grove::Node& row, const std::string& section, const std::string& key) {
    grove::Node* input = m_doc.create_element("input_bool");
    input->set_attribute("class", "setting-control");
    input->set_attribute("style", "appearance: switch");
    if (m_ws.settings.flag(section, key, false)) input->set_attribute("checked", "");
    input->set_attribute("data-section", section);
    input->set_attribute("data-key", key);
    m_doc.append_child(row, *input);
}

void SettingsPage::choice_control(grove::Node& row, const std::string& section, const std::string& key, const std::vector<std::string>& choices) {
    grove::Node* group = add(row, "row", "", "setting-choices");
    const std::string current = m_ws.settings.get_or(section, key, "");
    const std::string name = "setting-" + section + "-" + key;
    for (const std::string& choice : choices) {
        grove::Node* radio = m_doc.create_element("input_radio");
        radio->set_attribute("name", name);
        radio->set_attribute("value", choice);
        if (choice == current) radio->set_attribute("checked", "");
        radio->set_attribute("data-section", section);
        radio->set_attribute("data-key", key);
        radio->set_inner_text(choice);
        m_doc.append_child(*group, *radio);
    }
}

void SettingsPage::build_editor(grove::Node& body) {
    heading(body, "Editor", "Changes are saved to your editor.bark as you make them.");
    grove::Node* row = setting_row(body, "editor", "font-family");
    font_control(*row, "editor", "font-family");
    row = setting_row(body, "editor", "font-size");
    size_control(*row, "editor", "font-size", "14px");
    row = setting_row(body, "editor", "tab-size");
    integer_control(*row, "editor", "tab-size", 1, 16);
    row = setting_row(body, "editor", "insert-spaces");
    bool_control(*row, "editor", "insert-spaces");
    row = setting_row(body, "editor", "backspace-to-tab-stop");
    bool_control(*row, "editor", "backspace-to-tab-stop");
    row = setting_row(body, "editor", "close-blocks");
    bool_control(*row, "editor", "close-blocks");
    row = setting_row(body, "editor", "line-numbers");
    bool_control(*row, "editor", "line-numbers");
    row = setting_row(body, "editor", "hover-width");
    integer_control(*row, "editor", "hover-width", 300, 4000);
    row = setting_row(body, "editor", "hover-height");
    integer_control(*row, "editor", "hover-height", 100, 3000);
    row = setting_row(body, "editor", "autocomplete");
    bool_control(*row, "editor", "autocomplete");
    row = setting_row(body, "editor", "accept-suggestion-on-enter");
    bool_control(*row, "editor", "accept-suggestion-on-enter");
    row = setting_row(body, "editor", "word-suggestions");
    bool_control(*row, "editor", "word-suggestions");
    row = setting_row(body, "editor", "suggestion-icons");
    bool_control(*row, "editor", "suggestion-icons");
    row = setting_row(body, "editor", "suggestion-kinds");
    bool_control(*row, "editor", "suggestion-kinds");
    grove::Node* more = add(body, "p", "", "settings-note");
    more->set_inner_text("Colors and themes are under Highlighting.");
}

double SettingsPage::page_zoom() const {
    return gui_zoom(m_ws.settings, m_doc.host().viewport_width, m_doc.host().viewport_height);
}

void SettingsPage::size_control(grove::Node& row, const std::string& section, const std::string& key, const std::string& fallback) {
    const SizeValue size = parse_size(m_ws.settings.get_or(section, key, fallback)).value_or(parse_size(fallback).value_or(SizeValue{}));
    const auto range = size_range(size.unit);
    grove::Node* value = m_doc.create_element("input_float");
    value->set_attribute("class", "setting-control size-value");
    value->set_attribute("value", size_number(size.value));
    value->set_attribute("min", size_number(range.first));
    value->set_attribute("max", size_number(range.second));
    value->set_attribute("step", size.unit == "px" || size.unit == "%" ? "1" : size.unit == "pt" ? "0.5" : "0.005");
    value->set_attribute("data-section", section);
    value->set_attribute("data-key", key);
    value->set_attribute("data-size-part", "value");
    m_doc.append_child(row, *value);
    grove::Node* unit = m_doc.create_element("input_select");
    unit->set_attribute("class", "setting-control size-unit");
    unit->set_attribute("value", size.unit);
    unit->set_attribute("data-section", section);
    unit->set_attribute("data-key", key);
    unit->set_attribute("data-size-part", "unit");
    unit->set_attribute("title", "Unit");
    for (const std::string& u : size_units()) {
        grove::Node* option = m_doc.create_element("option");
        option->set_attribute("value", u);
        option->set_inner_text(u);
        m_doc.append_child(*unit, *option);
    }
    m_doc.append_child(row, *unit);
}

void SettingsPage::font_control(grove::Node& row, const std::string& section, const std::string& key) {
    const std::string current = m_ws.settings.get_or(section, key, "monospace");
    grove::Node* select = m_doc.create_element("input_select");
    select->set_attribute("class", "setting-control font-select");
    select->set_attribute("data-section", section);
    select->set_attribute("data-key", key);
    select->set_attribute("value", current);
    std::vector<std::pair<std::string, std::string>> options = { { "monospace", "Monospace (default)" } };
    for (const std::string& name : grove::runtime::font_families("monospace")) options.emplace_back(name, name);
    const std::size_t mono = options.size();
    for (const char* kind : { "sans-serif", "serif", "display", "handwriting" }) {
        for (const std::string& name : grove::runtime::font_families(kind)) options.emplace_back(name, name);
    }
    bool found = false;
    for (const auto& o : options) found = found || o.first == current;
    if (!found && !current.empty()) options.insert(options.begin(), { current, current });
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (i == mono + (found ? 0 : 1)) {
            grove::Node* gap = m_doc.create_element("option");
            gap->set_attribute("value", "");
            gap->set_attribute("disabled", "");
            gap->set_inner_text("\xE2\x80\x94 not monospace \xE2\x80\x94");
            m_doc.append_child(*select, *gap);
        }
        grove::Node* option = m_doc.create_element("option");
        option->set_attribute("value", options[i].first);
        option->set_inner_text(options[i].second);
        m_doc.append_child(*select, *option);
    }
    m_doc.append_child(row, *select);
}

void SettingsPage::on_size_change(grove::Node& target) {
    const std::string section = target.attribute_or("data-section", "");
    const std::string key = target.attribute_or("data-key", "");
    const grove::Node* row = target.parent();
    if (!row) return;
    const grove::Node* value_node = nullptr;
    const grove::Node* unit_node = nullptr;
    for (const auto& child : row->children()) {
        if (!child->is_element()) continue;
        const std::string part = child->attribute_or("data-size-part", "");
        if (part == "value") value_node = child.get();
        else if (part == "unit") unit_node = child.get();
    }
    if (!value_node || !unit_node) return;
    const std::string fallback = section == "gui" || section == "terminal" ? "13px" : "14px";
    const SizeValue before = parse_size(m_ws.settings.get_or(section, key, fallback)).value_or(parse_size(fallback).value_or(SizeValue{}));
    SizeValue size{ grove::input_float(*value_node).value_or(before.value), grove::input_string(*unit_node).value_or(before.unit) };
    if (target.attribute_or("data-size-part", "") == "unit" && size.unit != before.unit) {
        if (const auto converted = convert_size(before, size.unit, m_doc.host().viewport_width, m_doc.host().viewport_height)) {
            size = *converted;
        } else {
            const auto range = size_range(size.unit);
            size.value = std::clamp(size.value, range.first, range.second);
        }
    }
    const auto range = size_range(size.unit);
    size.value = std::clamp(size.value, range.first, range.second);
    if (size.value <= 0.0) return;
    write(section, key, size_text(size));
}

void SettingsPage::build_gui(grove::Node& body) {
    heading(body, "GUI", "How big Canopy's own parts are: tabs, the explorer, the panel, the status bar and these settings. Saved to your editor.bark.");
    grove::Node* row = setting_row(body, "gui", "size");
    size_control(*row, "gui", "size", "13px");
}

void SettingsPage::tool_row(grove::Node& body, const std::string& key, const std::string& label) {
    const Tool* tool = m_ws.toolchain.get(key);
    std::string help = tool ? "Using " + tool->path + " (" + origin_text(tool->origin) + ")." : std::string("Not found. Install it, or point this at it.");
    help += " auto finds it for you.";
    grove::Node* row = setting_row(body, "toolchain", key, label, help);
    text_control(*row, "toolchain", key, "200px");
    grove::Node* browse = m_doc.create_element("input_file");
    browse->set_attribute("mode", "open");
    browse->set_attribute("hide-value", "");
    browse->set_attribute("title", "Choose the " + label);
    browse->set_attribute("class", "setting-browse");
    browse->set_attribute("data-section", "toolchain");
    browse->set_attribute("data-key", key);
    browse->set_inner_text("Browse\xE2\x80\xA6");
    m_doc.append_child(*row, *browse);
}

void SettingsPage::build_compilers(grove::Node& body) {
    heading(body, "Compilers", "Where each tool lives and the flags each language builds with. Saved to your compilers.bark.");
    add(body, "h3", "Tools", "settings-group");
    tool_row(body, "c", "C compiler");
    tool_row(body, "cpp", "C++ compiler");
    tool_row(body, "walnut", "Walnut");
    tool_row(body, "grove", "Grove");

    auto flags = [&](const std::string& section, bool debug_release) {
        grove::Node* row = setting_row(body, section, "flags");
        text_control(*row, section, "flags");
        if (!debug_release) return;
        row = setting_row(body, section, "debug-flags");
        text_control(*row, section, "debug-flags");
        row = setting_row(body, section, "release-flags");
        text_control(*row, section, "release-flags");
    };
    add(body, "h3", "C", "settings-group");
    grove::Node* row = setting_row(body, "language.c", "standard");
    choice_control(*row, "language.c", "standard", { "c99", "c11", "c17", "c23" });
    flags("language.c", true);
    add(body, "h3", "C++", "settings-group");
    row = setting_row(body, "language.cpp", "standard");
    choice_control(*row, "language.cpp", "standard", { "c++11", "c++14", "c++17", "c++20", "c++23" });
    flags("language.cpp", true);
    add(body, "h3", "Walnut", "settings-group");
    flags("language.walnut", true);
    add(body, "h3", "Grove", "settings-group");
    flags("language.grove", false);
}

void SettingsPage::viewer_tool_row(grove::Node& body, const std::string& key, const std::string& label, const std::string& kinds) {
    const std::optional<ViewerTool> tool = find_viewer_tool(m_ws.settings, key);
    std::string help = tool ? "Using " + tool->path + "." : std::string("Not found. ") + viewer_install_hint(key);
    help += " " + kinds + " auto picks the first one installed.";
    grove::Node* row = setting_row(body, "viewer", key, label, help);
    text_control(*row, "viewer", key, "200px");
    grove::Node* browse = m_doc.create_element("input_file");
    browse->set_attribute("mode", "open");
    browse->set_attribute("hide-value", "");
    browse->set_attribute("title", "Choose the " + label);
    browse->set_attribute("class", "setting-browse");
    browse->set_attribute("data-section", "viewer");
    browse->set_attribute("data-key", key);
    browse->set_inner_text("Browse\xE2\x80\xA6");
    m_doc.append_child(*row, *browse);
}

void SettingsPage::build_viewers(grove::Node& body) {
    heading(body, "Viewers", "How images, PDFs, LaTeX, Markdown and binary files open. Saved to your editor.bark.");
    add(body, "h3", "PDF", "settings-group");
    viewer_tool_row(body, "pdf", "PDF renderer", "pdftoppm, mutool, pdftocairo or Ghostscript.");
    grove::Node* row = setting_row(body, "viewer", "pdf-resolution");
    integer_control(*row, "viewer", "pdf-resolution", 36, 600);
    add(body, "h3", "LaTeX", "settings-group");
    viewer_tool_row(body, "latex", "LaTeX", "latexmk, tectonic, pdflatex, xelatex or lualatex.");
    row = setting_row(body, "viewer", "latex-flags");
    text_control(*row, "viewer", "latex-flags");
    row = setting_row(body, "viewer", "latex-preview");
    bool_control(*row, "viewer", "latex-preview");
    add(body, "h3", "Markdown", "settings-group");
    row = setting_row(body, "viewer", "markdown-preview");
    bool_control(*row, "viewer", "markdown-preview");
    add(body, "h3", "Images and binary files", "settings-group");
    add(body, "p", "Images (PNG, JPEG, GIF, BMP, WebP, TIFF, TGA, QOI, PNM, ICO and SVG) open as pictures. Programs, libraries, archives and other files that aren't text open in the hex view, with Info, Hex, Binary and Strings tabs.", "settings-note");
}

void SettingsPage::build_build(grove::Node& body) {
    heading(body, "Build and Run", "What Build (Ctrl+B) and Run (F5) do. Saved to your build.bark.");
    grove::Node* row = setting_row(body, "build", "mode");
    choice_control(*row, "build", "mode", { "debug", "release" });
    row = setting_row(body, "build", "output-dir");
    text_control(*row, "build", "output-dir", "200px");
    row = setting_row(body, "build", "save-before-build");
    bool_control(*row, "build", "save-before-build");
}

void SettingsPage::build_workbench(grove::Node& body) {
    heading(body, "Explorer, Terminal and Git", "How the file explorer, the terminal, the local server, Git and local file history behave. Saved to your workbench.bark.");
    grove::Node* row = setting_row(body, "explorer", "preview-tabs");
    bool_control(*row, "explorer", "preview-tabs");
    row = setting_row(body, "explorer", "confirm-delete");
    bool_control(*row, "explorer", "confirm-delete");
    row = setting_row(body, "explorer", "confirm-drag-and-drop");
    bool_control(*row, "explorer", "confirm-drag-and-drop");
    row = setting_row(body, "explorer", "confirm-replace");
    bool_control(*row, "explorer", "confirm-replace");
    row = setting_row(body, "terminal", "shell");
    text_control(*row, "terminal", "shell", "260px");
    row = setting_row(body, "terminal", "prompt");
    choice_control(*row, "terminal", "prompt", { "canopy", "shell" });
    row = setting_row(body, "terminal", "font-size");
    size_control(*row, "terminal", "font-size", "13px");
    row = setting_row(body, "terminal", "scrollback");
    integer_control(*row, "terminal", "scrollback", 100, 1000000);
    row = setting_row(body, "terminal", "tab-size");
    integer_control(*row, "terminal", "tab-size", 1, 16);
    add(body, "h3", "Local server", "settings-group");
    row = setting_row(body, "server", "port");
    integer_control(*row, "server", "port", 1, 65535);
    row = setting_row(body, "server", "host");
    text_control(*row, "server", "host", "160px");
    row = setting_row(body, "server", "live-reload");
    bool_control(*row, "server", "live-reload");
    add(body, "h3", "Git and file history", "settings-group");
    row = setting_row(body, "git", "refresh-seconds");
    integer_control(*row, "git", "refresh-seconds", 1, 600);
    row = setting_row(body, "git", "protected-branches");
    text_control(*row, "git", "protected-branches", "260px");
    row = setting_row(body, "history", "enabled");
    bool_control(*row, "history", "enabled");
    row = setting_row(body, "history", "limit");
    integer_control(*row, "history", "limit", 1, 10000);
}

void SettingsPage::theme_cards(grove::Node& body) {
    const std::string current = m_draft_theme;
    const std::size_t per_row = 5;
    grove::Node* line = nullptr;
    for (std::size_t i = 0; i < m_themes.size(); ++i) {
        const Theme& theme = m_themes[i];
        if (i % per_row == 0) line = add(body, "row", "", "theme-row");
        grove::Node* card = add(*line, "column", "", theme.id == current ? "theme-card active" : "theme-card");
        card->set_attribute("data-theme", theme.id);
        if (!theme.background.empty()) card->set_attribute("style", "background: " + theme.background);
        grove::Node* name = add(*card, "div", theme.builtin ? theme.name : theme.name + "  \xC2\xB7  yours", "theme-name");
        if (!theme.foreground.empty()) name->set_attribute("style", "color: " + theme.foreground);
        grove::Node* swatches = add(*card, "row", "", "theme-swatches");
        const auto colors = resolve_highlight(Settings(), &theme, "cpp");
        for (const char* kind : { "keyword", "type", "function", "string", "number", "comment" }) {
            const auto it = colors.find(kind);
            if (it == colors.end()) continue;
            const HighlightLook look = parse_highlight_look(it->second);
            if (look.color.empty()) continue;
            grove::Node* swatch = add(*swatches, "div", "", "theme-swatch");
            swatch->set_attribute("style", "background: " + look.color);
        }
    }
    while (line && line->children().size() < per_row) add(*line, "div", "", "theme-card filler");
}

std::string SettingsPage::highlight_section() const {
    return m_language == "all" ? std::string("highlight") : "highlight." + m_language;
}

HighlightLook SettingsPage::current_look(const std::string& kind) const {
    const Theme* theme = draft_base();
    const auto colors = draft_colors(m_language == "all" ? std::string() : m_language);
    const std::string value = highlight_value(colors, kind);
    HighlightLook look = value.empty() ? HighlightLook{} : parse_highlight_look(value);
    if (look.color.empty() && kind == "error-underline") look.color = "#f14c4c";
    if (look.color.empty() && kind == "warning-underline") look.color = "#cca700";
    if (look.color.empty() && kind == "error-line") {
        const std::string squiggle = highlight_value(colors, "error-underline");
        look.color = squiggle.empty() ? std::string("#f14c4c") : parse_highlight_look(squiggle).color;
    }
    if (look.color.empty() && kind == "background" && theme) look.color = theme->background;
    if (look.color.empty() && theme) look.color = theme->foreground;
    if (look.color.empty()) look.color = "#cdd6f4";
    return look;
}

SettingsPage::Sections SettingsPage::saved_draft() const {
    Sections out;
    std::vector<std::string> sections = { "highlight" };
    for (const std::string& language : highlight_languages()) sections.push_back("highlight." + language);
    for (const std::string& section : sections) {
        for (const std::string& kind : highlight_kinds()) {
            if (auto v = m_ws.settings.get(section, kind); v && !v->empty()) out[section][kind] = *v;
        }
    }
    return out;
}

void SettingsPage::load_draft() {
    m_saved = saved_draft();
    m_saved_theme = theme_id(m_ws.settings);
    m_draft = m_saved;
    m_draft_theme = m_saved_theme;
    m_draft_ready = true;
}

bool SettingsPage::draft_dirty() const {
    return m_draft_theme != m_saved_theme || m_draft != m_saved;
}

const Theme* SettingsPage::draft_base() const {
    const Theme* theme = find_theme(m_themes, m_draft_theme);
    if (!theme && !m_themes.empty()) theme = &m_themes.front();
    return theme;
}

std::map<std::string, std::string> SettingsPage::draft_colors(const std::string& language) const {
    std::map<std::string, std::string> out;
    const Theme* theme = draft_base();
    const std::string own = "highlight." + language;
    auto layer = [&](const Sections& from, const std::string& section) {
        const auto it = from.find(section);
        if (it == from.end()) return;
        for (const auto& [key, value] : it->second) {
            if (!value.empty()) out[key] = value;
        }
    };
    if (theme) {
        if (!theme->background.empty()) out["background"] = theme->background;
        if (!theme->foreground.empty()) out["foreground"] = theme->foreground;
        layer(theme->highlight, "highlight");
        if (!language.empty()) layer(theme->highlight, own);
    }
    layer(m_draft, "highlight");
    if (!language.empty()) layer(m_draft, own);
    return out;
}

SettingsPage::Sections SettingsPage::draft_theme_sections() const {
    Sections out;
    out["highlight"] = draft_colors(std::string());
    out["highlight"].erase("background");
    out["highlight"].erase("foreground");
    const std::map<std::string, std::string> global = out["highlight"];
    for (const std::string& language : highlight_languages()) {
        const std::map<std::string, std::string> colors = draft_colors(language);
        for (const auto& [kind, value] : colors) {
            if (is_theme_color_kind(kind)) continue;
            const auto g = global.find(kind);
            if (g == global.end() || g->second != value) out["highlight." + language][kind] = value;
        }
    }
    return out;
}

void SettingsPage::color_row(grove::Node& list, const std::string& kind) {
    const KindInfo* info = kind_info(kind);
    const HighlightLook look = current_look(kind);
    const auto section = m_draft.find(highlight_section());
    const bool changed = section != m_draft.end() && section->second.count(kind) != 0;

    grove::Node* row = add(list, "row", "", kind == m_picked ? "color-row lit" : "color-row");
    row->set_attribute("data-kind", kind);
    grove::Node* color = m_doc.create_element("input_color");
    color->set_attribute("class", "color-input");
    color->set_attribute("value", look.color);
    color->set_attribute("data-color-kind", kind);
    m_doc.append_child(*row, *color);
    for (const auto& [flag, letter, on] : { std::tuple<const char*, const char*, bool>{ "bold", "B", look.bold }, { "italic", "I", look.italic }, { "underline", "U", look.underline }, { "strikethrough", "S", look.strikethrough } }) {
        if (is_theme_color_kind(kind) || kind.find("underline") != std::string::npos) {
            add(*row, "div", "", "style-toggle spacer");
            continue;
        }
        grove::Node* toggle = add(*row, "div", letter, std::string("style-toggle ") + flag + (on ? " on" : ""));
        toggle->set_attribute("data-toggle", flag);
        toggle->set_attribute("data-toggle-kind", kind);
        toggle->set_attribute("title", std::string(flag));
    }
    grove::Node* text = add(*row, "column", "", "color-text");
    grove::Node* name = add(*text, "div", info ? info->name : kind, "color-name");
    const Theme* theme = draft_base();
    std::string name_style = "color: " + look.color;
    if (theme && !theme->background.empty()) name_style += "; background: " + theme->background;
    if (look.bold) name_style += "; font-weight: bold";
    if (look.italic) name_style += "; font-style: italic";
    if (look.underline || look.strikethrough) name_style += std::string("; text-decoration:") + (look.underline ? " underline" : "") + (look.strikethrough ? " line-through" : "");
    name->set_attribute("style", name_style);
    add(*text, "div", kind_what(m_language, kind), "color-what");
    if (changed) {
        grove::Node* clear = add(*row, "div", "\xC3\x97", "color-clear");
        clear->set_attribute("data-clear-kind", kind);
        clear->set_attribute("title", "Use the theme's color");
    }
    m_doc.events().add_listener(row, "mouse_enter", [this, kind](grove::Event&) { mark(kind); });
    m_doc.events().add_listener(row, "mouse_leave", [this](grove::Event&) { mark(m_picked); });
}

void SettingsPage::build_highlighting(grove::Node& body) {
    if (!m_draft_ready) load_draft();
    heading(body, "Highlighting", "Pick a theme, then change any color for every language or just one. Point at a color to light up what it changes in the preview, or click a word in the preview to find its color. Nothing changes in your editor until you press Apply.");
    grove::Node* bar = add(body, "row", "", "theme-actions");
    for (const auto& [action, label] : std::vector<std::pair<std::string, std::string>>{
             { "apply", "Apply" }, { "save-theme", "Save as theme\xE2\x80\xA6" }, { "copy-bark", "Copy Bark" }, { "copy-json", "Copy JSON" }, { "discard", "Discard changes" } }) {
        grove::Node* button = add(*bar, "button", label, action == "apply" ? "theme-action primary" : "theme-action");
        button->set_attribute("data-action", action);
        if ((action == "apply" || action == "discard") && !draft_dirty()) button->set_attribute("disabled", "");
    }
    if (const Theme* current = draft_base(); current && !current->builtin && !current->file.empty()) {
        grove::Node* edit = add(*bar, "button", "Edit theme file", "theme-action");
        edit->set_attribute("data-action", "edit-theme");
    }
    m_status = add(*bar, "text", "", "theme-status");
    set_notice(m_notice);
    add(body, "h3", "Theme", "settings-group");
    theme_cards(body);

    grove::Node* chips = add(body, "row", "", "language-chips");
    for (const auto& [id, label] : language_names()) {
        grove::Node* chip = add(*chips, "div", label, id == m_language ? "language-chip active" : "language-chip");
        chip->set_attribute("data-language", id);
    }

    grove::Node* split = add(body, "row", "", "highlight-split");
    grove::Node* list = add(*split, "column", "", "color-list");
    for (const std::string& kind : kinds_for(m_language)) color_row(*list, kind);
    const std::string section = highlight_section();
    const auto own = m_draft.find(section);
    const bool any = own != m_draft.end() && !own->second.empty();
    if (any) {
        grove::Node* reset_all = add(*list, "p", m_language == "all" ? "Use the theme's colors everywhere" : "Use the theme's colors for this language", "color-reset-all");
        reset_all->set_attribute("data-reset-all", section);
    }

    grove::Node* side = add(*split, "column", "", "preview-side");
    const std::string language = m_language == "all" ? std::string("cpp") : m_language;
    m_preview = m_doc.create_element("input_code");
    m_preview->set_attribute("class", "hl-preview");
    m_preview->set_attribute("readonly", "");
    m_preview->set_attribute("language", language);
    m_preview->set_attribute("value", highlight_sample(language));
    if (const std::string spans = highlight_sample_spans(language); !spans.empty()) m_preview->set_attribute("semantic", spans);
    if (const std::string problems = highlight_sample_problems(language); !problems.empty()) m_preview->set_attribute("problems", problems);
    if (!m_marked.empty()) m_preview->set_attribute("mark", m_marked);
    apply_highlight_colors(m_doc, *m_preview, draft_colors(language), draft_base());
    grove::set_inline_style(m_doc, *m_preview, "font-size", editor_font_size(m_ws.settings, page_zoom()));
    m_doc.append_child(*side, *m_preview);
    std::string where = "Changes here apply to every language; a language's own colors win over them.";
    for (const auto& [id, label] : language_names()) {
        if (id == m_language && id != "all") where = "Changes here apply to " + label + " files only, on top of the colors for every language.";
    }
    add(*side, "p", where, "settings-note");
}

void SettingsPage::mark(const std::string& kind) {
    m_marked = kind;
    if (!m_preview) return;
    if (kind.empty()) m_preview->remove_attribute("mark");
    else m_preview->set_attribute("mark", kind);
    m_doc.touch();
}

std::string SettingsPage::target_file(const std::string& section, const std::string& key) const {
    const Setting* s = m_ws.settings.find(section, key);
    if (s && s->source == SettingSource::User && !s->file.empty()) return s->file;
    return user_config_file(settings_file_for_section(section));
}

void SettingsPage::write(const std::string& section, const std::string& key, const std::string& value) {
    write_setting(target_file(section, key), section, key, value);
    if (m_hooks.changed) m_hooks.changed();
}

void SettingsPage::reset(const std::string& section, const std::string& key) {
    for (const std::string& file : user_settings_files()) write_setting(file, section, key, "");
    if (m_hooks.changed) m_hooks.changed();
}

void SettingsPage::write_look(const std::string& kind, const HighlightLook& look) {
    m_draft[highlight_section()][kind] = highlight_look_text(look);
    m_notice.clear();
}

void SettingsPage::set_notice(const std::string& text) {
    m_notice = text;
    if (!m_status) return;
    std::string shown = text;
    if (shown.empty()) shown = draft_dirty() ? "Not applied yet: only the preview shows these colors." : "These are the colors your editor uses.";
    m_status->set_inner_text(shown);
    m_doc.touch();
}

void SettingsPage::show_draft() {
    if (!m_preview || !m_body) return;
    const std::string language = m_language == "all" ? std::string("cpp") : m_language;
    apply_highlight_colors(m_doc, *m_preview, draft_colors(language), draft_base());
    const Theme* theme = draft_base();
    if (grove::Node* list = grove::query(*m_body, ".color-list")) {
        for (const auto& child : list->children()) {
            if (!child->is_element() || !child->has_attribute("data-kind")) continue;
            const std::string kind = child->attribute_or("data-kind", "");
            const HighlightLook look = current_look(kind);
            for (const auto& part : child->children()) {
                if (!part->is_element()) continue;
                if (part->has_class("color-text")) {
                    for (const auto& line : part->children()) {
                        if (!line->is_element() || !line->has_class("color-name")) continue;
                        std::string style = "color: " + look.color;
                        if (theme && !theme->background.empty()) style += "; background: " + theme->background;
                        if (look.bold) style += "; font-weight: bold";
                        if (look.italic) style += "; font-style: italic";
                        if (look.underline || look.strikethrough) style += std::string("; text-decoration:") + (look.underline ? " underline" : "") + (look.strikethrough ? " line-through" : "");
                        line->set_attribute("style", style);
                    }
                }
            }
        }
    }
    if (grove::Node* bar = grove::query(*m_body, ".theme-actions")) {
        for (const auto& child : bar->children()) {
            if (!child->is_element()) continue;
            const std::string action = child->attribute_or("data-action", "");
            if (action != "apply" && action != "discard") continue;
            if (draft_dirty()) child->remove_attribute("disabled");
            else child->set_attribute("disabled", "");
        }
    }
    set_notice(m_notice);
}

void SettingsPage::apply_draft() {
    std::vector<std::string> sections = { "highlight" };
    for (const std::string& language : highlight_languages()) sections.push_back("highlight." + language);
    const std::string target = user_config_file("highlighting.bark");
    for (const std::string& file : user_settings_files()) {
        if (file == target) continue;
        for (const std::string& section : sections) {
            for (const std::string& kind : highlight_kinds()) {
                if (m_ws.settings.get(section, kind)) write_setting(file, section, kind, "");
            }
        }
    }
    for (const std::string& section : sections) {
        const auto own = m_draft.find(section);
        for (const std::string& kind : highlight_kinds()) {
            std::string value;
            if (own != m_draft.end()) {
                const auto v = own->second.find(kind);
                if (v != own->second.end()) value = v->second;
            }
            const auto current = m_ws.settings.get(section, kind);
            if (value == current.value_or("")) continue;
            write_setting(target, section, kind, value);
        }
    }
    if (m_draft_theme != theme_id(m_ws.settings)) write_setting(target_file("editor", "theme"), "editor", "theme", bark_setting_value(m_draft_theme, true));
    m_saved = m_draft;
    m_saved_theme = m_draft_theme;
    m_notice = "Applied. Your editor uses these colors now.";
    if (m_hooks.changed) m_hooks.changed();
}

void SettingsPage::ask_theme_name() {
    const Theme* base = draft_base();
    grove::PopupOptions options;
    options.kind = "question";
    options.title = "Save as theme";
    options.message = "Name the new theme. It's saved in your themes folder and shows up with the others.";
    options.input = "string";
    options.value = base ? (draft_dirty() ? base->name + " (mine)" : base->name + " copy") : std::string("My theme");
    if (grove::Node* popup = grove::create_popup(m_doc, nullptr, options)) {
        m_doc.events().add_listener(popup, "ok", [this, popup](grove::Event&) {
            const std::string name = popup->attribute_or("value", "");
            m_doc.events().set_timeout(0.0, [this, name] { save_theme(name); });
        });
    }
}

void SettingsPage::save_theme(const std::string& raw_name) {
    std::string name = raw_name;
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    if (name.empty()) {
        set_notice("A theme needs a name.");
        return;
    }
    std::string id = theme_slug(name);
    for (const Theme& t : m_themes) {
        if (t.id == id && t.builtin) {
            id += "-mine";
            break;
        }
    }
    const Theme* base = draft_base();
    const std::map<std::string, std::string> colors = draft_colors(std::string());
    const auto pick = [&colors](const char* kind) {
        const auto it = colors.find(kind);
        return it == colors.end() ? std::string() : parse_highlight_look(it->second).color;
    };
    const std::string text = theme_bark_text(name, pick("background"), pick("foreground"), draft_theme_sections(), base ? base->name : std::string());
    const std::string path = (fs::path(user_themes_dir()) / (id + ".bark")).string();
    std::error_code ec;
    fs::create_directories(user_themes_dir(), ec);
    if (!write_text_file(path, text)) {
        set_notice("Couldn't write " + path);
        return;
    }
    grove::Diagnostics diagnostics;
    m_themes = load_themes(diagnostics);
    m_draft_theme = id;
    m_draft.clear();
    apply_draft();
    m_notice = "Saved as \"" + name + "\" and switched to it. Edit theme file opens it to change its background, text or any color by hand.";
    rebuild();
}

void SettingsPage::copy_theme(bool json) {
    const Theme* base = draft_base();
    std::string name = base ? base->name : std::string("Theme");
    if (draft_dirty() || !m_draft.empty()) name += " (edited)";
    const std::map<std::string, std::string> colors = draft_colors(std::string());
    const auto pick = [&colors](const char* kind) {
        const auto it = colors.find(kind);
        return it == colors.end() ? std::string() : parse_highlight_look(it->second).color;
    };
    const std::string bg = pick("background");
    const std::string fg = pick("foreground");
    const std::string text = json ? theme_json_text(name, bg, fg, draft_theme_sections()) : theme_bark_text(name, bg, fg, draft_theme_sections(), base ? base->name : std::string());
    if (grove::set_clipboard_text(m_doc, text)) set_notice(json ? "Copied the colors as JSON." : "Copied the colors as a Bark theme.");
    else set_notice("Couldn't reach the clipboard.");
}

void SettingsPage::on_click(grove::Event& e) {
    if (const grove::Node* n = with_attribute(e.target, "data-page")) {
        show(n->attribute_or("data-page", "editor"));
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-open-file")) {
        const std::string path = user_config_file(n->attribute_or("data-open-file", "editor.bark"));
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            fs::create_directories(fs::path(path).parent_path(), ec);
            const std::string name = fs::path(path).filename().string();
            write_text_file(path, "// Your " + fs::path(name).stem().string() + " settings. Only what you change is kept here; everything else\n"
                                  "// comes from Canopy's built-in " + name + ", shown below for reference.\n\n" +
                                  "/*\n" + std::string(default_config_file(name)) + "*/\n");
        }
        if (m_hooks.open_file) m_hooks.open_file(path);
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-record")) {
        const std::string id = n->attribute_or("data-record", "");
        const Shortcut* s = find_shortcut(id);
        if (s && s->mouse && m_recording == id) {
            m_recording.clear();
            write_shortcut(id, grove::chord_text(grove::chord_of(e.button == 2 ? "RightClick" : e.button == 3 ? "MiddleClick" : "Click", e.modifiers)));
            return;
        }
        m_recording = m_recording == id ? std::string() : id;
        rebuild();
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-open-path")) {
        if (m_hooks.open_file) m_hooks.open_file(n->attribute_or("data-open-path", ""));
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-reset-key")) {
        reset(n->attribute_or("data-reset-section", ""), n->attribute_or("data-reset-key", ""));
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-action")) {
        if (n->has_attribute("disabled")) return;
        const std::string action = n->attribute_or("data-action", "");
        if (action == "apply") apply_draft();
        else if (action == "save-theme") ask_theme_name();
        else if (action == "copy-bark") copy_theme(false);
        else if (action == "copy-json") copy_theme(true);
        else if (action == "edit-theme") {
            if (const Theme* current = draft_base(); current && !current->builtin && m_hooks.open_file) m_hooks.open_file(current->file);
        }
        else if (action == "discard") {
            load_draft();
            m_notice.clear();
            rebuild();
        }
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-theme")) {
        m_draft_theme = n->attribute_or("data-theme", "canopy-dark");
        m_draft.clear();
        m_notice.clear();
        rebuild();
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-language")) {
        m_language = n->attribute_or("data-language", "cpp");
        m_marked.clear();
        m_picked.clear();
        rebuild();
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-toggle")) {
        const std::string kind = n->attribute_or("data-toggle-kind", "");
        HighlightLook look = current_look(kind);
        const std::string flag = n->attribute_or("data-toggle", "");
        if (flag == "bold") look.bold = !look.bold;
        else if (flag == "italic") look.italic = !look.italic;
        else if (flag == "underline") look.underline = !look.underline;
        else if (flag == "strikethrough") look.strikethrough = !look.strikethrough;
        m_picked = kind;
        write_look(kind, look);
        rebuild();
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-clear-kind")) {
        const std::string kind = n->attribute_or("data-clear-kind", "");
        const auto it = m_draft.find(highlight_section());
        if (it != m_draft.end()) {
            it->second.erase(kind);
            if (it->second.empty()) m_draft.erase(it);
        }
        m_notice.clear();
        rebuild();
        return;
    }
    if (const grove::Node* n = with_attribute(e.target, "data-reset-all")) {
        m_draft.erase(n->attribute_or("data-reset-all", "highlight"));
        m_notice.clear();
        rebuild();
        return;
    }
}

void SettingsPage::on_change(grove::Event& e) {
    grove::Node* target = e.target;
    if (!target || !target->is_element()) return;
    if (target->has_attribute("data-color-kind")) {
        on_color_input(e);
        return;
    }
    if (target->has_attribute("data-size-part")) {
        on_size_change(*target);
        return;
    }
    if (!target->has_attribute("data-key")) return;
    const std::string section = target->attribute_or("data-section", "");
    const std::string key = target->attribute_or("data-key", "");
    const std::string tag = target->tag();
    if (section == "keys") {
        write_shortcut(key, grove::input_string(*target).value_or(""));
        return;
    }
    const Setting* def = Settings::defaults().find(section, key);
    bool quoted = def && def->quoted;
    std::string value;
    if (tag == "input_integer") {
        value = std::to_string(static_cast<long long>(grove::input_integer(*target).value_or(0)));
        if (key == "font-size") value += "px";
        quoted = false;
    } else if (tag == "input_bool") {
        value = grove::input_bool(*target).value_or(false) ? "true" : "false";
        quoted = false;
    } else if (tag == "input_radio") {
        value = grove::radio_choice(m_doc, target->attribute_or("name", "")).value_or("");
    } else {
        value = grove::input_string(*target).value_or("");
        if (section == "toolchain" || (section == "viewer" && (key == "pdf" || key == "latex"))) {
            if (value.empty()) value = "auto";
            quoted = value != "auto";
        }
    }
    if (value.empty() && !quoted) return;
    write(section, key, bark_setting_value(value, quoted));
}

void SettingsPage::on_color_input(grove::Event& e) {
    grove::Node* target = e.target;
    if (!target || !target->is_element() || !target->has_attribute("data-color-kind")) return;
    const std::string kind = target->attribute_or("data-color-kind", "");
    const std::optional<grove::bark::Rgba> color = grove::input_color(*target);
    if (!color) return;
    HighlightLook look = current_look(kind);
    const std::string hex = hex_color(*color);
    if (look.color == hex) return;
    look.color = hex;
    m_picked = kind;
    const bool was_changed = m_draft.count(highlight_section()) && m_draft[highlight_section()].count(kind);
    write_look(kind, look);
    if (!was_changed) {
        for (const grove::Node* n = target->parent(); n; n = n->parent()) {
            if (!n->is_element() || !n->has_class("color-row")) continue;
            grove::Node* row = const_cast<grove::Node*>(n);
            grove::Node* clear = add(*row, "div", "\xC3\x97", "color-clear");
            clear->set_attribute("data-clear-kind", kind);
            clear->set_attribute("title", "Use the theme's color");
            break;
        }
    }
    show_draft();
}

void SettingsPage::on_preview_cursor(grove::Event& e) {
    if (!m_preview || e.target != m_preview) return;
    const std::string text = grove::input_string(*m_preview).value_or("");
    const std::size_t line = e.x > 0 ? static_cast<std::size_t>(e.x) - 1 : 0;
    const std::size_t column = e.y > 0 ? static_cast<std::size_t>(e.y) - 1 : 0;
    const grove::CodeLanguage language = grove::code_language_from_name(m_preview->attribute_or("language", "cpp"));
    grove::CodeLineState state;
    std::size_t start = 0;
    for (std::size_t i = 0; start <= text.size(); ++i) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string row = text.substr(start, end - start);
        const std::vector<grove::CodeToken> tokens = grove::highlight_code_line(language, row, state);
        if (i == line) {
            std::string kind = "plain";
            for (const grove::CodeToken& t : tokens) {
                if (column >= t.begin && column < t.end) kind = grove::to_string(t.kind);
            }
            m_picked = kind;
            mark(kind);
            if (grove::Node* list = m_body ? grove::query(*m_body, ".color-list") : nullptr) {
                for (const auto& child : list->children()) {
                    if (!child->is_element()) continue;
                    if (child->attribute_or("data-kind", "") == kind) grove::add_class(m_doc, *child, "lit");
                    else grove::remove_class(m_doc, *child, "lit");
                }
            }
            return;
        }
        start = end + 1;
    }
}

} // namespace canopy
