#include <canopy/build.hpp>
#include <canopy/code_intel.hpp>
#include <canopy/language_docs.hpp>
#include <canopy/languages.hpp>
#include <canopy/settings.hpp>
#include <canopy/settings_page.hpp>
#include <canopy/shortcuts.hpp>
#include <canopy/toolchain.hpp>
#include <canopy/viewers.hpp>
#include <canopy/walnut_semantic.hpp>
#include <canopy/workspace.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
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
    check(s.number("editor", "tab-size").value_or(0) == 4, "default tab size is 4");
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
    check(s.number("editor", "tab-size").value_or(0) == 4, "a misspelled key doesn't change anything");
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
    check(canopy::command_line(p.command) == "/usr/bin/g++ -std=c++17 -Wall -Wextra -Wpedantic -g -O0 /work/main.cpp -o /work/build/main", "C++ debug command");

    r.mode = canopy::BuildMode::Release;
    p = canopy::plan_build(r, s, t);
    check(canopy::command_line(p.command).find("-O2") != std::string::npos, "release uses release-flags");

    r = canopy::BuildRequest{};
    r.file = "/work/app.wal";
    r.output = "/work/build/app";
    p = canopy::plan_build(r, s, t);
    check(p.ok() && p.command.at(1) == "build" && p.command.back() == "-g", "Walnut builds through walnut build");

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
    test_code_intel();
    test_viewers();
    test_sizes_and_messages();
    test_shortcuts();
    if (failures == 0) std::cout << "all canopy tests passed\n";
    return failures == 0 ? 0 : 1;
}
