#include <canopy/languages.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace canopy {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string extension_of(const std::string& path) {
    return lower(std::filesystem::path(path).extension().string());
}

} // namespace

const std::vector<Language>& builtin_languages() {
    static const std::vector<Language> languages = {
        { LanguageId::Grove, "grove", "Grove", { ".grove" }, BuildKind::Grove, {} },
        { LanguageId::Bark, "bark", "Bark", { ".bark" }, BuildKind::None, {} },
        { LanguageId::C, "c", "C", { ".c", ".h" }, BuildKind::CCompiler, { ".h" } },
        { LanguageId::Cpp, "cpp", "C++", { ".cpp", ".cc", ".cxx", ".c++", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".tpp" }, BuildKind::CppCompiler, { ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".tpp" } },
        { LanguageId::Walnut, "walnut", "Walnut", { ".wal", ".walh", ".hwal" }, BuildKind::Walnut, { ".walh", ".hwal" } },
        { LanguageId::Html, "html", "HTML", { ".html", ".htm", ".xhtml" }, BuildKind::Web, {} },
        { LanguageId::Css, "css", "CSS", { ".css" }, BuildKind::None, {} },
        { LanguageId::JavaScript, "javascript", "JavaScript", { ".js", ".mjs", ".cjs" }, BuildKind::Node, {} },
        { LanguageId::Json, "json", "JSON", { ".json", ".webmanifest" }, BuildKind::None, {} },
        { LanguageId::Rust, "rust", "Rust", { ".rs" }, BuildKind::Rust, {} },
        { LanguageId::Zig, "zig", "Zig", { ".zig" }, BuildKind::Zig, {} },
        { LanguageId::Python, "python", "Python", { ".py", ".pyw", ".pyi" }, BuildKind::Python, { ".pyi" } },
        { LanguageId::Lua, "lua", "Lua", { ".lua" }, BuildKind::Lua, {} },
        { LanguageId::Java, "java", "Java", { ".java" }, BuildKind::Java, {} },
        { LanguageId::CSharp, "csharp", "C#", { ".cs", ".csx" }, BuildKind::CSharp, {} },
        { LanguageId::Assembly, "asm", "Assembly", { ".asm", ".s", ".nasm", ".masm", ".fasm" }, BuildKind::Assembly, {} },
    };
    return languages;
}

const Language* language_by_key(std::string_view key) {
    for (const Language& l : builtin_languages()) {
        if (l.key == key) return &l;
    }
    return nullptr;
}

const Language* language_for_path(const std::string& path) {
    const std::string ext = extension_of(path);
    if (ext.empty()) return nullptr;
    for (const Language& l : builtin_languages()) {
        if (std::find(l.extensions.begin(), l.extensions.end(), ext) != l.extensions.end()) return &l;
    }
    return nullptr;
}

bool is_header_path(const Language& language, const std::string& path) {
    const std::string ext = extension_of(path);
    return std::find(language.header_extensions.begin(), language.header_extensions.end(), ext) != language.header_extensions.end();
}

} // namespace canopy
