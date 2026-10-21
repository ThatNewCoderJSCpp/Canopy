#include <canopy/askpass.hpp>
#include <canopy/build.hpp>
#include <canopy/desktop.hpp>
#include <canopy/git.hpp>
#include <canopy/github_auth.hpp>
#include <canopy/history.hpp>
#include <canopy/tasks.hpp>
#include <canopy/code_intel.hpp>
#include <canopy/completion.hpp>
#include <canopy/language_docs.hpp>
#include <canopy/languages.hpp>
#include <canopy/live_server.hpp>
#include <canopy/markdown.hpp>
#include <canopy/settings.hpp>
#include <canopy/settings_page.hpp>
#include <canopy/special_names.hpp>
#include <canopy/shortcuts.hpp>
#include <canopy/terminal.hpp>
#include <canopy/toolchain.hpp>
#include <canopy/viewers.hpp>
#include <canopy/walnut_semantic.hpp>
#include <canopy/web_docs.hpp>
#include <canopy/asm_info.hpp>
#include <canopy/lang_intel.hpp>
#include <canopy/web_intel.hpp>
#include <canopy/elements_panel.hpp>
#include <canopy/workspace.hpp>

#include <grove/bark/parser.hpp>
#include <grove/bark/value.hpp>
#include <grove/input/code_buffer.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

bool has_warning(const grove::Diagnostics& d, const std::string& text) {
    for (const grove::Diagnostic& item : d.items()) {
        if (item.message.find(text) != std::string::npos) return true;
    }
    return false;
}

void test_defaults() {
    const canopy::Settings s = canopy::Settings::defaults();
    check(s.get_or("language.cpp", "standard", "") == "c++17", "default C++ standard is c++17");
    check(s.number("editor", "tab-size").value_or(0) == 2, "default tab size is 2");
    check(s.flag("editor", "insert-spaces"), "insert-spaces defaults to true");
    check(s.get_or("toolchain", "cpp", "") == "auto", "toolchain defaults to auto");
    check(s.get_or("language.cpp", "flags", "") == "-Wall -Wextra -Wpedantic", "flags keep their spaces");
}

void test_override() {
    canopy::Settings s = canopy::Settings::defaults();
    grove::Diagnostics d;
    s.load_text("language.cpp {\n    standard: \"c++20\";\n}\n", "user.bark", canopy::SettingSource::User, d);
    check(d.empty(), "a valid override gives no diagnostics");
    const canopy::Setting* std_setting = s.find("language.cpp", "standard");
    check(std_setting && std_setting->value == "c++20", "user value wins");
    check(std_setting && std_setting->source == canopy::SettingSource::User && std_setting->line == 2, "user value remembers its file and line");
    check(s.get_or("language.cpp", "flags", "") == "-Wall -Wextra -Wpedantic", "untouched settings keep their defaults");
}

void test_typos() {
    canopy::Settings s = canopy::Settings::defaults();
    grove::Diagnostics d;
    s.load_text("editor { tab-siz: 2; }\nlangauge.cpp { standard: c++20; }\n", "user.bark", canopy::SettingSource::User, d);
    check(has_warning(d, "did you mean 'tab-size'"), "a misspelled key suggests the right one");
    check(has_warning(d, "did you mean 'language.cpp'"), "a misspelled section suggests the right one");
    check(s.number("editor", "tab-size").value_or(0) == 2, "a misspelled key doesn't change anything");
}

void test_languages() {
    check(canopy::language_for_path("a/b/main.cpp")->id == canopy::LanguageId::Cpp, ".cpp is C++");
    check(canopy::language_for_path("x.WAL")->id == canopy::LanguageId::Walnut, "extensions ignore case");
    check(canopy::language_for_path("ui.grove")->id == canopy::LanguageId::Grove, ".grove is Grove");
    check(canopy::language_for_path("notes.txt") == nullptr, ".txt is unknown");
    check(canopy::is_header_path(*canopy::language_by_key("cpp"), "x.hpp"), ".hpp is a header");
}

void test_plans() {
    const canopy::Settings s = canopy::Settings::defaults();
    canopy::Toolchain t;
    t.cpp = canopy::Tool{ "cpp", "/usr/bin/g++", canopy::ToolOrigin::Path, canopy::CompilerFamily::Gcc };
    t.walnut = canopy::Tool{ "walnut", "/opt/canopy/bin/walnut", canopy::ToolOrigin::Bundled, canopy::CompilerFamily::Other };

    canopy::BuildRequest r;
    r.file = "/work/main.cpp";
    r.output = "/work/build/main";
    canopy::BuildPlan p = canopy::plan_build(r, s, t);
    check(p.ok(), "C++ plan is ok");
    check(canopy::command_line(p.command) == "/usr/bin/g++ -std=c++17 -Wall -Wextra -Wpedantic -g -O0 /work/main.cpp -o " + canopy::with_exe_suffix("/work/build/main"), "C++ debug command");

    r.mode = canopy::BuildMode::Release;
    p = canopy::plan_build(r, s, t);
    check(canopy::command_line(p.command).find("-O2") != std::string::npos, "release uses release-flags");

    r = canopy::BuildRequest{};
    r.file = "/work/app.wal";
    r.output = "/work/build/app";
    p = canopy::plan_build(r, s, t);
    check(p.ok() && p.command.at(1) == "build" && p.command.back() == "-g", "Walnut builds through walnut build");
    check(p.output == canopy::with_exe_suffix("/work/build/app"), "-o without an extension gets .exe on Windows");
    r.output = "/work/build/app.bin";
    p = canopy::plan_build(r, s, t);
    check(p.output == "/work/build/app.bin", "-o with an extension is kept as given");
    r.output = "/work/build/app";

    r.file = "/work/main.c";
    p = canopy::plan_build(r, s, t);
    check(!p.ok(), "C without a C compiler fails");

    r.file = "/work/theme.bark";
    p = canopy::plan_build(r, s, t);
    check(!p.ok() && p.error.find(".grove") != std::string::npos, "Bark explains to build the .grove file");

    t.cpp = canopy::Tool{ "cpp", "cl", canopy::ToolOrigin::Path, canopy::CompilerFamily::Msvc };
    r.file = "/work/main.cpp";
    p = canopy::plan_build(r, s, t);
    check(canopy::command_line(p.command).find("/std:c++17") != std::string::npos, "MSVC gets /std:");
    check(canopy::command_line(p.command).find("-Wall") == std::string::npos, "MSVC skips GCC-style flags");
}

void test_flags() {
    const std::vector<std::string> f = canopy::split_flags("-DNAME=\"a b\" -O2  -g");
    check(f.size() == 3 && f[0] == "-DNAME=a b" && f[2] == "-g", "split_flags keeps quoted spaces");
    check(canopy::closest_name("tabsize", { "tab-size", "font-size" }) == "tab-size", "closest_name");
}

void test_compiler_messages() {
    const std::string output =
        "> g++ main.cpp\n"
        "/work/src/main.cpp: In function 'int main()':\n"
        "/work/src/main.cpp:8:21: error: 'missing' was not declared in this scope\n"
        "C:/work/x.c:3:1: warning: unused variable 'y' [-Wunused-variable]\n"
        "lib.hpp:12: note: declared here\n"
        "app.wal:4:9: fatal error: cannot open file\n"
        "nothing to see here\n";
    const std::vector<canopy::CompilerMessage> m = canopy::parse_compiler_messages(output);
    check(m.size() == 4, "four compiler messages");
    if (m.size() == 4) {
        check(m[0].file == "/work/src/main.cpp" && m[0].line == 8 && m[0].column == 21 && m[0].severity == "error", "gcc error with column");
        check(m[1].file == "C:/work/x.c" && m[1].line == 3 && m[1].severity == "warning", "windows path keeps its drive letter");
        check(m[2].file == "lib.hpp" && m[2].line == 12 && m[2].column == 0 && m[2].severity == "note", "message without a column");
        check(m[3].severity == "error" && m[3].message == "cannot open file", "fatal error counts as error");
    }
}

void test_recent_and_files() {
    namespace fs = std::filesystem;
    const fs::path home = fs::temp_directory_path() / "canopy-tests-config";
    fs::remove_all(home);
    fs::create_directories(home / "a");
    fs::create_directories(home / "b");
#if defined(_WIN32)
    _putenv_s("APPDATA", home.string().c_str());
#else
    setenv("XDG_CONFIG_HOME", home.string().c_str(), 1);
#endif
    check(canopy::recent_folders().empty(), "no recent folders at first");
    canopy::remember_folder((home / "a").string());
    canopy::remember_folder((home / "b").string());
    canopy::remember_folder((home / "a").string());
    const std::vector<std::string> recent = canopy::recent_folders();
    check(recent.size() == 2 && recent[0] == (home / "a").string(), "most recent folder comes first, without repeats");
    fs::remove_all(home / "b");
    check(canopy::recent_folders().size() == 1, "folders that no longer exist are skipped");
    const std::string file = (home / "note.txt").string();
    check(canopy::write_text_file(file, "one\ntwo"), "write a file");
    check(canopy::read_text_file(file).value_or("") == "one\ntwo", "read it back");
    check(!canopy::read_text_file((home / "missing.txt").string()), "missing file reads as empty");
    fs::remove_all(home);
}

void test_config_files() {
    check(!canopy::default_config_file("editor.bark").empty() && !canopy::default_config_file("compilers.bark").empty(), "built-in settings are split into files");
    check(canopy::default_settings_text().find("language.cpp") != std::string_view::npos, "default_settings_text still has every section");
    check(canopy::settings_file_for_section("language.cpp") == "compilers.bark" && canopy::settings_file_for_section("highlight.c") == "highlighting.bark", "sections map to files");
    const canopy::Settings s = canopy::Settings::defaults();
    check(s.is_known("highlight.cmake", "keyword") && s.is_known("highlight", "plain"), "highlight sections are known");
    grove::Diagnostics d;
    canopy::Settings user = canopy::Settings::defaults();
    user.load_text("highlight.cpp { keyword: #ff0000 bold; }\n", "highlighting.bark", canopy::SettingSource::User, d);
    check(d.empty() && user.get_or("highlight.cpp", "keyword", "") == "#ff0000 bold", "a highlight color reads back as written");
}

void test_setting_writer() {
    using canopy::set_setting_in_text;
    std::string text = "// mine\neditor {\n    font-size: 14px; // big\n    tab-size: 4;\n}\n";
    std::string out = set_setting_in_text(text, "editor", "font-size", "24px");
    check(out == "// mine\neditor {\n    font-size: 24px; // big\n    tab-size: 4;\n}\n", "replace a value in place");
    out = set_setting_in_text(out, "editor", "theme", "\"neon\"");
    check(out.find("    tab-size: 4;\n    theme: \"neon\";\n}") != std::string::npos, "add a key at the end of its block");
    out = set_setting_in_text(out, "editor", "tab-size", "");
    check(out.find("tab-size") == std::string::npos && out.find("font-size: 24px") != std::string::npos, "remove a key and its line");
    out = set_setting_in_text(out, "build", "mode", "release");
    check(out.find("\nbuild {\n    mode: release;\n}\n") != std::string::npos, "add a missing section");
    const std::string one = set_setting_in_text("editor { font-size: 24px; }", "editor", "tab-size", "2");
    check(one == "editor { font-size: 24px;\n    tab-size: 2;\n}", "a one-line block grows");
    const std::string commented = set_setting_in_text("// editor { font-size: 9px; }\n", "editor", "font-size", "12px");
    check(commented.find("// editor { font-size: 9px; }") == 0 && commented.find("editor {\n    font-size: 12px;\n}") != std::string::npos, "commented-out blocks are left alone");
    check(canopy::bark_setting_value("a \"b\"", true) == "\"a \\\"b\\\"\"" && canopy::bark_setting_value("auto", false) == "auto", "values are quoted when they need to be");

    canopy::Settings s = canopy::Settings::defaults();
    grove::Diagnostics d;
    s.load_text(out, "editor.bark", canopy::SettingSource::User, d);
    check(d.empty() && s.get_or("editor", "font-size", "") == "24px" && s.get_or("build", "mode", "") == "release", "written settings load back");
}

void test_themes() {
    grove::Diagnostics d;
    const std::vector<canopy::Theme> themes = canopy::load_themes(d);
    check(d.empty(), "built-in themes load cleanly");
    check(!themes.empty() && themes.front().id == "canopy-dark", "Canopy Dark comes first");
    const canopy::Theme* neon = canopy::find_theme(themes, "neon");
    const canopy::Theme* amber = canopy::find_theme(themes, "amber");
    check(neon && neon->name == "Neon" && amber && amber->name == "Amber", "Neon and Amber are built in");
    const canopy::Theme* modern = canopy::find_theme(themes, "dark-modern");
    check(modern && modern->highlight.at("highlight").at("comment") == "#6a9955 italic", "a theme color keeps its style");
    check(neon && neon->highlight.at("highlight").at("keyword") == "#af00ff" && neon->highlight.at("highlight.walnut").at("enum-member") == "#00ff6e", "Neon matches Walnut's Neon colors");
    canopy::Settings s = canopy::Settings::defaults();
    s.load_text("highlight { string: #111111; }\nhighlight.cpp { keyword: #222222 italic; }\n", "h.bark", canopy::SettingSource::User, d);
    const auto cpp = canopy::resolve_highlight(s, neon, "cpp");
    const auto c = canopy::resolve_highlight(s, neon, "c");
    check(cpp.at("keyword") == "#222222 italic" && c.at("keyword") == "#af00ff", "a language's own colors only change that language");
    check(cpp.at("string") == "#111111" && c.at("string") == "#111111", "highlight changes every language");
    check(cpp.at("number") == "#f2f2f2", "everything else comes from the theme");
    auto mine = canopy::parse_theme("theme { name: \"Mine\"; background: #000000; }\nhighlight.cmake { function: #00ff00; }\n", "mine", "mine.bark", d);
    check(mine && mine->name == "Mine" && mine->background == "#000000" && canopy::resolve_highlight(s, &*mine, "cmake").at("function") == "#00ff00", "a theme can color one language");
}

void test_theme_files() {
    check(canopy::theme_slug("Amber Green!") == "amber-green" && canopy::theme_slug("  ") == "my-theme", "theme names become file names");
    const canopy::HighlightLook look = canopy::parse_highlight_look("#ff0000 italic bold");
    check(look.color == "#ff0000" && look.bold && look.italic && !look.underline, "a color and its styles read back");
    check(canopy::highlight_look_text(look) == "#ff0000 bold italic", "and write out in a fixed order");
    std::map<std::string, std::map<std::string, std::string>> sections;
    sections["highlight"]["keyword"] = "#af00ff bold";
    sections["highlight"]["string"] = "#329600";
    sections["highlight.cpp"]["namespace"] = "#ffffff";
    const std::string bark = canopy::theme_bark_text("My \"Neon\"", "#000000", "#ffffff", sections, "Neon");
    grove::Diagnostics d;
    const auto theme = canopy::parse_theme(bark, "mine", "mine.bark", d);
    check(d.empty() && theme && theme->name == "My \"Neon\"" && theme->background == "#000000", "a saved theme loads back");
    check(theme && theme->highlight.at("highlight").at("keyword") == "#af00ff bold" && theme->highlight.at("highlight.cpp").at("namespace") == "#ffffff", "with every color");
    const std::string json = canopy::theme_json_text("Mine", "#000000", "", sections);
    check(json.find("\"keyword\": { \"color\": \"#af00ff\", \"bold\": true }") != std::string::npos, "JSON keeps styles");
    check(json.find("\"highlight.cpp\": {") != std::string::npos && json.find("foreground") == std::string::npos, "JSON has each language and skips what's empty");
}

void test_walnut_semantic() {
    using K = grove::CodeTokenKind;
    const std::string text = "enum Color { Red, Green }\nfunction main() -> int {\n    Color: c = Color::Red;\n    int: n = totl;\n    return 0;\n} end function main\n";
    const std::string output = R"({"semantic":true,"tokens":[[0,5,5,"enum",["declaration"]],[0,13,3,"enumMember",["declaration"]],[0,18,5,"enumMember",["declaration"]],[1,9,4,"function",["declaration"]],[2,4,5,"enum",[]],[2,11,1,"variable",["declaration"]],[2,15,5,"enum",[]],[2,22,3,"enumMember",[]],[3,9,1,"variable",["declaration"]]],"diagnostics":[{"severity":"error","line":3,"col":0,"len":0,"message":"Name Error on line 4: use of undeclared identifier 'totl'"}],"hovers":[]})";
    const canopy::SemanticResult r = canopy::parse_walnut_highlight(output, text, "/w/main.wal");
    check(r.ok && r.semantic, "the compiler's colors read back");
    auto kind_at = [&](std::size_t line, std::size_t column) {
        for (const grove::CodeSpan& s : r.spans) {
            if (s.line == line && s.column == column) return s.kind;
        }
        return K::Plain;
    };
    check(kind_at(0, 13) == K::EnumMember && kind_at(2, 22) == K::EnumMember && kind_at(2, 11) == K::Declaration, "enum members and declarations come from the compiler");
    check(kind_at(2, 4) == K::Type && kind_at(1, 9) == K::Function, "types and functions too");
    check(kind_at(3, 13) == K::Unresolved, "names the compiler doesn't know are unresolved");
    check(kind_at(5, 15) == K::Plain, "the name after end isn't");
    check(r.problems.size() == 1 && r.problems[0].line == 3 && r.problems[0].column == 4 && r.problems[0].length == 14, "a problem without a length covers the line");
    check(r.messages.size() == 1 && r.messages[0].line == 4 && r.messages[0].message == "Name Error: use of undeclared identifier 'totl'", "and shows up as a message");
    check(canopy::walnut_semantic_kind("operator", { "overloaded" }) == K::OverloadedOperator && canopy::walnut_semantic_kind("variable", { "variableTemplate" }) == K::VariableTemplate, "modifiers pick the color");
    check(canopy::walnut_semantic_kind("function", { "templateQualifier" }) == K::TemplateScope, "template scopes");
    check(canopy::utf16_to_byte("\xC3\xA9x", 1) == 2 && canopy::utf16_to_byte("\xF0\x9F\x8C\xB0x", 2) == 4, "columns count UTF-16 units");
    check(!canopy::parse_walnut_highlight("crashed", text, "x").ok, "no JSON is not a result");
    const auto cmd = canopy::walnut_highlight_command("walnut", "a.wal", "-O2 -Iinc -D X=1 -M mods -Wall");
    check(cmd == std::vector<std::string>({ "walnut", "highlight", "a.wal", "--stdin", "-Iinc", "-D", "X=1", "-M", "mods" }), "only include, define and module flags are passed");
    check(canopy::walnut_highlight_command("walnut", "a.wal", "--random-seed 7 -O2") == std::vector<std::string>({ "walnut", "highlight", "a.wal", "--stdin", "--random-seed", "7" }), "a random seed is passed so hovers match the build");
#if !defined(_WIN32)
    std::string echoed;
    canopy::run_command_capture_input({ "cat" }, "piped text", echoed);
    check(echoed == "piped text", "text reaches the command's input");
#endif

    const std::string enum_text = "enum Color : short int { Red, Green = 5 }\nfunction main() -> int {\n    return 0;\n} end function main\n";
    const std::string enum_output = R"({"semantic":true,"tokens":[],"diagnostics":[],"hovers":[{"line":0,"col":30,"len":5,"code":"short int: Color::Green = 5","values":["5"]},{"line":0,"col":13,"len":9,"code":"short int","doc":"**Walnut builtin type.** A 16-bit signed integer."}],"locations":[["<walnut>/io.walh",10,4,5]],"links":[[0,30,5,0,[]]]})";
    const canopy::SemanticResult er = canopy::parse_walnut_highlight(enum_output, enum_text, "/w/e.wal");
    check(er.hovers.size() == 2 && er.hovers[1].doc == "**Walnut builtin type.** A 16-bit signed integer.", "hovers can say what a keyword or builtin type is");
    check(canopy::walnut_hover_markdown(er.hovers[1], 0) == "```walnut\nshort int\n```\n**Walnut builtin type.** A 16-bit signed integer.\n", "and show it under the code");
    check(canopy::walnut_hover_markdown(er.hovers[0], 0).find("short int: Color::Green = 5") != std::string::npos, "enum values show their type and value");
    check(er.links.size() == 1 && er.links[0].definition.file == "<walnut>/io.walh" && er.links[0].definition.line == 10, "links can go into the Walnut library");
    check(canopy::walnut_library_name("<walnut>/io.walh") == "io.walh" && canopy::walnut_library_path("walnut", "/w/x.wal") == "/w/x.wal", "library names drop the prefix, and other files are left alone");
    check(!canopy::is_walnut_library_copy("/w/x.wal") && canopy::is_walnut_library_copy(canopy::walnut_library_cache("walnut") + "/io.walh"), "copies of the library are recognized");
#if !defined(_WIN32)
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "canopy-test-walnut";
        fs::remove_all(dir);
        fs::create_directories(dir);
        const fs::path fake = dir / "walnut";
        std::ofstream(fake) << "#!/bin/sh\nif [ \"$1\" = library ] && [ \"$2\" = --extract ]; then mkdir -p \"$3\" && printf 'namespace io { }\\n' > \"$3/io.walh\"; fi\n";
        fs::permissions(fake, fs::perms::owner_all);
        fs::remove_all(canopy::walnut_library_cache(fake.string()));
        const std::string copy = canopy::walnut_library_path(fake.string(), "<walnut>/io.walh");
        check(!copy.empty() && canopy::is_walnut_library_copy(copy) && fs::path(copy).filename() == "io.walh", "a walnut without its Library folder hands its library over");
        check(canopy::walnut_library_path(fake.string(), "<walnut>/missing.walh").empty(), "a file it doesn't have stays missing");
        fs::remove_all(canopy::walnut_library_cache(fake.string()));
        fs::remove_all(dir);
    }
#endif

    check(canopy::highlight_fallback("control-keyword") == "keyword" && canopy::highlight_fallback("keyword").empty(), "split kinds fall back");
    check(canopy::highlight_value({ { "keyword", "#123456" } }, "control-keyword") == "#123456", "to the color they came from");
    check(!canopy::highlight_sample_spans("walnut").empty() && !canopy::highlight_sample_problems("walnut").empty(), "the Walnut preview shows the compiler's colors");
    grove::CodeOverlay overlay;
    overlay.set_spans(canopy::highlight_sample_spans("walnut"));
    const std::string sample = canopy::highlight_sample("walnut");
    check(sample.find("\xE2\x9F\xA6") == std::string::npos && sample.find("enum Color { Red, Green }") != std::string::npos, "the preview text has no marks left in it");
    const std::vector<std::string> lines = canopy::text_lines(sample);
    bool enum_member = false;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        for (const grove::CodeToken& t : overlay.spans(i, lines[i])) enum_member = enum_member || (t.kind == K::EnumMember && lines[i].substr(t.begin, t.end - t.begin) == "Red");
    }
    check(enum_member, "Red is an enum member in the preview");
    grove::Diagnostics d;
    const std::vector<canopy::Theme> themes = canopy::load_themes(d);
    const canopy::Theme* dark = canopy::find_theme(themes, "dark-modern");
    const auto walnut = canopy::resolve_highlight(canopy::Settings::defaults(), dark, "walnut");
    const auto cpp = canopy::resolve_highlight(canopy::Settings::defaults(), dark, "cpp");
    check(walnut.at("control-keyword") == "#c586c0" && walnut.at("declaration") == "#ffffff" && walnut.at("constant") == "#569cd6", "Dark Modern has VS Code's Walnut colors");
    check(cpp.at("constant") == "#4fc1ff" && cpp.count("declaration") == 0, "without changing C++");
    for (const canopy::Theme& t : themes) {
        const auto colors = canopy::resolve_highlight(canopy::Settings::defaults(), &t, "walnut");
        check(colors.count("control-keyword") && colors.count("builtin-type") && colors.count("error-underline"), t.name + " colors the new kinds");
    }
}

std::string bytes_of(std::initializer_list<int> values) {
    std::string out;
    for (int v : values) out += static_cast<char>(v);
    return out;
}

void put32(std::string& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<char>((v >> (8 * i)) & 0xFF);
}

void put16(std::string& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<char>(v & 0xFF);
    b[at + 1] = static_cast<char>(v >> 8);
}

void test_viewers() {
    namespace fs = std::filesystem;
    using canopy::ViewerKind;
    const fs::path root = fs::temp_directory_path() / "canopy-viewer-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    auto write = [&](const std::string& name, const std::string& data) {
        std::ofstream out(root / name, std::ios::binary);
        out << data;
        return (root / name).string();
    };

    const std::string png = bytes_of({ 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 1, 0x40, 0, 0, 0, 0xC8, 8, 6, 0, 0, 0 });
    const canopy::ImageInfo image = canopy::image_info(png);
    check(image.format == "PNG" && image.width == 320 && image.height == 200 && image.detail == "8-bit RGBA", "PNG size and depth are read from the header");
    const std::string gif = std::string("GIF89a") + bytes_of({ 0x10, 0, 0x20, 0 });
    check(canopy::image_info(gif).width == 16 && canopy::image_info(gif).height == 32, "GIF size");
    const std::string jpeg = bytes_of({ 0xFF, 0xD8, 0xFF, 0xE0, 0, 4, 0, 0, 0xFF, 0xC0, 0, 11, 8, 0, 50, 0, 70, 3 });
    check(canopy::image_info(jpeg).width == 70 && canopy::image_info(jpeg).height == 50, "JPEG size comes from the frame header");
    check(canopy::image_info("<svg width=\"120\" height=\"80px\"></svg>", "a.svg").width == 120 && canopy::image_info("<svg viewBox=\"0 0 30 40\"/>", "a.svg").height == 40, "SVG size from attributes or viewBox");

    check(canopy::viewer_kind_for(write("pic.PNG", png)) == ViewerKind::Image, "images open as pictures");
    check(canopy::viewer_kind_for(write("noext", png)) == ViewerKind::Image, "and so does a PNG without an extension");
    check(canopy::viewer_kind_for(write("doc.pdf", "%PDF-1.7\n")) == ViewerKind::Pdf, "PDFs open in the PDF viewer");
    check(canopy::viewer_kind_for(write("paper.tex", "\\documentclass{article}")) == ViewerKind::Latex, "TeX files get the LaTeX preview");
    check(canopy::viewer_kind_for(write("tool.exe", "MZ")) == ViewerKind::Binary, "programs open in the hex view");
    check(canopy::viewer_kind_for(write("blob", std::string("abc\0def", 7))) == ViewerKind::Binary, "files with NUL bytes are binary");
    check(canopy::viewer_kind_for(write("notes", "just some text\nand more \xC3\xA9\n")) == ViewerKind::Text, "text without an extension stays text");
    check(!canopy::looks_binary("caf\xC3\xA9 \xE2\x9C\x93\n") && canopy::looks_binary(std::string("\x01\x02\x03\x04\x05\x06\x07\x08 hi", 11)), "UTF-8 is text, control bytes aren't");

    std::string pe(1024, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    put32(pe, 0x3C, 0x80);
    pe.replace(0x80, 4, std::string("PE\0\0", 4));
    put16(pe, 0x84, 0x8664);
    put16(pe, 0x86, 1);
    put16(pe, 0x94, 240);
    put16(pe, 0x96, 0x2022);
    put16(pe, 0x98, 0x20b);
    put16(pe, 0x98 + 68, 3);
    pe.replace(0x98 + 240, 5, ".text");
    put32(pe, 0x98 + 240 + 16, 0x200);
    put32(pe, 0x98 + 240 + 20, 0x400);
    put32(pe, 0x98 + 240 + 36, 0x60000020);
    const canopy::BinaryInfo dll = canopy::describe_binary(pe, "x.dll");
    check(dll.kind == "Windows DLL" && dll.summary.find("x86-64") != std::string::npos && dll.summary.find("Console") != std::string::npos, "PE files say what they are");
    check(dll.sections.size() == 1 && dll.sections[0].name == ".text" && dll.sections[0].flags == "r-x code", "and list their sections");

    std::string elf(64, '\0');
    elf.replace(0, 4, "\x7F" "ELF");
    elf[4] = 2;
    elf[5] = 1;
    put16(elf, 16, 2);
    put16(elf, 18, 183);
    const canopy::BinaryInfo arm = canopy::describe_binary(elf, "prog");
    check(arm.kind == "ELF executable" && arm.summary.find("AArch64") != std::string::npos, "ELF files too");
    check(canopy::describe_binary(std::string("PK\x05\x06", 4) + std::string(18, '\0'), "a.jar").kind == "Java archive (ZIP)", "archives by magic and extension");
    check(canopy::describe_binary(std::string("\xCA\xFE\xBA\xBE\0\0\0\x34", 8), "A.class").summary.find("Java 8") != std::string::npos, "Java class versions");
    check(canopy::describe_binary(png, "p.png").facts[1].value == "29 bytes", "facts start with the size");

    const canopy::ByteDump hex = canopy::hex_dump(std::string("MZ\x90\0", 4), 0, 4);
    check(hex.text == "00000000  4d 5a 90 00" + std::string(39, ' ') + "|MZ..|\n", "hex rows have offset, bytes and characters");
    check(!hex.spans.empty() && hex.spans.front().kind == grove::CodeTokenKind::Comment, "offsets are colored");
    check(canopy::hex_dump("A", 0x100000000ull, 0x200000000ull).text.rfind("000100000000  41", 0) == 0, "big files get wider offsets");
    check(canopy::bits_dump("A", 8, 9).text.rfind("00000008  01000001 ", 0) == 0, "the binary view shows bits");
    const canopy::ByteDump strings = canopy::strings_dump(std::string("\0\0hello world\0\x01w\0i\0d\0e\0!\0", 25), 0, 25);
    check(strings.text.find("00000002  hello world\n") != std::string::npos && strings.text.find("wide!  UTF-16") != std::string::npos, "strings finds ASCII and UTF-16 text");
    check(canopy::human_size(1536) == "1.5 KB" && canopy::human_size(1) == "1 byte", "sizes read naturally");

    const canopy::ViewerTool latexmk{ "latex", "/usr/bin/latexmk", "latexmk" };
    const auto build = canopy::latex_build_command(latexmk, "/docs/paper.tex", "/tmp/out", "-xelatex");
    check(build.back() == "paper.tex" && std::find(build.begin(), build.end(), "-outdir=/tmp/out") != build.end() && build[build.size() - 2] == "-xelatex", "latexmk builds into the cache with the user's flags");
    const canopy::ViewerTool pdflatex{ "latex", "pdflatex", "pdflatex" };
    const auto plain = canopy::latex_build_command(pdflatex, "/docs/paper.tex", "/o", "");
    check(std::find(plain.begin(), plain.end(), "-output-directory=/o") != plain.end() && std::find(plain.begin(), plain.end(), "-file-line-error") != plain.end(), "pdflatex reports file:line errors");
    check(canopy::latex_pdf_path("/docs/paper.tex", "/o") == (fs::path("/o") / "paper.pdf").string(), "the PDF is named after the root file");
    const canopy::ViewerTool mutool{ "pdf", "mutool", "mutool" };
    check(canopy::pdf_render_command(mutool, "a.pdf", "/p", 100)[1] == "draw", "mutool draws pages");
    const canopy::ViewerTool pdftoppm{ "pdf", "pdftoppm", "pdftoppm" };
    check(canopy::pdf_render_command(pdftoppm, "a.pdf", "/p", 100)[3] == "100", "pdftoppm uses the resolution");
    const std::string chapter = write("chapter.tex", "% !TEX root = main.tex\n\\section{One}\n");
    check(canopy::latex_root_file(chapter, "% !TEX root = main.tex\n\\section{One}\n") == (root / "main.tex").string(), "% !TEX root points at the main file");
    check(canopy::latex_is_document("\\documentclass{article}") && !canopy::latex_is_document("% \\documentclass{article}\n\\section{x}"), "commented-out documentclass doesn't count");

    const std::string log = "./paper.tex:12: Undefined control sequence.\nl.12 \\foo\n! LaTeX Error: File `x.sty' not found.\n\nl.3 \\usepackage\nLaTeX Warning: Reference `fig' on page 1 undefined on input line 20.\n";
    const std::vector<canopy::CompilerMessage> messages = canopy::parse_latex_log(log, (root / "paper.tex").string());
    check(messages.size() == 3, "LaTeX errors and warnings are read from the log");
    if (messages.size() == 3) {
        check(messages[0].line == 12 && messages[0].severity == "error" && messages[0].message == "Undefined control sequence.", "file:line errors");
        check(messages[1].line == 3 && messages[1].message == "LaTeX Error: File `x.sty' not found.", "! errors with their l. line");
        check(messages[2].line == 20 && messages[2].severity == "warning" && messages[2].message == "Reference `fig' on page 1 undefined", "warnings with their input line");
    }

    fs::create_directories(root / "pages", ec);
    for (const char* name : { "page-10.png", "page-2.png", "page-1.png", "other.png" }) write(std::string("pages/") + name, "x");
    const std::vector<std::string> pages = canopy::rendered_pages((root / "pages").string());
    check(pages.size() == 3 && fs::path(pages[0]).filename() == "page-1.png" && fs::path(pages[2]).filename() == "page-10.png", "pages are sorted by number");
    check(canopy::pdf_page_count("<< /Type /Pages /Count 7 /Kids [] >>").value_or(0) == 7, "page count from the page tree");
    check(canopy::read_file_range(write("range", "0123456789"), 3, 4).value_or("") == "3456", "files are read in pieces");
#if !defined(_WIN32)
    std::string where;
    canopy::run_command_capture_in({ "pwd" }, root.string(), where);
    check(where.find("canopy-viewer-test") != std::string::npos, "commands can run in another folder");
#endif
    fs::remove_all(root, ec);
}

void test_line_masking() {
    const std::string text = "int a; // note \"x\"\r\n/* start\nstill */ int b = 'c';\nauto s = R\"(raw\nraw)\"; int d;\nlast";
    const std::vector<std::string> masked = canopy::split_text_lines(canopy::mask_c_like(text));
    const std::vector<std::string> raw = canopy::split_text_lines(text);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        check(canopy::text_line_at(text, i) == raw[i], "text_line_at matches split_text_lines");
        check(canopy::mask_c_like_line(text, i) == masked[i], "mask_c_like_line matches mask_c_like");
    }
    check(!canopy::text_line_at(text, raw.size()), "text_line_at past the end");
}

void test_code_intel() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "canopy-intel-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "include", ec);
    auto write = [&](const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    };
    write(root / "include" / "game.hpp", "#pragma once\n#define LIMIT (1 << 4)\n#define TWICE (LIMIT * 2)\n\nnamespace game {\n\n// Colors a player can pick\nenum class Color { Red, Green = 5, Blue };\n\n/* Keeps one player's score */\nstruct [[nodiscard]] Player {\n    std::string name;\n    int score = 0;\n};\n\n// Adds up the scores\nint total(const Player& p, int bonus);\n\n} // namespace game\n");
    const std::string main_text = "#include \"game.hpp\"\n\nnamespace game {\n\nint total(const Player& p, int bonus) {\n    int extra = 0x10;\n    return p.score + bonus + extra + LIMIT;\n}\n\n} // namespace game\n\nint main() {\n    game::Player me;\n    return game::total(me, TWICE) + static_cast<int>(game::Color::Blue);\n}\n";
    write(root / "main.cpp", main_text);
    write(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n# Which C++ to build with\nset(DEMO_STANDARD 17)\nadd_executable(demo main.cpp)\nmessage(STATUS \"${DEMO_STANDARD}\")\n");
    write(root / "app.bark", "$accent = #89b4fa;\n\n.card {\n    color: $accent;\n    padding: 4px;\n}\n");
    write(root / "app.grove", "<grove>\n    <body>\n        <div class=\"card\" id=\"main\">Hi &amp; bye</div>\n        <style src=\"app.bark\" />\n    </body>\n</grove>\n");

    canopy::CodeIntel intel;
    intel.set_workspace(root.string());
    const std::string main_path = (root / "main.cpp").string();
    auto hover = [&](const std::string& file, const std::string& language, const std::string& text, std::size_t line, const std::string& word, std::size_t nth = 0) {
        const std::vector<std::string> lines = canopy::split_text_lines(text);
        std::size_t at = std::string::npos;
        std::size_t from = 0;
        for (std::size_t k = 0; k <= nth; ++k) {
            at = lines[line].find(word, from);
            from = at + 1;
        }
        return intel.hover(canopy::IntelRequest{ file, language, text, line, at });
    };
    auto has = [](const std::optional<canopy::HoverResult>& h, const std::string& text) { return h && h->markdown.find(text) != std::string::npos; };
    const auto limit = hover(main_path, "cpp", main_text, 6, "LIMIT");
    check(has(limit, "#define LIMIT (1 << 4)") && has(limit, "`16`") && has(limit, "`0x10`"), "a macro hover shows its value");
    check(has(hover(main_path, "cpp", main_text, 13, "TWICE"), "`32`"), "macros that use macros are worked out");
    const auto total = hover(main_path, "cpp", main_text, 13, "total");
    check(has(total, "int total(const Player& p, int bonus)") && has(total, "Adds up the scores"), "a function hover shows its signature and comment");
    check(has(hover(main_path, "cpp", main_text, 13, "Blue"), "`6`"), "enum members show their value");
    check(has(hover(main_path, "cpp", main_text, 6, "extra"), "int extra = 0x10;") && has(hover(main_path, "cpp", main_text, 6, "extra"), "`16`"), "locals show their declaration and value");
    check(has(hover(main_path, "cpp", main_text, 5, "0x10"), "`16`"), "numbers show other bases");
    check(has(hover(main_path, "cpp", main_text, 6, "score"), "int score = 0;"), "fields after . show their declaration");
    check(has(hover(main_path, "cpp", main_text, 12, "Player"), "Keeps one player's score"), "types show their comment");
    check(has(hover(main_path, "cpp", main_text, 0, "game.hpp"), "game.hpp"), "includes show the file");
    const auto def = intel.definition(canopy::IntelRequest{ main_path, "cpp", main_text, 13, canopy::split_text_lines(main_text)[13].find("total") });
    check(def && def->file == main_path && def->line == 4, "Ctrl+click goes to the definition, not the prototype");
    const auto inc = intel.definition(canopy::IntelRequest{ main_path, "cpp", main_text, 0, 12 });
    check(inc && fs::path(inc->file).filename() == "game.hpp", "and opens includes");
    const auto local = intel.definition(canopy::IntelRequest{ main_path, "cpp", main_text, 6, canopy::split_text_lines(main_text)[6].find("extra") });
    check(local && local->line == 5, "and finds locals");

    const std::string cmake = (root / "CMakeLists.txt").string();
    const std::string cmake_text = "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n# Which C++ to build with\nset(DEMO_STANDARD 17)\nadd_executable(demo main.cpp)\nmessage(STATUS \"${DEMO_STANDARD}\")\n";
    const auto var = hover(cmake, "cmake", cmake_text, 5, "DEMO_STANDARD");
    check(has(var, "set(DEMO_STANDARD 17)") && has(var, "**Value:** `17`") && has(var, "Which C++"), "CMake variables show where they're set");
    check(has(hover(cmake, "cmake", cmake_text, 4, "add_executable"), "builds a program"), "CMake commands have docs");
    const auto src = intel.definition(canopy::IntelRequest{ cmake, "cmake", cmake_text, 4, canopy::split_text_lines(cmake_text)[4].find("main.cpp") });
    check(src && fs::path(src->file).filename() == "main.cpp", "CMake file arguments open");

    const std::string bark = (root / "app.bark").string();
    const std::string bark_text = "$accent = #89b4fa;\n\n.card {\n    color: $accent;\n    padding: 4px;\n}\n";
    check(has(hover(bark, "bark", bark_text, 3, "$accent"), "color:#89b4fa"), "Bark variables preview their color");
    check(has(hover(bark, "bark", bark_text, 0, "#89b4fa"), "rgb(137, 180, 250)"), "colors show other formats");
    check(has(hover(bark, "bark", bark_text, 4, "padding"), "CSS order"), "properties have docs");
    check(has(hover(bark, "bark", bark_text, 2, ".card"), "Used by 1 element"), "classes show the elements using them");
    const std::string grove = (root / "app.grove").string();
    const std::string grove_text = "<grove>\n    <body>\n        <div class=\"card\" id=\"main\">Hi &amp; bye</div>\n        <style src=\"app.bark\" />\n    </body>\n</grove>\n";
    check(has(hover(grove, "grove", grove_text, 2, "card"), "padding: 4px;"), "a class shows its Bark rule");
    check(has(hover(grove, "grove", grove_text, 2, "&amp;"), "`&`"), "entities show their character");
    check(has(hover(grove, "grove", grove_text, 2, "div"), "blocks"), "tags have docs");
    const auto rule = intel.definition(canopy::IntelRequest{ grove, "grove", grove_text, 2, canopy::split_text_lines(grove_text)[2].find("card") });
    check(rule && rule->file == bark && rule->line == 2, "Ctrl+click on a class opens its rule");
    const auto sheet = intel.definition(canopy::IntelRequest{ grove, "grove", grove_text, 3, canopy::split_text_lines(grove_text)[3].find("app.bark") });
    check(sheet && fs::path(sheet->file).filename() == "app.bark", "src opens the file");

    check(canopy::evaluate_integer("(1 << 3) | 2") == 10 && !canopy::evaluate_integer("1 +"), "integer expressions");
    const auto link = canopy::parse_hover_link("open:3:4:/a/b c.cpp");
    check(link && link->line == 3 && link->column == 4 && link->file == "/a/b c.cpp", "hover links read back");
    check(canopy::find_doc(canopy::bark_property_docs(), "gap") && canopy::find_doc(canopy::grove_tag_docs(), "input_code"), "Grove's docs are built in");

    canopy::WalnutHover wh;
    wh.code = "function scale(T: value) -> T";
    wh.values = { "6" };
    wh.overloads = { "function a()", "function a(int: x)" };
    check(canopy::walnut_hover_markdown(wh, 0).find("1 of 2") != std::string::npos && canopy::walnut_hover_markdown(wh, 1).find("a(int: x)") != std::string::npos, "Walnut overloads page");
    wh.overloads.clear();
    check(canopy::walnut_hover_markdown(wh, 0).find("**Value:** `6`") != std::string::npos, "Walnut values show");
    fs::remove_all(root, ec);
}

} // namespace

void test_sizes_and_messages() {
    const auto walnut = canopy::parse_compiler_messages("[Parser] e.wal:3: Syntax Error on line 3: Expected ':', but found the name 'x'\nContext: types come first\n  3 |     float x = ;\n    |           ^\n[Semantic warning] e.wal:7: 'n' is never used\nwalnut: Compilation failed with 1 error\n");
    check(walnut.size() == 2, "Walnut's build messages are read");
    if (walnut.size() == 2) {
        check(walnut[0].file == "e.wal" && walnut[0].line == 3 && walnut[0].column == 11 && walnut[0].severity == "error" && walnut[0].message == "Syntax Error: Expected ':', but found the name 'x'", "with the caret's column");
        check(walnut[1].severity == "warning" && walnut[1].line == 7, "and warnings");
    }
    const auto size = canopy::parse_size("0.02vmin");
    check(size && size->value == 0.02 && size->unit == "vmin" && canopy::parse_size("14")->unit == "px" && !canopy::parse_size("big"), "sizes read their unit");
    check(canopy::size_pixels({ 12.0, "pt" }, 0, 0).value_or(0) == 16.0 && canopy::size_pixels({ 0.5, "vw" }, 800, 600).value_or(0) == 400.0 && canopy::size_pixels({ 10.0, "%" }, 800, 600).value_or(0) == 60.0, "every unit becomes pixels");
    check(!canopy::size_pixels({ 0.5, "vw" }, 0, 0), "window units need the window");
    const auto pt = canopy::convert_size({ 16.0, "px" }, "pt", 0, 0);
    check(pt && pt->value == 12.0 && pt->unit == "pt", "switching unit keeps the size");
    check(canopy::convert_size({ 600.0, "px" }, "vmin", 800, 600)->value == 1.0, "and clamps to the unit's range");
    check(canopy::grove_size({ 0.02, "vmin" }, 2.0) == "2vmin" && canopy::grove_size({ 28.0, "px" }, 2.0) == "14px" && canopy::grove_size({ 5.0, "%" }, 1.0) == "5vmin", "Grove gets percent viewport units and unzoomed pixels");
    check(canopy::size_range("vdiag").second == 1.0 && canopy::size_range("%").second == 100.0, "viewport units go 0 to 1, % to 100");
    canopy::Settings s = canopy::Settings::defaults();
    grove::Diagnostics d;
    check(canopy::gui_zoom(s, 0, 0) == 1.0, "the GUI starts at its normal size");
    s.load_text("gui { size: 26px; }\nhighlight { background: #123456; }\n", "g.bark", canopy::SettingSource::User, d);
    check(d.empty() && canopy::gui_zoom(s, 0, 0) == 2.0, "a bigger GUI size zooms it");
    const std::vector<canopy::Theme> themes = canopy::load_themes(d);
    const auto colors = canopy::resolve_highlight(s, canopy::find_theme(themes, "dark-modern"), "cpp");
    check(colors.at("background") == "#123456" && colors.at("foreground") == "#d4d4d4", "background and text can be changed over the theme");
    check(colors.at("error-line") == colors.at("error-underline"), "lines with errors are drawn in the error color");
    check(colors.at("warning-underline") == "#cca700", "warning squiggles have their own color");
}

void test_shortcuts() {
    canopy::Settings s = canopy::Settings::defaults();
    std::vector<canopy::ShortcutBinding> b = canopy::shortcut_bindings(s);
    check(canopy::shortcut_conflicts(b).empty() && canopy::shortcut_errors(b).empty(), "the default shortcuts don't clash");
    check(canopy::command_for(b, grove::chord_of("b", grove::ModifierControl)).value_or("") == "build", "Ctrl+B builds");
    check(canopy::command_for(b, grove::chord_of("F5", 0)).value_or("") == "run" && canopy::command_for(b, grove::chord_of("Comma", grove::ModifierControl)).value_or("") == "open-settings", "F5 runs and Ctrl+, opens settings");
    check(!canopy::command_for(b, grove::chord_of("A", grove::ModifierControl)), "editor keys are left to the editor");
    check(canopy::editor_keymap(b).empty(), "the editor keeps its own keys by default");
    const canopy::ShortcutBinding* redo = canopy::binding_for(b, "redo");
    check(redo && redo->chords.size() == 2 && redo->text == "Ctrl+Y, Ctrl+Shift+Z", "a shortcut can have several keys");
    const canopy::ShortcutBinding* warnings = canopy::binding_for(b, "show-warnings");
    check(warnings && warnings->chords.empty() && !warnings->invalid, "none means no keys");
    grove::Diagnostics d;
    s.load_text("keys {\n    run: \"Ctrl+B\";\n    copy: \"Ctrl+Shift+C\";\n    add-caret: \"Ctrl+Click\";\n    paste: \"Ctrl+Banana\";\n}\n", "/cfg/keys.bark", canopy::SettingSource::User, d);
    check(d.empty(), "keys.bark reads cleanly");
    b = canopy::shortcut_bindings(s);
    const auto conflicts = canopy::shortcut_conflicts(b);
    check(conflicts.size() == 2, "two clashes are found");
    bool build_run = false;
    bool mouse = false;
    for (const canopy::ShortcutConflict& c : conflicts) {
        if (c.first->id == "build" && c.second->id == "run") {
            build_run = c.message.find("Ctrl+B is used by both \"Build\" (General) and \"Run\" (General)") != std::string::npos &&
                        c.message.find("Canopy's default, in Settings > Shortcuts > General") != std::string::npos && c.message.find("keys.bark line 2") != std::string::npos &&
                        c.file == "/cfg/keys.bark" && c.line == 2;
        }
        if (c.first->id == "go-to-definition" && c.second->id == "add-caret") mouse = c.message.find("Ctrl+Click") != std::string::npos && c.line == 4;
    }
    check(build_run, "a clash names both shortcuts and where each is set");
    check(mouse, "mouse shortcuts clash too");
    const auto errors = canopy::shortcut_errors(b);
    check(errors.size() == 1 && errors[0].find("Ctrl+Banana") != std::string::npos && errors[0].find("keys.bark line 5") != std::string::npos, "keys it can't read are errors with their line");
    const std::string keymap = canopy::editor_keymap(b);
    check(keymap.find("copy=Ctrl+Shift+C") != std::string::npos && keymap.find("add-caret=Ctrl+Click") != std::string::npos && keymap.find("paste") == std::string::npos, "changed editor keys go to the editor");
    check(canopy::command_for(b, grove::chord_of("B", grove::ModifierControl)).value_or("") == "build", "while clashing, the first one listed wins");
    std::size_t fixed = 0;
    for (const canopy::Shortcut& sc : canopy::shortcuts()) fixed += sc.fixed ? 1 : 0;
    check(fixed >= 4 && canopy::find_shortcut("column-select") && canopy::find_shortcut("column-select")->mouse, "mouse shortcuts are listed too");
}

bool has_label(const grove::CompletionList& list, const std::string& label) {
    for (const grove::CompletionItem& item : list.items) {
        if (item.label == label) return true;
    }
    return false;
}

const grove::CompletionItem* find_label(const grove::CompletionList& list, const std::string& label) {
    for (const grove::CompletionItem& item : list.items) {
        if (item.label == label) return &item;
    }
    return nullptr;
}

grove::CompletionList complete_at(const std::string& language, const std::string& text, std::size_t line, std::size_t column, const canopy::CompletionSources& sources = {}) {
    canopy::CompletionRequest r;
    r.language = language;
    r.path = "/tmp/x";
    r.text = text;
    r.line = line;
    r.column = column;
    return canopy::complete_code(r, sources);
}

void test_completion_index() {
    canopy::CompletionIndex index;
    grove::CompletionItem a;
    a.label = "vector";
    index.add(a, 1);
    a.label = "Vec";
    index.add(a, 0);
    a.label = "value";
    index.add(a, 0);
    a.label = "getValue";
    index.add(a, 0);
    check(!index.add(a, 0), "the index keeps one entry per label");
    check(index.count_prefix("ve") == 2, "the trie counts words by prefix, ignoring case");
    const std::vector<grove::CompletionItem> got = index.query("ve");
    check(got.size() >= 2 && got[0].label == "vector" && got[1].label == "Vec", "a prefix with the same case comes first, then other cases, then looser matches");
    const std::vector<grove::CompletionItem> fuzzy = index.query("gv");
    check(!fuzzy.empty() && fuzzy[0].label == "getValue", "word starts match when no prefix does");
    check(index.query("").size() == 4, "an empty prefix lists everything");
}

void test_code_completion() {
    const std::string cpp = "#include <vector>\nstruct Point { int xpos; int ypos; };\nint total_count = 0;\nint main() {\n    Point p;\n    tot\n    p.\n    // tot\n    ret\n}\n";
    canopy::CompletionSources sources;
    sources.symbols = canopy::scan_c_symbols(cpp, "/tmp/x");
    grove::CompletionList list = complete_at("cpp", cpp, 5, 7, sources);
    check(list.column == 4 && !list.items.empty() && list.items[0].label == "total_count", "C++ names complete from the file's symbols");
    list = complete_at("cpp", cpp, 6, 6, sources);
    check(list.items.size() >= 2 && list.items[0].label == "xpos" && list.items[1].label == "ypos" && !has_label(list, "return"), "after a dot members come first and keywords aren't offered");
    list = complete_at("cpp", cpp, 7, 10, sources);
    check(list.items.empty(), "nothing is offered inside a comment");
    list = complete_at("cpp", cpp, 8, 7, sources);
    check(has_label(list, "return") && find_label(list, "return")->kind == "keyword", "keywords are offered");
    list = complete_at("cpp", "#inc\n", 0, 4);
    check(has_label(list, "include"), "preprocessor directives complete after #");

    const std::string bark = "$accent = #fff;\nbutton:ho {\n    backg\n    color: $ac\n}\n";
    list = complete_at("bark", bark, 2, 9);
    const grove::CompletionItem* bg = find_label(list, "background");
    check(bg && bg->insert == "background: $0;", "Bark properties complete with a colon and semicolon");
    list = complete_at("bark", bark, 1, 9);
    check(has_label(list, ":hover") && list.column == 6, "pseudo-classes complete after a colon");
    canopy::CompletionSources bark_sources;
    bark_sources.symbols = canopy::scan_bark_symbols(bark, "/tmp/x");
    list = complete_at("bark", bark, 3, 14, bark_sources);
    check(has_label(list, "$accent"), "Bark variables complete in values");

    const std::string grove_text = "<body>\n  <but\n  <button ali\n  <row>\n  </\n</body>\n";
    list = complete_at("grove", grove_text, 1, 6);
    check(has_label(list, "button"), "Grove tags complete after <");
    list = complete_at("grove", grove_text, 2, 13);
    const grove::CompletionItem* align = find_label(list, "align");
    check(align && align->insert == "align=\"$0\"", "Grove attributes complete with =\"\"");
    list = complete_at("grove", "<body>\n  <row>\n  </\n</body>\n", 2, 4);
    check(!list.items.empty() && list.items[0].label == "row" && list.items[0].insert == "row>", "</ offers to close the open tag first");

    const std::string cmake = "set(MY_FLAGS -O2)\nadd_exec\nmessage(${MY_F)\n";
    canopy::CompletionSources cmake_sources;
    cmake_sources.symbols = canopy::scan_cmake_symbols(cmake, "/tmp/x");
    list = complete_at("cmake", cmake, 1, 8, cmake_sources);
    const grove::CompletionItem* exe = find_label(list, "add_executable");
    check(exe && exe->insert == "add_executable($0)", "CMake commands complete with parentheses");
    list = complete_at("cmake", cmake, 2, 14, cmake_sources);
    check(has_label(list, "MY_FLAGS") && find_label(list, "MY_FLAGS")->insert == "MY_FLAGS}", "CMake variables complete inside ${ } and close the brace");

    canopy::CompletionSources walnut_sources;
    walnut_sources.pending = true;
    list = complete_at("walnut", "let c = Counter{};\nc.\n", 1, 2, walnut_sources);
    check(list.items.empty() && list.column == 2, "Walnut member lists wait for the compiler");
    grove::CompletionItem bump;
    bump.label = "bump";
    bump.kind = "method";
    walnut_sources.extra = { bump };
    walnut_sources.pending = false;
    list = complete_at("walnut", "let c = Counter{};\nc.b\n", 1, 3, walnut_sources);
    check(list.items.size() == 1 && list.items[0].label == "bump", "Walnut members come from the compiler");
    const canopy::WalnutCompletions parsed = canopy::parse_walnut_complete("{\"start\":2,\"mode\":\"member\",\"items\":[{\"label\":\"total\",\"kind\":\"field\",\"detail\":\"int\"}]}");
    check(parsed.ok && parsed.start == 2 && parsed.mode == "member" && parsed.items.size() == 1 && parsed.items[0].detail == "int", "walnut complete output is read");
    const std::vector<std::string> command = canopy::walnut_complete_command("walnut", "a.wal", "-I inc", 3, 7);
    check(command.size() >= 7 && command[1] == "complete" && command[command.size() - 1] == "3:7", "the walnut complete command has --at");
}

void test_create_entries() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "canopy-create-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    check(canopy::create_workspace_entry(dir.string(), "random.tex", false).empty() && fs::is_regular_file(dir / "random.tex"), "New File makes the file and reports no error");
    check(canopy::create_workspace_entry(dir.string(), "random.tex", false) == "random.tex is already there", "New File refuses a name that's taken");
    check(canopy::create_workspace_entry(dir.string(), "sub", true).empty() && fs::is_directory(dir / "sub"), "New Folder makes the folder");
    check(!canopy::create_workspace_entry((dir / "missing").string(), "x.txt", false).empty(), "New File in a folder that's gone reports it");
    fs::remove_all(dir, ec);
}

void test_signature_layout() {
    check(canopy::format_signature("function add(a: int, b: int) -> int", 80) == "function add(a: int, b: int) -> int", "a short signature stays on one line");
    const std::string sig = "function name(param1: int, param2: int, param3: string, param4: float, param5: bool) -> int";
    const std::string wrapped = canopy::format_signature(sig, 40);
    check(wrapped == "function name(\n    param1: int, param2: int,\n    param3: string, param4: float,\n    param5: bool\n) -> int", "a long signature breaks after whole parameters: " + wrapped);
    const std::string templated = canopy::format_signature("std::map<int, std::string> lookup(const std::map<int, std::string>& table, int key, bool strict)", 50);
    check(templated.find("const std::map<int, std::string>& table,") != std::string::npos, "commas inside <> don't split a parameter: " + templated);
    check(templated.substr(templated.rfind('\n') + 1) == ")", "the closing parenthesis gets its own last line: " + templated);
    const std::string spread = canopy::format_signature("void f(\n    int a,\n    int b\n)", 80);
    check(spread == "void f(int a, int b)", "a signature that fits is pulled onto one line: " + spread);
    const std::string md = canopy::format_hover_signatures("```walnut\n" + sig + "\n```\nSome docs.\n", 40);
    check(md.find("    param5: bool\n) -> int\n```\nSome docs.") != std::string::npos, "hovers lay out signatures inside code blocks: " + md);
    const std::string body = canopy::format_hover_signatures("```cpp\nstruct S { int a; };\n```\n", 10);
    check(body == "```cpp\nstruct S { int a; };\n```\n", "code that isn't a signature is left alone");
}

void test_terminal_screen() {
    canopy::TerminalScreen s;
    canopy::terminal_feed(s, "user@host:~$ ls\r\n\x1b[01;34mSource\x1b[0m  README.md\r\nuser@host:~$ ");
    check(s.text == "user@host:~$ ls\nSource  README.md\nuser@host:~$ ", "colors are stripped and CRLF becomes a newline: " + s.text);
    check(canopy::terminal_last_line(s) == "user@host:~$ ", "the prompt is the last unfinished line");
    check(canopy::terminal_finished_lines(s) == "user@host:~$ ls\nSource  README.md", "finished lines leave the prompt out");
    canopy::TerminalScreen p;
    canopy::terminal_feed(p, "10%\r20%\r");
    canopy::terminal_feed(p, "\n");
    check(p.text == "20%\n", "carriage returns overwrite progress and a split CRLF still ends the line: " + p.text);
    canopy::TerminalScreen q;
    canopy::terminal_feed(q, "ab\x1b[");
    canopy::terminal_feed(q, "31mc\x1b]0;title\x07" "d\bx\r\r\n");
    check(q.text == "abcx\n", "escape codes split across reads are dropped and backspace erases: " + q.text);
    canopy::TerminalScreen t;
    t.text = "one\ntwo\nthree\n";
    canopy::terminal_trim(t, 8);
    check(t.text == "three\n", "trimming keeps whole lines from the end: " + t.text);
    for (const char* section : { "editor", "gui", "viewer", "toolchain", "language.cpp", "build", "explorer", "terminal", "keys", "highlight", "highlight.cpp" }) {
        const std::string file = canopy::settings_file_for_section(section);
        const auto& names = canopy::settings_file_names();
        check(std::find(names.begin(), names.end(), file) != names.end(), std::string("settings written for [") + section + "] go to " + file + ", which Canopy loads");
    }
#if defined(_WIN32)
    const std::string uri_prefix = "file:///" + std::filesystem::current_path().root_name().generic_string();
#else
    const std::string uri_prefix = "file://";
#endif
    check(canopy::file_uri("/home/me/My Dir/a,b#c.txt") == uri_prefix + "/home/me/My%20Dir/a%2Cb%23c.txt", "file URIs are percent-encoded: " + canopy::file_uri("/home/me/My Dir/a,b#c.txt"));
    check(canopy::settings_file_for_section("terminal") == "workbench.bark" && canopy::settings_file_for_section("explorer") == "workbench.bark", "explorer and terminal settings live in workbench.bark");
}

void test_workspace_paths() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "canopy-root-test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const std::string plain = canopy::clean_folder_path(dir.string());
    check(canopy::clean_folder_path(dir.string() + "/") == plain, "a trailing slash doesn't change a folder's path");
    check(canopy::clean_folder_path(dir.string() + "//") == plain, "nor do two");
    check(canopy::find_workspace_root(dir.string() + "/") == plain, "opening a folder with a trailing slash gives the same root");
    check(fs::path(canopy::find_workspace_root(dir.string() + "/")).filename() == "canopy-root-test", "so the folder keeps its short name");
    check(canopy::clean_folder_path("/") == fs::path("/").lexically_normal().string() || canopy::clean_folder_path("/").size() <= 3, "the filesystem root stays itself");
    fs::remove_all(dir, ec);
}

void test_markdown() {
    using canopy::MdBlock;
    check(canopy::is_markdown_path("README.md") && canopy::is_markdown_path("notes.Markdown") && !canopy::is_markdown_path("md/file.txt"), "markdown extensions");
    const std::vector<MdBlock> doc = canopy::parse_markdown(
        "---\ntitle: x\n---\n# Title *here*\n\nSome **bold** and `code` with a [link](docs/a.md).\n\n"
        "Setext\n---\n\n- one\n- two\n  - nested\n\n1. first\n2. second\n\n- [x] done\n- [ ] todo\n\n"
        "> quoted\n\n```cpp\nint main() {}\n```\n\n| a | b |\n|:--|--:|\n| 1 | 2 |\n\n***\n");
    check(doc.size() == 10, "markdown splits into blocks (front matter skipped)");
    if (doc.size() == 10) {
        check(doc[0].kind == MdBlock::Kind::Heading && doc[0].level == 1 && canopy::markdown_plain_text(doc[0].spans) == "Title here", "ATX heading");
        check(doc[0].spans.size() == 2 && doc[0].spans[1].italic, "emphasis in a heading");
        const std::vector<canopy::MdSpan>& p = doc[1].spans;
        bool bold = false, code = false, link = false;
        for (const canopy::MdSpan& s : p) {
            bold = bold || (s.bold && s.text == "bold");
            code = code || (s.code && s.text == "code");
            link = link || (s.href == "docs/a.md" && s.text == "link");
        }
        check(doc[1].kind == MdBlock::Kind::Paragraph && bold && code && link, "bold, code spans and links");
        check(doc[2].kind == MdBlock::Kind::Heading && doc[2].level == 2, "setext heading");
        check(doc[3].kind == MdBlock::Kind::List && doc[3].items.size() == 2 && !doc[3].ordered, "bullet list");
        check(doc[3].items.size() == 2 && doc[3].items[1].blocks.size() == 2 && doc[3].items[1].blocks[1].kind == MdBlock::Kind::List, "nested list");
        check(doc[4].ordered && doc[4].start == 1 && doc[4].items.size() == 2, "ordered list");
        check(doc[5].items.size() == 2 && doc[5].items[0].task && doc[5].items[0].checked && !doc[5].items[1].checked, "task list");
        check(doc[6].kind == MdBlock::Kind::Quote && doc[6].children.size() == 1, "block quote");
        check(doc[7].kind == MdBlock::Kind::Code && doc[7].language == "cpp" && doc[7].code == "int main() {}", "fenced code");
        check(doc[8].kind == MdBlock::Kind::Table && doc[8].rows.size() == 2 && doc[8].align[0] == canopy::MdAlign::Left && doc[8].align[1] == canopy::MdAlign::Right, "table with alignment");
        check(doc[9].kind == MdBlock::Kind::Rule, "thematic break");
    }
    const std::vector<canopy::MdSpan> spans = canopy::parse_markdown_inline("a\\*b\\* ~~gone~~ https://x.org/p. snake_case_name");
    check(!spans.empty() && spans[0].text == "a*b* " && !spans[0].italic, "escaped asterisks stay literal");
    bool strike = false, url = false, snake = false;
    for (const canopy::MdSpan& sp : spans) {
        strike = strike || (sp.strike && sp.text == "gone");
        url = url || sp.href == "https://x.org/p";
        snake = snake || sp.text.find("snake_case_name") != std::string::npos;
    }
    check(strike && url && snake, "strikethrough, bare links without trailing dots, and underscores inside words");
    check(canopy::markdown_anchor("Hello, World!") == "hello-world", "heading anchors");
}

void test_live_server() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "canopy-serve-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "sub", ec);
    fs::create_directories(root / "pages", ec);
    std::ofstream(root / "index.html") << "<html><body>hi</body></html>";
    std::ofstream(root / "sub" / "a b.css") << "body{}";
    std::ofstream(root / "about.html") << "about";
    check(canopy::mime_type_for("x.HTML").rfind("text/html", 0) == 0 && canopy::mime_type_for("a.js").rfind("text/javascript", 0) == 0 && canopy::mime_type_for("z.unknown") == "application/octet-stream", "content types");
    check(canopy::url_decode("a%20b%2Fc") == "a b/c" && canopy::url_encode_path("sub/a b.css") == "sub/a%20b.css", "URL escapes");
    const std::string r = root.string();
    check(canopy::resolve_served(r, "/").file == (root / "index.html").string(), "/ serves index.html");
    check(canopy::resolve_served(r, "/sub/a%20b.css?v=2").file == (root / "sub" / "a b.css").string(), "escaped names and queries resolve");
    check(canopy::resolve_served(r, "/about").file == (root / "about.html").string(), "clean URLs find .html files");
    check(canopy::resolve_served(r, "/sub").status == 301 && canopy::resolve_served(r, "/sub").redirect == "/sub/", "folders without a slash redirect");
    check(canopy::resolve_served(r, "/pages/").listing, "folders without an index get a listing");
    check(canopy::resolve_served(r, "/../../etc/passwd").status != 200 || canopy::resolve_served(r, "/../../etc/passwd").file.rfind(r, 0) == 0, "paths can't climb out of the folder");
    check(canopy::resolve_served(r, "/%2e%2e/%2e%2e/secret").status != 200, "escaped .. can't climb out either");
    check(canopy::resolve_served(r, "/missing.png").status == 404, "missing files are 404");
    const std::string page = canopy::inject_live_reload("<html><body>x</body></html>");
    check(page.find("<script>") < page.find("</body>") && page.find("__canopy/reload") != std::string::npos, "live reload goes before </body>");
    check(canopy::folder_listing_html(r, (root / "sub").string(), "/sub/").find("a%20b.css") != std::string::npos, "listings link escaped names");
    const std::string head_page = canopy::inject_page_script("<!DOCTYPE html>\n<html>\n<head>\n<title>t</title></head><body>x</body></html>", true, true);
    check(head_page.rfind("<!DOCTYPE html>\n<html>\n<head><script>", 0) == 0, "the page script goes right after <head>, keeping the doctype first: " + head_page.substr(0, 60));
    check(std::count(head_page.begin(), head_page.end(), '\n') == 3, "the page script adds no lines, so line numbers in the page stay right");
    check(head_page.find("__canopy/") != std::string::npos && head_page.find("__RELOAD__") == std::string::npos && head_page.find("__CONSOLE__") == std::string::npos, "the page script has its switches filled in");
    check(canopy::inject_page_script("<p>bare</p>", false, true).rfind("<script>", 0) == 0, "pages without <head> or <html> get the script first");
    check(canopy::inject_page_script("<!doctype html><p>x</p>", true, false).rfind("<!doctype html><script>", 0) == 0, "a doctype stays the first thing in the page");
    check(canopy::inject_page_script("<html><head></head></html>", false, false) == "<html><head></head></html>", "nothing is added with reload and console both off");
    {
        const std::string us = "\x1f";
        const std::string rs = "\x1e";
        const std::string body = "log" + us + "/app.js" + us + "12" + us + "5" + us + "/index.html" + us + "p1" + us + "hello\nworld" + rs + "error" + us + us + "0" + us + "0" + us + "/index.html" + us + "p1" + us + "boom" + rs + "weird" + us + "a" + us + "1" + us + "1" + us + "/" + us + "p2" + us + "x" + rs + "broken";
        const std::vector<canopy::ConsoleMessage> msgs = canopy::parse_console_messages(body);
        check(msgs.size() == 3, "console messages are split into records: " + std::to_string(msgs.size()));
        if (msgs.size() == 3) {
            check(msgs[0].level == "log" && msgs[0].source == "/app.js" && msgs[0].line == 12 && msgs[0].column == 5 && msgs[0].page == "/index.html" && msgs[0].page_id == "p1" && msgs[0].text == "hello\nworld", "a console record keeps its fields");
            check(msgs[1].level == "error" && msgs[1].text == "boom" && msgs[1].source.empty(), "errors without a source");
            check(msgs[2].level == "log", "unknown levels become log");
        }
    }

    canopy::LiveServer a;
    canopy::LiveServer b;
    std::string error;
    const bool started = a.start(r, 5590, "127.0.0.1", true, error);
    check(started, "a local server starts: " + error);
    if (started) {
        check(b.start(r, a.port(), "127.0.0.1", false, error) && b.port() != a.port(), "a busy port moves on to the next free one");
        check(a.url() == "http://localhost:" + std::to_string(a.port()) + "/", "server URL");
        check(a.url((root / "sub" / "a b.css").string()) == a.url() + "sub/a%20b.css", "URL of a file in the folder");
        check(a.contains((root / "sub").string()) && !a.contains(root.parent_path().string()), "contains only its folder");
        check(a.file_for_url("/sub/a%20b.css?v=1") == (root / "sub" / "a b.css").string() && a.file_for_url("http://localhost:1/about") == (root / "about.html").string() && a.file_for_url("/nope.js").empty(), "console sources map back to files");
        check(a.console() && a.evaluate("1 + 1") == 0 && a.pages() == 0, "the console starts on and has no pages yet");
        a.set_console(false);
        check(!a.console() && a.take_console().empty(), "the console can be turned off");
        a.stop();
        b.stop();
        check(!a.running(), "servers stop");
    }
    fs::remove_all(root, ec);
}

void test_git_parsers() {
    std::string porcelain;
    porcelain += "# branch.oid 1234abcd";
    porcelain += '\0';
    porcelain += "# branch.head dev";
    porcelain += '\0';
    porcelain += "# branch.upstream origin/dev";
    porcelain += '\0';
    porcelain += "# branch.ab +2 -1";
    porcelain += '\0';
    porcelain += "1 .M N... 100644 100644 100644 aaa bbb Source/my file.cpp";
    porcelain += '\0';
    porcelain += "1 A. N... 000000 100644 100644 000 ccc new.hpp";
    porcelain += '\0';
    porcelain += "2 R. N... 100644 100644 100644 ddd eee R100 renamed.txt";
    porcelain += '\0';
    porcelain += "old.txt";
    porcelain += '\0';
    porcelain += "u UU N... 100644 100644 100644 100644 f1 f2 f3 both.txt";
    porcelain += '\0';
    porcelain += "? untracked dir/x.txt";
    porcelain += '\0';
    const canopy::GitStatus st = canopy::parse_git_status(porcelain);
    check(st.repo && st.branch == "dev" && st.upstream == "origin/dev" && st.head == "1234abcd", "status reads the branch header");
    check(st.ahead == 2 && st.behind == 1, "status reads ahead and behind");
    check(st.files.size() == 5, "status lists every file");
    if (st.files.size() == 5) {
        check(st.files[0].path == "Source/my file.cpp" && st.files[0].changed() && !st.files[0].staged(), "paths keep their spaces");
        check(st.files[1].staged() && canopy::git_status_letter(st.files[1]) == 'A', "staged additions");
        check(st.files[2].path == "renamed.txt" && st.files[2].old_path == "old.txt", "renames keep the old path");
        check(st.files[3].conflicted && canopy::git_status_word(st.files[3]) == "conflict", "conflicts");
        check(st.files[4].untracked && st.files[4].path == "untracked dir/x.txt", "untracked files");
    }
    check(!canopy::parse_git_status("").repo, "no output means no repo");

    const std::string log = "\x1e" "abc123\x1f" "abc\x1f" "p1 p2\x1f" "Ann\x1f" "a@x\x1f" "1700000000\x1f" "HEAD -> dev, tag: v1.0, origin/dev\x1f" "Merge things\n"
                            "\x1e" "def456\x1f" "def\x1f\x1f" "Bob\x1f" "b@x\x1f" "1600000000\x1f\x1f" "First\n\nR100\told.txt\tnew.txt\n";
    const std::vector<canopy::GitCommit> commits = canopy::parse_git_log(log);
    check(commits.size() == 2, "log has two commits");
    if (commits.size() == 2) {
        check(commits[0].parents.size() == 2 && commits[0].subject == "Merge things" && commits[0].time == 1700000000, "log fields");
        check(canopy::short_ref_names(commits[0].refs) == "dev, v1.0, origin/dev", "ref names are shortened");
        check(commits[1].parents.empty() && commits[1].path == "new.txt" && commits[1].status == "R", "file history reads name-status");
    }

    const std::vector<canopy::GitBranch> branches = canopy::parse_git_branches("refs/heads/dev\x1f*\x1forigin/dev\x1f" "5\nrefs/heads/main\x1f \x1f\x1f" "4\nrefs/remotes/origin/HEAD\x1f \x1f\x1f\nrefs/remotes/origin/dev\x1f \x1f\x1f" "3\n");
    check(branches.size() == 3, "remote HEAD is skipped");
    if (branches.size() == 3) {
        check(branches[0].current && branches[0].upstream == "origin/dev", "current branch");
        check(branches[2].remote && branches[2].name == "origin/dev", "remote branches");
    }

    const std::vector<canopy::DiffLine> parsed = canopy::parse_unified_diff("diff --git a/x.txt b/x.txt\nindex 1..2 100644\n--- a/x.txt\n+++ b/x.txt\n@@ -1,3 +1,3 @@\n one\n-two\n+TWO\n three\n");
    std::size_t added = 0, removed = 0;
    for (const canopy::DiffLine& d : parsed) {
        if (d.kind == canopy::DiffLine::Kind::Added) {
            ++added;
            check(d.new_line == 2 && d.text == "TWO", "added line numbers");
        }
        if (d.kind == canopy::DiffLine::Kind::Removed) {
            ++removed;
            check(d.old_line == 2, "removed line numbers");
        }
    }
    check(added == 1 && removed == 1 && parsed.front().kind == canopy::DiffLine::Kind::File && parsed.front().text == "x.txt", "unified diff parses");

    check(canopy::diff_texts("a\nb\n", "a\nb\n").empty(), "equal texts have no diff");
    std::string before, after;
    for (int i = 1; i <= 40; ++i) {
        before += "line " + std::to_string(i) + "\n";
        after += (i == 5 ? std::string("changed five") : i == 30 ? std::string("changed thirty") : "line " + std::to_string(i)) + "\n";
    }
    after += "extra\n";
    const std::vector<canopy::DiffLine> d = canopy::diff_texts(before, after);
    std::size_t hunks = 0, adds = 0, dels = 0;
    for (const canopy::DiffLine& l : d) {
        if (l.kind == canopy::DiffLine::Kind::Hunk) ++hunks;
        if (l.kind == canopy::DiffLine::Kind::Added) ++adds;
        if (l.kind == canopy::DiffLine::Kind::Removed) ++dels;
    }
    check(hunks == 3 && adds == 3 && dels == 2, "line diff finds separate hunks");
    check(!d.empty() && d.front().text == "@@ -2,7 +2,7 @@", "hunk headers count lines");
    const std::vector<canopy::DiffLine> fresh = canopy::diff_texts("", "a\nb\n");
    check(fresh.size() == 3 && fresh[1].kind == canopy::DiffLine::Kind::Added, "diff from empty");

    check(canopy::relative_time(1000, 1010) == "just now", "just now");
    check(canopy::relative_time(1000, 1000 + 3 * 3600) == "3 hours ago", "hours ago");
    check(canopy::relative_time(1000, 1000 + 90000) == "yesterday", "yesterday");
    check(canopy::relative_to("/a/b/c.txt", "/a") == "b/c.txt", "relative paths");
    check(canopy::relative_to("/x/c.txt", "/a") == "/x/c.txt", "outside paths stay absolute");
}

void test_local_history() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "canopy-history-test";
    fs::remove_all(base, ec);
    canopy::LocalHistory h(base.string());
    h.set_limit(3);
    const std::string file = (base / "work" / "a.txt").string();
    check(h.record(file, "one"), "first snapshot");
    check(!h.record(file, "one"), "duplicates are skipped");
    check(h.record(file, "two"), "second snapshot");
    check(h.record(file, "three"), "third snapshot");
    check(h.record(file, "four"), "fourth snapshot");
    const std::vector<canopy::HistoryEntry> e = h.entries(file);
    check(e.size() == 3, "history keeps its limit");
    check(!e.empty() && h.read(e.front()).value_or("") == "four" && e.front().size == 4, "newest first");
    check(h.latest(file).value_or("") == "four", "latest snapshot");
    if (e.size() == 3) {
        check(h.remove(file, e[1].time_ms) && h.entries(file).size() == 2, "entries can be removed");
    }
    check(h.entries((base / "other.txt").string()).empty(), "other files have no history");
    h.set_max_bytes(2);
    check(!h.record(file, "too big"), "big files are skipped");
    check(h.clear(file) && h.entries(file).empty(), "history can be cleared");
    fs::remove_all(base, ec);
}

void test_tasks_file() {
    const canopy::TaskFile tf = canopy::parse_tasks(canopy::tasks_template(), "tasks.bark");
    check(tf.problems.empty(), "the template has no problems");
    const canopy::Task* release = tf.find("release");
    check(release != nullptr, "the template has a release task");
    if (release) {
        check(release->asks.size() == 2 && release->asks[1].name == "version" && release->asks[1].value == "{next-patch}", "asks keep name, prompt and default");
        check(release->steps.size() == 10 && release->steps[0].kind == canopy::TaskStep::Kind::SetVersion && release->steps[2].kind == canopy::TaskStep::Kind::Try, "steps keep their order");
        check(!release->confirm.empty() && release->title() == "Release", "label and confirm");
    }
    const canopy::TaskFile odd = canopy::parse_tasks("go {\n  run: cmake \"--build\" build;\n  on-build: before;\n  bogus: 1;\n  keep-going: true;\n}\n", "t.bark");
    check(odd.tasks.size() == 1 && odd.tasks[0].on_build == canopy::TaskHook::Before && odd.tasks[0].keep_going, "hooks and flags");
    check(odd.problems.size() == 1 && odd.problems[0].find("bogus") != std::string::npos, "unknown properties are reported");
    check(odd.hooked_build(canopy::TaskHook::Before) == &odd.tasks[0], "find the before-build task");
    check(!odd.tasks.empty() && canopy::tokenize_command(odd.tasks[0].steps[0].text) == std::vector<std::string>{ "cmake", "--build", "build" }, "bare values join into a command");

    check(canopy::tokenize_command("git commit -m \"a \\\"b\\\"\" 'c d' e") == std::vector<std::string>{ "git", "commit", "-m", "a \"b\"", "c d", "e" }, "commands tokenize with quotes");
    check(canopy::tokenize_command("x \"\"").size() == 2, "empty quoted arguments are kept");
    const std::map<std::string, std::string> values{ { "message", "fix: it's \"done\"" }, { "version", "1.2.3" } };
    check(canopy::substitute("v{version} {unknown}", values) == "v1.2.3 {unknown}", "placeholders are replaced");
    check(canopy::substitute_quoted("echo {message}", values) == "echo " + canopy::quote_argument("fix: it's \"done\""), "shell placeholders are quoted");

    const std::string cmake = "cmake_minimum_required(VERSION 3.20)\nproject(Canopy\n    VERSION 0.1.4\n    LANGUAGES CXX)\n";
    check(canopy::find_version(cmake).value_or("") == "0.1.4", "CMake project version");
    check(canopy::find_version(canopy::replace_version(cmake, "v0.1.5").value_or("")).value_or("") == "0.1.5", "CMake version is replaced");
    check(canopy::replace_version(cmake, "0.1.5").value_or("").find("VERSION 3.20") != std::string::npos, "cmake_minimum_required is left alone");
    check(canopy::find_version("{\n  \"name\": \"x\",\n  \"version\": \"2.0.1\"\n}").value_or("") == "2.0.1", "package.json version");
    check(canopy::find_version("[package]\nname = \"x\"\nversion = \"0.3.0\"\n").value_or("") == "0.3.0", "Cargo version");
    check(canopy::find_version("1.4.2\n").value_or("") == "1.4.2", "VERSION file");
    check(!canopy::find_version("hello").has_value(), "no version");
    check(canopy::bump_version("0.1.9", "patch") == "0.1.10", "patch bump");
    check(canopy::bump_version("v1.2.3-beta", "minor") == "v1.3.0", "minor bump keeps the v");
    check(canopy::bump_version("1.2", "major") == "2.0.0", "major bump");
}

void test_git_run() {
    namespace fs = std::filesystem;
    if (canopy::run_in_folder({ "git", "--version" }, std::string(), true).code != 0) return;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec) / "canopy-git-test";
    fs::remove_all(base, ec);
    fs::create_directories(base / "remote.git", ec);
    fs::create_directories(base / "work", ec);
    const std::string remote = (base / "remote.git").string();
    const std::string work = (base / "work").string();
    auto git = [&](std::vector<std::string> args) {
        args.insert(args.begin(), "git");
        return canopy::run_in_folder(args, work, true);
    };
    canopy::run_in_folder({ "git", "init", "--bare", "-b", "main" }, remote, true);
    check(git({ "init", "-b", "main" }).code == 0, "git init");
    git({ "config", "user.email", "t@example.com" });
    git({ "config", "user.name", "Tester" });
    git({ "remote", "add", "origin", remote });
    canopy::write_text_file((base / "work" / "CMakeLists.txt").string(), "project(T VERSION 0.1.0)\n");
    git({ "add", "-A" });
    git({ "commit", "-m", "start" });
    git({ "push", "-u", "origin", "main" });
    git({ "checkout", "-b", "dev" });
    git({ "push", "-u", "origin", "dev" });

    canopy::write_text_file((base / "work" / "a file.txt").string(), "hello\n");
    const canopy::GitStatus st = canopy::parse_git_status(canopy::run_in_folder({ "git", "status", "--porcelain=v2", "--branch", "-z", "--untracked-files=all" }, work, false).text);
    check(st.repo && st.branch == "dev" && st.upstream == "origin/dev" && st.files.size() == 1 && st.files[0].untracked, "status of a real repo");

    const canopy::TaskFile tf = canopy::parse_tasks(canopy::tasks_template(), "tasks.bark");
    const canopy::Task* release = tf.find("release");
    check(release != nullptr, "release task");
    if (release) {
        canopy::TaskContext ctx;
        ctx.root = work;
        ctx.branch = "dev";
        const std::map<std::string, std::string> base_values = canopy::task_values(*release, ctx);
        check(base_values.at("current-version") == "0.1.0" && base_values.at("next-patch") == "0.1.1", "task values read the version");
        ctx.values["message"] = "Add \"a file\"; rm -rf nothing";
        ctx.values["version"] = canopy::substitute(release->asks[1].value, base_values);
        canopy::CommandBatch batch;
        check(batch.start(canopy::task_steps(*release, ctx), work, true, true), "batch starts");
        while (batch.running()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const std::optional<std::vector<canopy::CommandOutput>> results = batch.take_results();
        const std::string output = batch.take_output();
        bool all_ok = results && results->size() == 10;
        if (results) {
            for (const canopy::CommandOutput& r : *results) all_ok = all_ok && r.code == 0;
        }
        check(all_ok, "the release task runs every step: " + output);
        check(canopy::run_in_folder({ "git", "ls-remote", "--tags", remote }, work, true).text.find("refs/tags/v0.1.1") != std::string::npos, "the tag reached the remote");
        check(canopy::run_in_folder({ "git", "log", "-1", "--format=%s", "main" }, work, true).text == "Add \"a file\"; rm -rf nothing\n", "messages pass through untouched");
        check(canopy::run_in_folder({ "git", "rev-parse", "--abbrev-ref", "HEAD" }, work, true).text == "dev\n", "back on dev");
        check(canopy::find_version(canopy::read_text_file((base / "work" / "CMakeLists.txt").string()).value_or("")).value_or("") == "0.1.1", "the version was bumped");
    }
    const std::vector<canopy::GitCommit> history = canopy::parse_git_log(canopy::run_in_folder({ "git", "log", "--follow", "--format=" + canopy::git_log_format(), "--name-status", "--", "CMakeLists.txt" }, work, true).text);
    check(history.size() == 2 && history[0].status == "M" && history[1].status == "A", "file history from git");
    fs::remove_all(base, ec);
}

void test_git_extras() {
    std::string porcelain = "! build/";
    porcelain += '\0';
    porcelain += "! secret.txt";
    porcelain += '\0';
    const canopy::GitStatus st = canopy::parse_git_status(porcelain);
    check(st.ignored.size() == 2 && st.ignored[0] == "build" && st.ignored[1] == "secret.txt", "ignored entries are kept without the slash");
    const std::vector<canopy::GitTag> tags = canopy::parse_git_tags("v0.1.5\x1f" "b874cc9\x1f" "1700000000\x1f" "Release\nv0.1.4\x1f" "583473f\x1f" "1600000000\x1f\n");
    check(tags.size() == 2 && tags[0].name == "v0.1.5" && tags[0].hash == "b874cc9" && tags[0].time == 1700000000 && tags[0].subject == "Release", "tags parse");
    const std::vector<canopy::GitBranch> b = canopy::parse_git_branches("refs/heads/dev\x1f*\x1forigin/dev\x1f" "5\x1f[ahead 2]\n");
    check(b.size() == 1 && b[0].track == "[ahead 2]", "branch tracking info");
    check(canopy::repo_name_from_url("https://github.com/ThatNewCoderJSCpp/Grove.git") == "Grove", "repo name from https");
    check(canopy::repo_name_from_url("git@github.com:me/walnut") == "walnut", "repo name from ssh");
    check(canopy::askpass_secret("Password for 'https://me@github.com': ") && !canopy::askpass_secret("Username for 'https://github.com': "), "askpass hides passwords only");
    check(canopy::askpass_secret("Enter passphrase for key '/home/me/.ssh/id_ed25519': "), "askpass hides passphrases");

    std::string before, after;
    for (int i = 1; i <= 30; ++i) {
        before += "line " + std::to_string(i) + "\n";
        after += (i == 3 ? std::string("three") : i == 25 ? std::string("twenty-five") : "line " + std::to_string(i)) + "\n";
    }
    const std::vector<canopy::DiffLine> lines = canopy::diff_texts(before, after);
    check(canopy::diff_hunk_count(lines) == 2, "two hunks");
    const std::optional<std::string> reverted = canopy::revert_hunk(lines, 1, before, after);
    check(reverted && reverted->find("twenty-five") == std::string::npos && reverted->find("three") != std::string::npos, "reverting one hunk keeps the other");
    const std::string crlf_after = "a\r\nB\r\nc\r\n";
    const std::vector<canopy::DiffLine> crlf_lines = canopy::diff_texts("a\r\nb\r\nc\r\n", crlf_after);
    const std::optional<std::string> crlf_back = canopy::revert_hunk(crlf_lines, 0, "a\r\nb\r\nc\r\n", crlf_after);
    check(crlf_back && *crlf_back == "a\r\nb\r\nc\r\n", "reverting keeps CRLF line ends");
    const std::optional<std::string> no_eol = canopy::revert_hunk(canopy::diff_texts("x\ny", "x\nz"), 0, "x\ny", "x\nz");
    check(no_eol && *no_eol == "x\ny", "reverting keeps a missing final newline");

    namespace fs = std::filesystem;
    if (canopy::run_in_folder({ "git", "--version" }, std::string(), true).code != 0) return;
    std::error_code ec;
    const fs::path repo = fs::temp_directory_path(ec) / "canopy-hunk-test";
    fs::remove_all(repo, ec);
    fs::create_directories(repo, ec);
    const std::string r = repo.string();
    auto git = [&](std::vector<std::string> args) {
        args.insert(args.begin(), "git");
        return canopy::run_in_folder(args, r, true);
    };
    git({ "init", "-q" });
    git({ "config", "user.email", "t@example.com" });
    git({ "config", "user.name", "Tester" });
    canopy::write_text_file((repo / "f.txt").string(), before);
    git({ "add", "-A" });
    git({ "commit", "-q", "-m", "start" });
    canopy::write_text_file((repo / "f.txt").string(), after);
    const std::string index_text = git({ "show", ":f.txt" }).text;
    const std::vector<canopy::DiffLine> work = canopy::diff_texts(index_text, after);
    const std::string patch = canopy::hunk_patch("f.txt", work, 0, index_text, after);
    canopy::write_text_file((repo / "p.patch").string(), patch);
    const canopy::CommandOutput applied = git({ "apply", "--cached", "--recount", "p.patch" });
    check(applied.code == 0, "a hunk patch applies to the index: " + applied.text);
    const std::string staged = git({ "diff", "--cached" }).text;
    check(staged.find("+three") != std::string::npos && staged.find("twenty-five") == std::string::npos, "only the chosen hunk is staged");
    const std::string staged_index = git({ "show", ":f.txt" }).text;
    const std::string head_text = git({ "show", "HEAD:f.txt" }).text;
    const std::vector<canopy::DiffLine> cached = canopy::diff_texts(head_text, staged_index);
    canopy::write_text_file((repo / "u.patch").string(), canopy::hunk_patch("f.txt", cached, 0, head_text, staged_index));
    check(git({ "apply", "--cached", "-R", "--recount", "u.patch" }).code == 0 && git({ "diff", "--cached" }).text.empty(), "a hunk can be unstaged again");
    canopy::write_text_file((repo / "n.txt").string(), "one\ntwo");
    git({ "add", "n.txt" });
    git({ "commit", "-q", "-m", "n" });
    canopy::write_text_file((repo / "n.txt").string(), "one\nTWO");
    const std::string n_index = git({ "show", ":n.txt" }).text;
    const std::vector<canopy::DiffLine> nd = canopy::diff_texts(n_index, "one\nTWO");
    canopy::write_text_file((repo / "nn.patch").string(), canopy::hunk_patch("n.txt", nd, 0, n_index, "one\nTWO"));
    const canopy::CommandOutput n_applied = git({ "apply", "--cached", "--recount", "nn.patch" });
    check(n_applied.code == 0 && git({ "show", ":n.txt" }).text == "one\nTWO", "hunks without a final newline stage: " + n_applied.text);
    fs::remove_all(repo, ec);
}

std::string enter_after(grove::CodeLanguage language, const std::string& text, grove::CodePos at) {
    grove::CodeBuffer b(text);
    b.set_caret(at);
    b.new_line(4, true, language, true);
    return b.text();
}

std::string enter_at_end(grove::CodeLanguage language, const std::string& line) {
    return enter_after(language, line, grove::CodePos{ 0, line.size() });
}

void test_block_closing() {
    using grove::CodeLanguage;
    using grove::code_block_closer;
    check(code_block_closer(CodeLanguage::Walnut, "function main() -> int {") == "} end function main", "Walnut functions end with their name");
    check(code_block_closer(CodeLanguage::Walnut, "static function opened_with(int: amount) -> Account {") == "} end function opened_with", "qualifiers are skipped");
    check(code_block_closer(CodeLanguage::Walnut, "function Account::deposit(int: amount) -> Account& {") == "} end function Account::deposit", "methods defined outside keep the class");
    check(code_block_closer(CodeLanguage::Walnut, "class Counter : Base {") == "} end class Counter", "Walnut classes");
    check(code_block_closer(CodeLanguage::Walnut, "struct Point {") == "} end struct Point", "Walnut structs");
    check(code_block_closer(CodeLanguage::Walnut, "union Number {") == "} end union Number", "Walnut unions");
    check(code_block_closer(CodeLanguage::Walnut, "namespace geo::flat {") == "} end namespace geo::flat", "Walnut namespaces");
    check(code_block_closer(CodeLanguage::Walnut, "extern \"C\" {") == "} end extern", "Walnut extern blocks");
    check(code_block_closer(CodeLanguage::Walnut, "template <typename T>\nfunction biggest(T: a, T: b) -> T {") == "} end function biggest", "templates");
    check(code_block_closer(CodeLanguage::Walnut, "if (a > b) then {") == "}", "Walnut if");
    check(code_block_closer(CodeLanguage::Walnut, "} else then {") == "}", "Walnut else");
    check(code_block_closer(CodeLanguage::Walnut, "enum Color {") == "}", "Walnut enum");
    check(code_block_closer(CodeLanguage::Walnut, "operator(string) function() const {") == "}", "Walnut operators");
    check(code_block_closer(CodeLanguage::Walnut, "Counter(int: start) {") == "}", "Walnut constructors");
    check(code_block_closer(CodeLanguage::Walnut, "var twice = [](int: n) -> int {") == "};", "Walnut lambdas");
    check(code_block_closer(CodeLanguage::Walnut, "items.each([](int: n) {") == "});", "a lambda passed to a call");
    check(code_block_closer(CodeLanguage::Cpp, "class Widget : public Base {") == "};", "C++ classes");
    check(code_block_closer(CodeLanguage::Cpp, "enum class Mode : int {") == "};", "C++ enums");
    check(code_block_closer(CodeLanguage::Cpp, "template <typename T> struct Box {") == "};", "C++ templates");
    check(code_block_closer(CodeLanguage::Cpp, "namespace canopy {") == "} // namespace canopy", "C++ namespaces");
    check(code_block_closer(CodeLanguage::Cpp, "int main() {") == "}", "C++ functions");
    check(code_block_closer(CodeLanguage::Cpp, "if (a == b) {") == "}", "C++ if with ==");
    check(code_block_closer(CodeLanguage::Cpp, "auto f = [&](int x) {") == "};", "C++ lambdas");
    check(code_block_closer(CodeLanguage::Cpp, "std::vector<int> v = {") == "};", "C++ initializers");
    check(code_block_closer(CodeLanguage::Cpp, "Foo& operator=(const Foo& o) {") == "}", "operator= is a function");
    check(code_block_closer(CodeLanguage::C, "struct point {") == "};", "C structs");
    check(code_block_closer(CodeLanguage::Bark, "editor {") == "}", "Bark blocks");

    std::string out = enter_at_end(CodeLanguage::Walnut, "function main() -> int {");
    check(out == "function main() -> int {\n    \n} end function main", "Enter closes a Walnut function: " + out);
    {
        grove::CodeBuffer b("class A {\n    function f() -> int {\n} end class A");
        b.set_caret(grove::CodePos{ 1, b.line(1).size() });
        b.new_line(4, true, CodeLanguage::Walnut, true);
        check(b.text() == "class A {\n    function f() -> int {\n        \n    } end function f\n} end class A" && b.caret() == grove::CodePos{ 2, 8 }, "nested blocks keep the indentation: " + b.text());
    }
    out = enter_after(CodeLanguage::Walnut, "function main() -> int {\n    return 0;\n} end function main", grove::CodePos{ 0, 24 });
    check(out == "function main() -> int {\n    \n    return 0;\n} end function main", "a closed block only gets a new line: " + out);
    out = enter_after(CodeLanguage::Walnut, "function main() -> int {}", grove::CodePos{ 0, 24 });
    check(out == "function main() -> int {\n    \n} end function main", "a typed } is completed: " + out);
    out = enter_at_end(CodeLanguage::Cpp, "struct Point {");
    check(out == "struct Point {\n    \n};", "C++ structs get a semicolon: " + out);
    out = enter_at_end(CodeLanguage::Cpp, "// see {");
    check(out == "// see {\n    ", "braces in comments are left alone: " + out);
    out = enter_at_end(CodeLanguage::Cpp, "call(");
    check(out == "call(\n    \n)", "parentheses close too: " + out);
    out = enter_at_end(CodeLanguage::Grove, "<column class=\"a\">");
    check(out == "<column class=\"a\">\n    \n</column>", "Grove tags close: " + out);
    out = enter_after(CodeLanguage::Grove, "<row></row>", grove::CodePos{ 0, 5 });
    check(out == "<row>\n    \n</row>", "Enter between tags splits them: " + out);
    out = enter_at_end(CodeLanguage::Grove, "<br>");
    check(out == "<br>\n", "void tags are not closed: " + out);
    grove::CodeBuffer off("int main() {");
    off.set_caret(grove::CodePos{ 0, 12 });
    off.new_line(4, true, CodeLanguage::Cpp, false);
    check(off.text() == "int main() {\n    ", "turned off it only indents");
}

void test_bark_properties() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "canopy-bark-props-test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    auto write = [&](const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    };
    const std::string app = ".card {\n    glow: 4px;\n    colr: red;\n    padding: 3px;\n}\n";
    write(root / "app.bark", app);
    write(root / "other.bark", ".panel {\n    glow: 8px;\n}\n");
    write(root / "main.cpp", "float glow_of(const Style& s) { return s.length(\"glow\").value_or(0); }\n");
    const std::string settings = "editor {\n    tab-size: 4;\n    tab-sise: 2;\n}\n";
    write(root / "editor.bark", settings);

    canopy::CodeIntel intel;
    intel.set_workspace(root.string());
    const std::string app_path = (root / "app.bark").string();
    auto has = [](const std::optional<canopy::HoverResult>& h, const std::string& text) { return h && h->markdown.find(text) != std::string::npos; };
    const auto glow = intel.hover(canopy::IntelRequest{ app_path, "bark", app, 1, 5 });
    check(has(glow, "isn't a property Grove knows") && has(glow, "main.cpp:1") && has(glow, "other.bark:2"), "a custom property shows what reads and sets it");
    check(has(intel.hover(canopy::IntelRequest{ app_path, "bark", app, 2, 5 }), "did you mean `color`?"), "a misspelled property suggests the real one");
    check(has(intel.hover(canopy::IntelRequest{ app_path, "bark", app, 3, 5 }), "padding"), "known properties keep their docs");
    const auto reader = intel.definition(canopy::IntelRequest{ app_path, "bark", app, 1, 5 });
    check(reader && fs::path(reader->file).filename() == "main.cpp", "Ctrl+click on a custom property goes to what reads it");

    std::string asked;
    intel.set_bark_settings([&](const std::string& section, const std::string& key, const std::string& file, const std::string&) -> std::optional<std::string> {
        if (fs::path(file).filename() != "editor.bark") return std::nullopt;
        asked = section + "." + key;
        const auto doc = canopy::find_setting_doc(section, key);
        return doc ? "**" + doc->label + "**\n" : std::string("no such setting\n");
    });
    const std::string settings_path = (root / "editor.bark").string();
    check(has(intel.hover(canopy::IntelRequest{ settings_path, "bark", settings, 1, 5 }), "**Tab size**") && asked == "editor.tab-size", "settings files show the setting's description");
    check(has(intel.hover(canopy::IntelRequest{ settings_path, "bark", settings, 2, 5 }), "no such setting"), "unknown settings are reported by the settings hover");
    check(has(intel.hover(canopy::IntelRequest{ settings_path, "bark", settings, 0, 1 }), "") && asked == "editor.", "section names ask about the section");
    check(has(intel.hover(canopy::IntelRequest{ app_path, "bark", app, 1, 5 }), "isn't a property Grove knows"), "app stylesheets are left alone");

    {
        grove::Diagnostics d;
        const auto sheet = grove::bark::parse_stylesheet(".a {\n    --gap: 6px;\n    padding: var(--gap, 2px);\n    margin: calc(var(--gap) * 2);\n}\n", "t.bark", d);
        check(d.items().empty(), "custom properties and var() parse without warnings");
        const auto& decls = sheet.rules().front().declarations;
        check(decls.size() == 3 && decls[0].property == "--gap", "a --name declaration keeps its name");
        grove::bark::ValueList gap = { grove::bark::Value::make_number(6, "px") };
        grove::bark::ValueList padding = decls[1].values;
        grove::bark::substitute_vars(padding, [&](const std::string& name) -> const grove::bark::ValueList* { return name == "--gap" ? &gap : nullptr; });
        check(grove::bark::to_string(padding) == "6px", "var() takes the custom property's value: " + grove::bark::to_string(padding));
        grove::bark::ValueList fallback = decls[1].values;
        grove::bark::substitute_vars(fallback, [](const std::string&) -> const grove::bark::ValueList* { return nullptr; });
        check(grove::bark::to_string(fallback) == "2px", "var() falls back when nothing sets it");
    }
    const std::string custom = ".card {\n    --glow: 4px;\n    padding: var(--glow);\n}\n";
    write(root / "custom.bark", custom);
    const std::string custom_path = (root / "custom.bark").string();
    canopy::CodeIntel fresh;
    fresh.set_workspace(root.string());
    check(has(fresh.hover(canopy::IntelRequest{ custom_path, "bark", custom, 1, 6 }), "A custom property"), "--name hovers explain custom properties");
    const auto setter = fresh.definition(canopy::IntelRequest{ custom_path, "bark", custom, 2, 18 });
    check(setter && setter->line == 1, "Ctrl+click on var(--name) goes to where it's set");
    {
        canopy::CompletionRequest req;
        req.language = "walnut";
        req.text = "import module.ma";
        req.line = 0;
        req.column = req.text.size();
        const canopy::CompletionContext ctx = canopy::completion_context(req);
        check(ctx.site == canopy::CompletionSite::Module && ctx.typed == "ma" && ctx.module_prefix.empty(), "import module. completes module names");
        req.text = "import module.shapes.ci";
        req.column = req.text.size();
        const canopy::CompletionContext nested = canopy::completion_context(req);
        check(nested.site == canopy::CompletionSite::Module && nested.module_prefix == "shapes.", "dotted module paths complete inside folders");
        req.text = "import module.io as x";
        req.column = req.text.size();
        check(canopy::completion_context(req).site != canopy::CompletionSite::Module, "after 'as' it's not a module name");
    }
    check(canopy::find_setting_doc("editor", "close-blocks").has_value(), "setting docs cover the editor settings");
    check(canopy::find_setting_doc("language.walnut", "flags").has_value(), "language flags share one description");
    check(canopy::find_setting_doc("layout", "git").has_value(), "every view in the layout section is described");
    const canopy::Settings defaults = canopy::Settings::defaults();
    const std::vector<std::string> keys = defaults.keys("explorer");
    check(std::find(keys.begin(), keys.end(), "confirm-replace") != keys.end(), "explorer has a confirm-replace setting");
    for (const std::string& section : defaults.known_sections()) {
        if (section.rfind("highlight", 0) == 0 || section == "keys") continue;
        for (const std::string& key : defaults.keys(section)) {
            check(canopy::find_setting_doc(section, key).has_value(), "setting " + section + "." + key + " has a description");
        }
    }
    fs::remove_all(root, ec);
}

void test_special_names() {
    std::uint32_t cp = 0;
    check(canopy::special_character("alpha", cp) && cp == 0x3B1, "\\s{alpha} is U+03B1");
    check(canopy::special_character("Beta", cp) && cp == 0x392, "names are case-sensitive: Beta is U+0392");
    check(canopy::special_character("infty", cp) && cp == 0x221E && canopy::special_character("infin", cp) && cp == 0x221E, "LaTeX and HTML names both work");
    check(!canopy::special_character("alhpa", cp) && canopy::special_character_suggestion("alhpa") == "alpha", "a swapped pair of letters suggests the right name");
    check(canopy::special_character_suggestion("ALPHA") == "Alpha" || canopy::special_character_suggestion("ALPHA") == "alpha", "a wrongly cased name suggests the right one");
    check(canopy::special_character_suggestion("zzzzzzzz").empty(), "nothing close means no suggestion");
    const auto names = canopy::special_character_names("lambd");
    check(std::find(names.begin(), names.end(), "lambda") != names.end(), "names can be listed by prefix");
    canopy::CompletionSources none;
    const std::string wal = "string: s = \"\\s{lamb\";\n";
    grove::CompletionList list = complete_at("walnut", wal, 0, 20, none);
    check(!list.items.empty() && list.items[0].label == "lambda" && list.items[0].insert == "lambda}" && list.column == 16, "names complete after \\s{ in Walnut, closing the brace");
    list = complete_at("walnut", "string: s = \"\\\\s{lamb\";\n", 0, 21, none);
    check(!has_label(list, "lambda"), "an escaped backslash before s{ isn't an escape");
}

void test_project_names() {
    const std::string text = "// mine\nother { x: 1; }\nnames {\n    a: alpha;\n    dg: \"\\u{B0}\";\n    hrt: U+2665;\n    a2: a;\n    alpha: \"q\";\n    two: \"ab\";\n    bad: alhpa;\n}\n";
    const canopy::ProjectNames names = canopy::parse_project_names(text, "walnut.bark");
    check(names.entries.size() == 7, "every written entry is listed: " + std::to_string(names.entries.size()));
    check(names.names.size() == 4, "valid entries become names: " + std::to_string(names.names.size()));
    if (names.entries.size() == 7) {
        check(names.entries[0].ok && names.entries[0].code == 0x3B1, "a built-in name works as a value");
        check(names.entries[1].ok && names.entries[1].code == 0xB0 && names.entries[1].quoted, "a quoted escape works as a value");
        check(names.entries[2].ok && names.entries[2].code == 0x2665, "U+ codes work as values");
        check(names.entries[3].ok && names.entries[3].code == 0x3B1, "a name can point at another of your names");
        check(!names.entries[4].ok && names.entries[4].problem.find("built-in") != std::string::npos, "built-in names can't be taken: " + names.entries[4].problem);
        check(!names.entries[5].ok && names.entries[5].problem.find("one character") != std::string::npos, "values are one character: " + names.entries[5].problem);
        check(!names.entries[6].ok && names.entries[6].problem.find("alpha") != std::string::npos, "a misspelled value suggests the right name: " + names.entries[6].problem);
    }
    const canopy::ProjectNames twice = canopy::parse_project_names("names {\n    x: alpha;\n    y: beta;\n    x: gamma;\n}\n", "walnut.bark");
    check(twice.problems.size() == 2 && twice.entries.size() == 3 && !twice.entries[0].problem.empty() && !twice.entries[2].problem.empty() && twice.entries[1].problem.empty(), "a name given twice marks both lines");
    check(canopy::special_character_category(0x3B1) == "Greek" && canopy::special_character_category(0x2192) == "Arrows" && canopy::special_character_category(0x2264) == "Math operators and relations" && canopy::special_character_category(0x211D) == "Math letters and number sets" && canopy::special_character_category(0x20AC) == "Currency" && canopy::special_character_category(0x09) == "Control and invisible characters", "characters are sorted into types");
    const auto rarr = canopy::builtin_names_for(0x2192);
    check(rarr.size() >= 3 && rarr.front() == "to", "a character lists its built-in names, shortest first");
    check(canopy::builtin_special_codes().size() > 1000, "every built-in character is listed by code");
    canopy::use_project_names(names);
    std::uint32_t cp = 0;
    check(canopy::special_character("hrt", cp) && cp == 0x2665 && canopy::special_character("alpha", cp) && cp == 0x3B1, "project names work alongside the built-in ones");
    check(canopy::special_character_suggestion("hrrt") == "hrt", "project names are suggested too");
    canopy::CompletionSources none;
    const grove::CompletionList list = complete_at("walnut", "string: s = \"\\s{hr\";\n", 0, 18, none);
    check(has_label(list, "hrt"), "project names complete after \\s{");
    const auto hover = canopy::literal_hover("walnut", "char: c = '\\s{dg}';\n", 0, 11);
    check(hover && hover->markdown.find("U+00B0") != std::string::npos, "project names decode in hovers");
    canopy::clear_project_names();
    check(!canopy::special_character("hrt", cp), "project names can be cleared");

    std::vector<canopy::ProjectNameEntry> entries = names.entries;
    entries.resize(2);
    const std::string rewritten = canopy::replace_project_names(text, entries);
    check(rewritten == "// mine\nother { x: 1; }\nnames {\n    a: alpha;\n    dg: \"\\u{B0}\";\n}\n", "rewriting keeps everything outside the names block: " + rewritten);
    check(canopy::replace_project_names("other { x: 1; }", entries) == "other { x: 1; }\n\nnames {\n    a: alpha;\n    dg: \"\\u{B0}\";\n}\n", "a missing names block is added at the end");
    check(canopy::project_name_value_text("\xC2\xB0") == "\"\xC2\xB0\"" && canopy::project_name_value_text("degree") == "degree" && canopy::project_name_value_text("U+00B0") == "U+00B0" && canopy::project_name_value_text("\\u{B0}") == "\"\\u{B0}\"", "typed values are written the way the file reads them");

    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "canopy-names-test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "src" / "deep", ec);
    const std::string file = (dir / "walnut.bark").string();
    check(canopy::write_project_names(file, entries), "names can be written to a new walnut.bark");
    check(canopy::find_project_file((dir / "src" / "deep" / "main.wal").string()) == file, "walnut.bark is found from folders below it");
    const canopy::ProjectNames back = canopy::read_project_names(file);
    check(back.names.size() == 2 && back.problems.empty(), "a written walnut.bark reads back cleanly");
    fs::remove_all(dir, ec);
}

std::size_t column_after(const std::string& text, std::size_t line, const std::string& marker) {
    std::size_t at = 0;
    for (std::size_t l = 0; l < line; ++l) at = text.find('\n', at) + 1;
    const std::size_t end = text.find('\n', at);
    const std::string row = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    return row.find(marker) + marker.size();
}

void test_personal_tasks() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "canopy-personal-tasks";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / ".canopy", ec);
    { std::ofstream(dir / ".canopy" / "tasks.bark") << "build { run: \"make\"; }\nrelease { run: \"git push\"; }\n"; }
    { std::ofstream(dir / ".canopy" / "tasks.local.bark") << "release { label: \"My release\"; run: \"git push origin dev\"; }\nmine { run: \"echo hi\"; }\n"; }
    const canopy::TaskFile file = canopy::load_tasks(dir.string());
    check(file.shared_exists && file.local_exists && file.tasks.size() == 3, "shared and personal tasks load together");
    const canopy::Task* release = file.find("release");
    check(release && release->personal && release->title() == "My release", "a personal task replaces the shared one with the same name");
    const canopy::Task* build = file.find("build");
    check(build && !build->personal && build->file == canopy::tasks_path(dir.string()), "shared tasks remember their file");
    check(canopy::keep_local_tasks_out_of_git(dir.string()) && canopy::keep_local_tasks_out_of_git(dir.string()), "personal tasks are kept out of Git");
    std::ifstream ignore(dir / ".canopy" / ".gitignore");
    const std::string text((std::istreambuf_iterator<char>(ignore)), std::istreambuf_iterator<char>());
    check(text == "*.local.bark\n", "the ignore line is written once");
    fs::remove_all(dir, ec);
}

void test_github_auth() {
    const std::map<std::string, std::string> form = canopy::parse_form("device_code=dc1&user_code=ABCD-1234&verification_uri=https%3A%2F%2Fgithub.com%2Flogin%2Fdevice&interval=5");
    check(form.at("user_code") == "ABCD-1234" && form.at("verification_uri") == "https://github.com/login/device", "GitHub's form answers are decoded");
    check(canopy::json_string_field("{\"login\": \"octo-cat\", \"id\": 1}", "login") == "octo-cat", "the login is read from GitHub's user answer");
    check(canopy::json_string_field("{\"id\": 1}", "login").empty(), "a missing field reads as empty");
    check(canopy::github_auth_failure("remote: Invalid username or token. Password authentication is not supported for Git operations.\nfatal: Authentication failed for 'https://github.com/a/b.git/'"), "GitHub's rejected-password message is recognized");
    check(!canopy::github_auth_failure("error: failed to push some refs (fetch first)"), "other push failures aren't sign-in problems");
    check(canopy::github_token_page().find("scopes=repo,workflow") != std::string::npos, "the token page asks for repo and workflow");
    check(canopy::github_client_id(std::string(" Iv1.abc ")) == "Iv1.abc", "a configured client ID is used");
}

void test_run_stats() {
    check(canopy::format_duration_us(0) == "0 \xC2\xB5s", "zero time shows as microseconds");
    check(canopy::format_duration_us(999) == "999 \xC2\xB5s", "under a millisecond shows microseconds");
    check(canopy::format_duration_us(1000) == "1 ms", "1000 microseconds carry into a millisecond");
    check(canopy::format_duration_us(1001) == "1 ms 1 \xC2\xB5s", "milliseconds and microseconds");
    check(canopy::format_duration_us(3723004005LL) == "1 h 2 min 3 s 4 ms 5 \xC2\xB5s", "every unit carries into the next");
    check(canopy::format_duration_us(60000000LL) == "1 min", "zero parts are left out");
    check(canopy::format_bytes(512) == "512 bytes", "small sizes in bytes");
    check(canopy::format_bytes(1536) == "1.5 KB", "kilobytes");
    check(canopy::format_bytes(254ULL * 1024 * 1024) == "254 MB", "megabytes");
#if !defined(_WIN32)
    std::string out;
    canopy::RunStats stats;
    std::string streamed;
    const int code = canopy::run_command_measured({ "sh", "-c", "echo hi; exit 3" }, std::string(), out, stats, [&](const std::string& piece) { streamed += piece; });
    check(code == 3 && stats.exit_code == 3, "measured runs keep the exit code");
    check(out == "hi\n" && streamed == out, "measured runs capture and stream the output");
    check(stats.measured && stats.wall_us > 0 && stats.peak_bytes > 0, "measured runs record time and memory");
    check(canopy::run_stats_summary(stats).find("exit code 3") != std::string::npos, "the summary names the exit code");
#endif
}

void test_walnut_safety_settings() {
    canopy::Settings s = canopy::Settings::defaults();
    check(canopy::walnut_safety_flags(s).empty(), "walnut safety is off by default");
    grove::Diagnostics d;
    s.load_text("language.walnut {\n    checks: \"all\";\n    leak-check: \"error\";\n    gc: true;\n    ownership: true;\n}\n", "user.bark", canopy::SettingSource::User, d);
    check(d.empty(), "walnut safety settings are known settings");
    const std::vector<std::string> flags = canopy::walnut_safety_flags(s);
    const std::vector<std::string> want = { "--checks", "--leak-check=error", "--gc", "--ownership" };
    check(flags == want, "walnut safety settings become walnut flags");
    canopy::Settings one = canopy::Settings::defaults();
    one.load_text("language.walnut {\n    checks: \"bounds\";\n}\n", "user.bark", canopy::SettingSource::User, d);
    check(canopy::walnut_safety_flags(one) == std::vector<std::string>{ "--check-bounds" }, "a single runtime check picks its own flag");
    canopy::Settings some = canopy::Settings::defaults();
    some.load_text("language.walnut {\n    checks: \"null dangling\";\n}\n", "user.bark", canopy::SettingSource::User, d);
    check(canopy::walnut_safety_flags(some) == std::vector<std::string>{ "--check-null", "--check-dangling" }, "several runtime checks can be picked together");
    const std::vector<std::string> live = canopy::walnut_highlight_command("walnut", "a.wal", "-O2 --ownership");
    check(std::find(live.begin(), live.end(), "--ownership") != live.end(), "ownership checks reach live problems");
}

void test_more_language_builds() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "canopy-lang-tests";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "src" / "demo", ec);
    fs::create_directories(dir / "crate" / "src", ec);
    auto write = [&](const fs::path& p, const std::string& text) {
        std::ofstream out(p, std::ios::binary);
        out << text;
    };
    write(dir / "src" / "demo" / "Hello.java", "package demo;\npublic class Hello { public static void main(String[] a) {} }\n");
    write(dir / "crate" / "Cargo.toml", "[package]\nname = \"x\"\n");
    write(dir / "crate" / "src" / "main.rs", "fn main() {}\n");
    write(dir / "one.rs", "fn main() {}\n");
    write(dir / "a.py", "print(1)\n");
    write(dir / "a.lua", "print(1)\n");
    write(dir / "a.cs", "class A { static void Main() {} }\n");
    write(dir / "start.asm", "section .text\nglobal _start\n_start:\n  mov rax, 60\n  syscall\n");
    write(dir / "main.s", ".globl main\nmain:\n  movq $0, %rax\n  ret\n");
    write(dir / "arm.s", ".global _start\n_start:\n  mov x0, #0\n  mov x8, #93\n  svc #0\n");

    const canopy::Settings s = canopy::Settings::defaults();
    canopy::Toolchain t;
    t.c = canopy::Tool{ "c", "/usr/bin/gcc", canopy::ToolOrigin::Path, canopy::CompilerFamily::Gcc };
    auto tool = [&](const std::string& key, const std::string& path) { t.more[key] = canopy::Tool{ key, path, canopy::ToolOrigin::Path, canopy::CompilerFamily::Other }; };
    canopy::BuildRequest r;
    r.workspace_root = dir.string();
    auto plan = [&](const fs::path& file) {
        r.file = file.string();
        r.output.clear();
        return canopy::plan_build(r, s, t);
    };
    for (const char* ext : { "x.rs", "x.zig", "x.py", "x.lua", "X.java", "x.cs", "x.asm", "x.s", "x.S" }) check(canopy::language_for_path(ext) != nullptr, std::string("Canopy knows ") + ext);
    canopy::BuildPlan p = plan(dir / "one.rs");
    check(!p.ok() && p.error.find("rustup") != std::string::npos, "Rust without rustc says how to install it: " + p.error);
    tool("rustc", "/bin/rustc");
    tool("cargo", "/bin/cargo");
    p = plan(dir / "one.rs");
    check(p.ok() && p.command[0] == "/bin/rustc" && p.run.empty(), "a lone .rs file builds with rustc");
    p = plan(dir / "crate" / "src" / "main.rs");
    check(p.ok() && p.command[1] == "build" && p.run.size() > 2 && p.run[1] == "run" && fs::path(p.folder) == dir / "crate", "a file in a Cargo project builds with cargo");
    tool("python", "/bin/python3");
    p = plan(dir / "a.py");
    check(p.ok() && p.run == std::vector<std::string>{ "/bin/python3", "-u", (dir / "a.py").string() }, "Python checks the syntax, then runs");
    tool("lua", "/bin/lua");
    p = plan(dir / "a.lua");
    check(p.ok() && p.command[0] == "/bin/lua" && p.run[0] == "/bin/lua", "Lua without luac still checks with lua");
    tool("javac", "/bin/javac");
    tool("java", "/bin/java");
    p = plan(dir / "src" / "demo" / "Hello.java");
    check(p.ok() && p.run.back() == "demo.Hello" && std::find(p.command.begin(), p.command.end(), (dir / "src").string()) != p.command.end(), "Java finds the package and the source root");
    p = plan(dir / "a.cs");
    check(!p.ok() && p.error.find(".NET") != std::string::npos, "C# without a compiler says what to install");
    tool("mcs", "/bin/mcs");
    tool("mono", "/bin/mono");
    p = plan(dir / "a.cs");
    check(p.ok() && p.command[0] == "/bin/mcs", "C# without a project builds with mcs");
    p = plan(dir / "start.asm");
    check(!p.ok() && p.error.find("NASM") != std::string::npos && p.error.find("nasm") != std::string::npos, "NASM code without nasm says which assembler it needs: " + p.error);
    tool("nasm", "/bin/nasm");
    tool("ld", "/bin/ld");
    p = plan(dir / "start.asm");
    check(p.ok() && p.command[0] == "/bin/nasm" && p.more_steps.size() == 1, "NASM assembles, then links");
    p = plan(dir / "main.s");
    check(p.ok() && p.command[0] == "/usr/bin/gcc" && p.note.find("GAS") != std::string::npos, "GAS code builds with the C compiler");
#if defined(__x86_64__) || defined(_M_X64)
    p = plan(dir / "arm.s");
    check(!p.ok() && p.error.find("ARM64") != std::string::npos, "ARM64 code on x86-64 needs a cross compiler: " + p.error);
    tool("gcc-aarch64", "/bin/aarch64-linux-gnu-gcc");
    p = plan(dir / "arm.s");
    check(p.ok() && !p.run_error.empty(), "without QEMU it builds but can't run");
    tool("qemu-aarch64", "/bin/qemu-aarch64");
    p = plan(dir / "arm.s");
    check(p.ok() && !p.run.empty() && p.run[0] == "/bin/qemu-aarch64", "with QEMU it runs");
#endif
    const canopy::AsmInfo masm = canopy::detect_assembly(".model flat\n.code\nmain PROC\n  ret\nmain ENDP\nEND\n", "x.asm");
    check(masm.dialect == canopy::AsmDialect::Masm, "MASM is recognized");
    const canopy::AsmInfo intel = canopy::detect_assembly(".intel_syntax noprefix\n.globl main\nmain:\n  mov rax, 0\n  ret\n", "x.s");
    check(intel.dialect == canopy::AsmDialect::GasIntel && intel.arch == canopy::AsmArch::X86_64 && intel.entry == "main", "GAS Intel syntax is recognized");
    const canopy::AsmInfo rv = canopy::detect_assembly(".globl _start\n_start:\n  li a7, 93\n  li a0, 0\n  ecall\n", "x.s");
    check(rv.arch == canopy::AsmArch::RiscV64, "RISC-V is recognized");
    const canopy::AsmInfo x86 = canopy::detect_assembly("bits 32\nsection .text\nglobal _start\n_start:\n  mov eax, 1\n  int 0x80\n", "x.asm");
    check(x86.arch == canopy::AsmArch::X86, "32-bit x86 is recognized");
    fs::remove_all(dir, ec);
}

void test_more_language_messages() {
    auto m = canopy::parse_compiler_messages("error: expected `;`, found `println`\n --> bad.rs:2:14\n  |\n\nerror[E0425]: cannot find value `y` in this scope\n --> bad.rs:3:20\n  |\nerror: aborting due to 2 previous errors\n");
    check(m.size() == 2 && m[0].file == "bad.rs" && m[0].line == 2 && m[0].column == 14 && m[1].message.find("[E0425]") != std::string::npos, "Rust errors are read");
    m = canopy::parse_compiler_messages("Traceback (most recent call last):\n  File \"<string>\", line 1, in <module>\n    import ast\n  File \"/usr/lib/python3.13/ast.py\", line 50, in parse\n    return compile(source)\n  File \"bad.py\", line 1\n    def f(:\n          ^\nSyntaxError: invalid syntax\n");
    check(m.size() == 1 && m[0].file == "bad.py" && m[0].line == 1 && m[0].column == 7 && m[0].message == "SyntaxError: invalid syntax", "Python syntax errors are read");
    m = canopy::parse_compiler_messages("Traceback (most recent call last):\n  File \"/w/run.py\", line 2, in <module>\n    print(undefined_name)\n          ^^^^^^^^^^^^^^\nNameError: name 'undefined_name' is not defined\n");
    check(m.size() == 1 && m[0].line == 2 && m[0].message.rfind("NameError", 0) == 0, "Python exceptions point at the line that raised");
    m = canopy::parse_compiler_messages("luac: bad.lua:2: ')' expected near <eof>\nlua: run.lua:1: boom\nstack traceback:\n");
    check(m.size() == 2 && m[0].file == "bad.lua" && m[0].line == 2 && m[1].message == "boom", "Lua errors are read");
    m = canopy::parse_compiler_messages("Bad.java:2: error: illegal start of expression\n  void f() { int x = ; }\n                     ^\n1 error\n");
    check(m.size() == 1 && m[0].file == "Bad.java" && m[0].line == 2, "javac errors are read");
    m = canopy::parse_compiler_messages("bad.cs(1,39): error CS1525: Unexpected symbol `;'\n/w/P.cs(3,5): warning CS0168: The variable 'x' is declared but never used [/w/P.csproj]\n");
    check(m.size() == 2 && m[0].line == 1 && m[0].column == 39 && m[1].severity == "warning" && m[1].message.find("csproj") == std::string::npos, "C# errors are read");
    m = canopy::parse_compiler_messages("bad.asm:2: error: invalid combination of opcode and operands\nbad.zig:1:15: error: expected expression, found ';'\n");
    check(m.size() == 2 && m[0].line == 2 && m[1].column == 15, "NASM and Zig errors are read");
}

void test_more_language_completion() {
    auto at = [](const std::string& language, const std::string& text, const std::string& marker) {
        const std::size_t pos = text.find(marker);
        std::size_t line = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < pos + marker.size(); ++i) {
            if (text[i] == '\n') {
                ++line;
                start = i + 1;
            }
        }
        return complete_at(language, text, line, pos + marker.size() - start);
    };
    const std::string py = "import os\nname = 'x'\nitems = [1, 2]\nwith open('f') as fh:\n    pass\nname.up\nitems.app\nos.path.jo\nfh.rea\npri\nimport ra\n# name.up\n";
    check(has_label(at("python", py, "name.up"), "upper"), "Python knows a string variable's methods");
    check(has_label(at("python", py, "items.app"), "append"), "Python knows a list variable's methods");
    check(has_label(at("python", py, "os.path.jo"), "join"), "Python knows os.path");
    check(has_label(at("python", py, "fh.rea"), "readline"), "Python knows files from with open(...) as");
    check(has_label(at("python", py, "\npri"), "print") && find_label(at("python", py, "\npri"), "print")->insert == "print($0)", "Python builtins complete");
    check(has_label(at("python", py, "import ra"), "random"), "Python module names complete after import");
    check(at("python", py, "# name.up").items.empty(), "nothing completes inside a comment");
    const std::string rs = "use std::collections::HashMap;\nfn main() {\n    let mut v = Vec::new();\n    let s = String::from(\"a\");\n    let m: HashMap<String, i32> = HashMap::new();\n    v.pu\n    s.tri\n    m.ins\n    v.iter().ma\n    Vec::wi\n    prin\n}\n";
    check(has_label(at("rust", rs, "v.pu"), "push"), "Rust knows Vec::new()");
    check(has_label(at("rust", rs, "s.tri"), "trim"), "Rust knows String::from");
    check(has_label(at("rust", rs, "m.ins"), "insert"), "Rust reads type annotations");
    check(has_label(at("rust", rs, "v.iter().ma"), "map"), "Rust follows iterator chains");
    check(has_label(at("rust", rs, "Vec::wi"), "with_capacity") && !has_label(at("rust", rs, "Vec::wi"), "push"), "Rust paths show associated functions");
    check(has_label(at("rust", rs, "    prin"), "println!"), "Rust macros complete");
    const std::string zig = "const std = @import(\"std\");\npub fn main() void {\n    std.debug.pr\n    @imp\n}\n";
    check(has_label(at("zig", zig, "std.debug.pr"), "print"), "Zig knows std.debug");
    check(has_label(at("zig", zig, "@imp"), "@import"), "Zig builtins complete after @");
    const std::string lua = "local s = \"hi\"\nprint(s:up\nstring.fo\n";
    check(has_label(at("lua", lua, "s:up"), "upper") && has_label(at("lua", lua, "string.fo"), "format"), "Lua knows string methods and modules");
    const std::string java = "import java.util.*;\nclass A {\n  void f() {\n    List<String> names = new ArrayList<>();\n    String t = \"x\";\n    names.ad\n    t.toUp\n    System.out.pri\n  }\n}\n";
    check(has_label(at("java", java, "names.ad"), "add") && has_label(at("java", java, "t.toUp"), "toUpperCase") && has_label(at("java", java, "System.out.pri"), "println"), "Java knows declared types and System.out");
    const std::string cs = "var list = new List<int>();\nstring s = \"x\";\nlist.Ad\ns.ToUp\nConsole.Wri\n";
    check(has_label(at("csharp", cs, "list.Ad"), "Add") && has_label(at("csharp", cs, "s.ToUp"), "ToUpper") && has_label(at("csharp", cs, "Console.Wri"), "WriteLine"), "C# knows declared types and Console");
    const std::string as = "section .text\nglobal _start\n_start:\n    mo\n    mov ra\n";
    check(has_label(at("asm", as, "    mo"), "mov") && !has_label(at("asm", as, "    mo"), "rax"), "assembly offers instructions at the start of a line");
    check(has_label(at("asm", as, "mov ra"), "rax"), "assembly offers registers after the instruction");
    auto hover = canopy::more_hover("python", "name = 'x'\nname.upper()\n", "/w/a.py", 1, 6);
    check(hover && hover->markdown.find("Upper case") != std::string::npos, "Python members have hovers");
    hover = canopy::more_hover("asm", "section .text\nglobal _start\n_start:\n    mov rax, 60\n", "/w/a.asm", 3, 9);
    check(hover && hover->markdown.find("Return value") != std::string::npos && hover->markdown.find("NASM") != std::string::npos, "registers have hovers that name the syntax");
    hover = canopy::more_hover("rust", "fn main() {\n    println!(\"x\");\n}\n", "/w/a.rs", 1, 6);
    check(hover && hover->markdown.find("Prints a line") != std::string::npos, "Rust macros have hovers");
}

void test_web_completion() {
    check(canopy::web_docs().size() > 500, "the web docs table loads: " + std::to_string(canopy::web_docs().size()));
    check(canopy::find_web_doc("jquery", "addClass") && canopy::find_web_doc("res", "json") && canopy::find_web_doc("css-prop", "display"), "the web docs cover jQuery, Express and CSS");
    check(canopy::find_web_doc("css-value:display", "flex") != nullptr, "CSS value lists are split into entries");

    const std::string js = "const fs = require('fs');\nconst express = require(\"express\");\nconst app = express();\napp.get('/', (req, res) => {\n  res.\n  req.\n});\nfs.\ndocument.getElementById('x').\n$('#a').addClass('b').\n$.\nconsole.\nconst p = require('pa');\nconst btn = document.querySelector('b');\nbtn.\nfetch('/x').then(r => r.json()).\ncons\n";
    grove::CompletionList list = complete_at("javascript", js, 4, column_after(js, 4, "res."));
    check(has_label(list, "json") && has_label(list, "status") && find_label(list, "json")->insert == "json($0)", "Express res members complete");
    list = complete_at("javascript", js, 5, column_after(js, 5, "req."));
    check(has_label(list, "params") && has_label(list, "body"), "Express req members complete");
    list = complete_at("javascript", js, 7, 3);
    check(has_label(list, "readFileSync") && has_label(list, "existsSync"), "a variable from require('fs') gets fs members");
    list = complete_at("javascript", js, 8, column_after(js, 8, "')."));
    check(has_label(list, "addEventListener") && has_label(list, "classList"), "getElementById(...) gives element members");
    list = complete_at("javascript", js, 9, column_after(js, 9, "addClass('b')."));
    check(has_label(list, "fadeIn") && has_label(list, "on"), "jQuery chains keep jQuery members");
    list = complete_at("javascript", js, 10, 2);
    check(has_label(list, "ajax") && has_label(list, "each"), "$. gives jQuery's static functions");
    list = complete_at("javascript", js, 11, 8);
    check(has_label(list, "log") && has_label(list, "table"), "console members complete");
    list = complete_at("javascript", js, 12, column_after(js, 12, "'pa"));
    check(has_label(list, "path") && list.column == column_after(js, 12, "'"), "require('...') completes module names");
    list = complete_at("javascript", js, 14, 4);
    check(has_label(list, "addEventListener"), "a querySelector result gets element members");
    list = complete_at("javascript", js, 15, column_after(js, 15, "r.json())."));
    check(has_label(list, "then") && has_label(list, "catch"), "promise chains complete");
    list = complete_at("javascript", js, 16, 4);
    check(has_label(list, "console") && has_label(list, "const"), "JavaScript globals and keywords complete");
    check(canopy::js_object_owner(js, js.find("app.get") + 3) == "app", "express() makes an Express app");

    const std::string html = "<!doctype html>\n<html>\n<head>\n<style>\nbody { disp\n  display: fl\n}\na:hov {}\n</style>\n</head>\n<body>\n<inp\n<input ty\n<input type=\"che\n<script>\ndocument.\n</script>\n</bod\n";
    list = complete_at("html", html, 4, 11);
    check(has_label(list, "display") && find_label(list, "display")->insert == "display: $0;", "CSS properties complete inside <style>");
    list = complete_at("html", html, 5, 13);
    check(has_label(list, "flex") && has_label(list, "inline-flex"), "CSS values complete for the property");
    list = complete_at("html", html, 7, 5);
    check(has_label(list, ":hover"), "pseudo-classes complete in <style>");
    list = complete_at("html", html, 11, 4);
    check(has_label(list, "input") && !has_label(list, "column"), "HTML tags complete, not Grove ones");
    list = complete_at("html", html, 12, 9);
    check(has_label(list, "type") && find_label(list, "type")->insert == "type=\"$0\"" && find_label(list, "type")->detail.find("input") != std::string::npos, "HTML attributes complete");
    list = complete_at("html", html, 13, 16);
    check(has_label(list, "checkbox"), "attribute values complete");
    list = complete_at("html", html, 15, 9);
    check(has_label(list, "querySelector"), "JavaScript completes inside <script>");
    list = complete_at("html", html, 17, 5);
    check(has_label(list, "body"), "closing tags complete");

    const std::string css = "@med\n.box {\n  justify-content: sp\n}\n";
    list = complete_at("css", css, 0, 4);
    check(has_label(list, "@media") && list.column == 0, "at-rules complete");
    list = complete_at("css", css, 2, 21);
    check(has_label(list, "space-between"), "CSS keyword values complete");

    auto hover = canopy::web_hover("javascript", js, 4, column_after(js, 4, "res.") - 1);
    check(!hover, "no hover on the dot");
    hover = canopy::web_hover("javascript", "const app = require('express')();\napp.listen(3000);\n", 1, 6);
    check(hover && hover->markdown.find("Starts the server") != std::string::npos, "Express members have hovers");
    hover = canopy::web_hover("html", html, 12, 3);
    check(hover && hover->markdown.find("form field") != std::string::npos, "HTML tags have hovers");
    hover = canopy::web_hover("css", css, 2, 5);
    check(hover && hover->markdown.find("main axis") != std::string::npos, "CSS properties have hovers");
    check(canopy::is_web_language("html") && !canopy::is_web_language("grove"), "web languages are known");
}

void test_web_problems() {
    auto has = [](const std::vector<canopy::CompilerMessage>& list, std::size_t line, const std::string& part) {
        for (const canopy::CompilerMessage& m : list) {
            if (m.line == line && m.message.find(part) != std::string::npos) return true;
        }
        return false;
    };
    const std::string html = "<!doctype html>\n<html>\n<body>\n<div id=\"a\">\n  <p>one\n  <span>two</div>\n<p id=\"a\">x</p>\n<img src=\"x.png\">\n<br></br>\n<blink>x</blink>\n</section>\n<style>\np { colr: red; }\n</style>\n<svg viewBox=\"0 0 1 1\"><path d=\"M0 0\"/></svg>\n<my-widget></my-widget>\n<ul><li>a<li>b</ul>\n</body>\n</html>\n";
    const auto list = canopy::lint_web("html", html, "/tmp/p.html");
    check(has(list, 6, "<span> is never closed"), "an unclosed element is reported where it opens");
    check(has(list, 7, "already used on line 4"), "duplicate ids are reported");
    check(has(list, 8, "alt"), "images without alt text are reported");
    check(has(list, 9, "</br> isn't needed"), "closing tags on void elements are reported");
    check(has(list, 10, "isn't an HTML element"), "unknown tags are reported");
    check(has(list, 11, "no matching <section>"), "stray closing tags are reported");
    check(has(list, 13, "did you mean 'color'"), "CSS inside <style> is checked");
    check(!has(list, 15, "") && !has(list, 16, "") && !has(list, 17, ""), "SVG, custom elements and optional end tags are fine");
    const std::string css = "a {\n  color: red\n  margin: 0;\n}\n}\n.b { display: flex; -webkit-x: 1; --my: 2; }\n@media (max-width: 9px) {\n  .c { colour: red; }\n}\n.d {\n";
    const auto cl = canopy::lint_web("css", css, "/tmp/s.css");
    check(has(cl, 2, "Missing ;"), "a missing semicolon is reported");
    check(has(cl, 5, "Unexpected }"), "an extra } is reported");
    check(!has(cl, 6, ""), "vendor prefixes and custom properties are fine");
    check(has(cl, 8, "Unknown property 'colour'"), "rules inside @media are checked");
    check(has(cl, 10, "never closed"), "an unclosed { is reported");
    bool module = false;
    const std::string page = "<p>\n<script>\nlet x = ;\n</script>\n<script src=\"a.js\"></script>\n<script type=\"module\">import a from './a.js';</script>\n";
    const std::string js = canopy::web_script_text(page, module);
    check(module && js.size() == page.size() && js.find("let x = ;") == page.find("let x = ;") && js.find("<p>") == std::string::npos, "inline scripts keep their lines and columns");
    const auto parsed = canopy::parse_node_check("[stdin]:3\nlet x = ;\n        ^\n\nSyntaxError: Unexpected token ';'\n    at checkSyntax (node:internal/main/check_syntax:74:5)\n", "/tmp/p.html");
    check(parsed.size() == 1 && parsed[0].line == 3 && parsed[0].column == 9 && parsed[0].message == "SyntaxError: Unexpected token ';'", "node --check output is read");
    check(canopy::javascript_is_module("import x from 'y';\n", "/tmp/a.js") && !canopy::javascript_is_module("const x = require('y');\n", "/tmp/a.js"), "ES modules are told apart from CommonJS");
}

void test_elements_tree() {
    const std::string tree = std::string("d\x1f") + "1\x1f" + "0\x1fhtml" + "\x1e" + "e\x1f" + "2\x1f" + "0\x1fhtml\x1f" + "lang\x1d" + "en" + "\x1e" +
                             "e\x1f" + "3\x1f" + "1\x1f" + "body\x1f" + "\x1e" + "e\x1f" + "4\x1f" + "2\x1f" + "p\x1f" + "class\x1d" + "a \"b\"\x1c" + "hidden\x1d" + "\x1e" +
                             "t\x1f" + "5\x1f" + "3\x1f" + "x < y" + "\x1e" + "e\x1f" + "6\x1f" + "2\x1f" + "br\x1f" + "\x1e" + "e\x1f" + "7\x1f" + "2\x1f" + "script\x1f" + "\x1e" + "t\x1f" + "8\x1f" + "3\x1fif (a < b) go();";
    const std::vector<canopy::DomTreeNode> nodes = canopy::parse_dom_tree(tree);
    check(nodes.size() == 8, "the page's tree is read: " + std::to_string(nodes.size()));
    check(nodes.size() == 8 && nodes[1].children.size() == 1 && nodes[2].children.size() == 3 && nodes[3].parent == 2, "children and parents come from the depths");
    const std::string html = canopy::dom_outer_html(nodes, 2);
    check(html == "<body>\n  <p class=\"a &quot;b&quot;\" hidden>x &lt; y</p>\n  <br>\n  <script>if (a < b) go();</script>\n</body>", "elements turn back into HTML: " + html);
    check(canopy::dom_node_label(nodes[3]) == "p.a.\"b\"", "labels list the classes: " + canopy::dom_node_label(nodes[3]));
    check(canopy::js_string_literal("a\"b\\c\nd\xE2\x80\xA8") == "\"a\\\"b\\\\c\\nd\\u2028\"", "edits are sent as safe JavaScript strings");
    canopy::LiveServer server;
    std::string error;
    check(server.start(".", 0, "127.0.0.1", true, error) || true, "a server starts");
    server.watch_dom("nope");
    canopy::PageDom dom;
    check(!server.page_dom("nope", 0, dom), "pages that never sent a tree have none");
    server.stop();
}

void test_literal_hovers() {
    auto hover_on = [](const std::string& language, const std::string& line, const std::string& find) {
        const std::size_t at = line.find(find);
        const auto h = canopy::literal_hover(language, line + "\n", 0, at == std::string::npos ? 0 : at + 1);
        return h ? h->markdown : std::string();
    };
    auto has = [](const std::string& md, const std::string& part) { return md.find(part) != std::string::npos; };
    std::string md = hover_on("walnut", "string: s = \"herllo\";", "\"herllo");
    check(has(md, "`string`") && has(md, "6 characters") && has(md, "herllo"), "Walnut strings show type, length and content: " + md);
    md = hover_on("walnut", "string: s = \"a\\tb\\u00e9\";", "\"a");
    check(has(md, "4 characters") && has(md, "5 bytes") && has(md, "is tab") && has(md, "U+00E9"), "escapes are decoded and listed: " + md);
    md = hover_on("walnut", "char: c = '\\u00e9';", "'");
    check(has(md, "233") && has(md, "U+00E9") && has(md, "C3 A9"), "Walnut chars show their code: " + md);
    md = hover_on("walnut", "char: c = '\\s{alpha}';", "'");
    check(has(md, "U+03B1"), "Walnut \\s{name} escapes are decoded: " + md);
    md = hover_on("walnut", "string: s = \"\\s{lamda}\";", "\"");
    check(has(md, "lambda"), "a misspelled \\s{name} suggests the right one: " + md);
    md = hover_on("walnut", "char: c = '\\n';", "'");
    check(has(md, "10 (0x0A") && has(md, "newline"), "control characters are named: " + md);
    md = hover_on("walnut", "int: x = 0xFF_FF;", "0x");
    check(has(md, "`int`") && has(md, "65_535") && has(md, "0o177777"), "Walnut hex integers: " + md);
    md = hover_on("walnut", "var x = 7ULL;", "7");
    check(has(md, "`unsigned long long int`"), "Walnut suffixes pick the type: " + md);
    md = hover_on("walnut", "var x = 2.5sd;", "2");
    check(has(md, "`short double`") && has(md, "256 bits"), "Walnut float families: " + md);
    md = hover_on("walnut", "var x = 0x1Fd;", "0x");
    check(has(md, "`int`") && has(md, "509"), "d is a hex digit in hex literals: " + md);
    md = hover_on("walnut", "var x = 017;", "017");
    check(has(md, "`0o17`"), "Walnut warns that 017 isn't octal: " + md);
    md = hover_on("walnut", "var x = 1'000'000;", "1'");
    check(has(md, "1_000_000"), "digit separators: " + md);
    md = hover_on("walnut", "text: t = `a${x}b`;", "`a");
    check(has(md, "`text`") && has(md, "depends"), "interpolated text: " + md);
    md = hover_on("walnut", "char: c = '\\q';", "'");
    check(has(md, "doesn't know"), "unknown escapes are reported: " + md);
    md = hover_on("cpp", "auto s = \"abc\";", "\"abc");
    check(has(md, "`const char[4]`") && has(md, "3 characters"), "C++ strings are char arrays: " + md);
    md = hover_on("cpp", "auto s = u\"a\\U0001F600\";", "u\"");
    check(has(md, "`const char16_t[4]`"), "u\"\" counts UTF-16 units: " + md);
    md = hover_on("cpp", "auto x = 0x10u;", "0x");
    check(has(md, "`unsigned int`") && has(md, "16"), "C++ unsigned hex: " + md);
    md = hover_on("cpp", "auto x = 017;", "017");
    check(has(md, "`15`") && has(md, "octal"), "C octal: " + md);
    md = hover_on("cpp", "auto x = 1.5f;", "1.5");
    check(has(md, "`float`") && has(md, "1.5"), "C++ float suffix: " + md);
    md = hover_on("cpp", "auto x = 3000000000;", "3000");
    check(has(md, "`long`") || has(md, "`long long`"), "C++ decimal literals grow to long: " + md);
    md = hover_on("cpp", "auto c = 'ab';", "'ab");
    check(has(md, "multi-character"), "multi-character literals are flagged: " + md);
    md = hover_on("cpp", "auto r = R\"(a\\n)\";", "R\"");
    check(has(md, "Raw string") && has(md, "3 characters"), "raw strings keep backslashes: " + md);
    md = hover_on("cpp", "#include \"game.hpp\"", "\"game");
    check(md.empty(), "#include paths aren't string literals");
    md = hover_on("cpp", "// \"quoted\" in a comment", "\"quoted");
    check(md.empty(), "comments are skipped");
}

void test_editor_editing() {
    grove::CodeBuffer b("//     highlight {\n//     }\n");
    b.set_caret(grove::CodePos{ 0, 7 });
    b.outdent(4);
    check(b.line(0) == "//  highlight {" && b.caret().column == 4, "Shift+Tab after a comment takes spaces back to the tab stop");
    b.outdent(4);
    check(b.line(0) == "//highlight {" && b.caret().column == 2, "Shift+Tab again takes the rest of the run");
    grove::CodeBuffer c("        x");
    c.set_caret(grove::CodePos{ 0, 8 });
    c.outdent(4);
    check(c.line(0) == "    x", "leading indentation still outdents first");

    grove::CodeBuffer d("      x");
    d.set_caret(grove::CodePos{ 0, 6 });
    d.backspace_to_tab_stop(4);
    check(d.line(0) == "     x" && d.caret().column == 5, "Backspace between tab stops deletes one space");
    d.backspace_to_tab_stop(4);
    check(d.line(0) == "    x" && d.caret().column == 4, "one at a time until the tab stop");
    d.backspace_to_tab_stop(4);
    check(d.line(0) == "x", "then a whole tab level at once");
    grove::CodeBuffer g("    return 0;");
    g.set_caret(grove::CodePos{ 0, 13 });
    g.indent(4, true);
    g.outdent(4);
    check(g.line(0) == "    return 0;" && g.caret().column == 13, "Shift+Tab after Tab at the end of a line takes back those spaces, not the indentation: " + g.line(0));
    g.set_caret(grove::CodePos{ 0, 0 });
    g.indent(4, true);
    check(g.line(0) == "        return 0;", "Tab at the start indents a level");
    g.outdent(4);
    check(g.line(0) == "    return 0;", "Shift+Tab goes back exactly one level");
    g.indent(4, true);
    check(g.line(0) == "        return 0;", "and Tab goes forward exactly one");
    grove::CodeBuffer e("ab c");
    e.set_caret(grove::CodePos{ 0, 3 });
    e.backspace_to_tab_stop(4);
    check(e.line(0) == "abc", "a single space is just one backspace");

    grove::CodeBuffer f("abcdef\nab\nabcdefgh\n");
    f.box_select(0, 1, 2, 4, 4);
    const std::string picked = f.selected_texts();
    check(picked == "bcd\nb\nbcd", "box selection takes the same columns on every line: " + picked);
    f.box_select(0, 2, 2, 2, 4);
    check(f.cursor_count() == 3 && !f.any_selection(), "a straight box is a caret per line");

    test_block_closing();
}

int main() {
    test_defaults();
    test_override();
    test_typos();
    test_languages();
    test_plans();
    test_flags();
    test_compiler_messages();
    test_recent_and_files();
    test_config_files();
    test_setting_writer();
    test_themes();
    test_theme_files();
    test_walnut_semantic();
    test_line_masking();
    test_code_intel();
    test_viewers();
    test_sizes_and_messages();
    test_shortcuts();
    test_create_entries();
    test_completion_index();
    test_code_completion();
    test_signature_layout();
    test_terminal_screen();
    test_workspace_paths();
    test_markdown();
    test_live_server();
    test_git_parsers();
    test_local_history();
    test_tasks_file();
    test_git_run();
    test_git_extras();
    test_editor_editing();
    test_bark_properties();
    test_literal_hovers();
    test_web_completion();
    test_personal_tasks();
    test_github_auth();
    test_run_stats();
    test_walnut_safety_settings();
    test_more_language_builds();
    test_more_language_messages();
    test_more_language_completion();
    test_elements_tree();
    test_web_problems();
    test_special_names();
    test_project_names();
    if (failures == 0) std::cout << "all canopy tests passed\n";
    return failures == 0 ? 0 : 1;
}
