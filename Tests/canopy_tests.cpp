#include <canopy/build.hpp>
#include <canopy/languages.hpp>
#include <canopy/settings.hpp>
#include <canopy/toolchain.hpp>

#include <cstdlib>
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

} // namespace

int main() {
    test_defaults();
    test_override();
    test_typos();
    test_languages();
    test_plans();
    test_flags();
    if (failures == 0) std::cout << "all canopy tests passed\n";
    return failures == 0 ? 0 : 1;
}
