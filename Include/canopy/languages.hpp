#ifndef CANOPY_LANGUAGES_HPP
#define CANOPY_LANGUAGES_HPP

#include <string>
#include <string_view>
#include <vector>

namespace canopy {

enum class LanguageId {
    Grove,
    Bark,
    C,
    Cpp,
    Walnut,
    Unknown
};

enum class BuildKind {
    None,
    CCompiler,
    CppCompiler,
    Walnut,
    Grove
};

struct Language {
    LanguageId id = LanguageId::Unknown;
    std::string key;
    std::string name;
    std::vector<std::string> extensions;
    BuildKind build = BuildKind::None;
    std::vector<std::string> header_extensions;
};

const std::vector<Language>& builtin_languages();
const Language* language_by_key(std::string_view key);
const Language* language_for_path(const std::string& path);
bool is_header_path(const Language& language, const std::string& path);

} // namespace canopy

#endif // CANOPY_LANGUAGES_HPP
