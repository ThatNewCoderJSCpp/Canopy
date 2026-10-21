#ifndef CANOPY_LANG_INTEL_HPP
#define CANOPY_LANG_INTEL_HPP

#include <canopy/code_intel.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

bool is_more_language(const std::string& language);
std::string more_language_prefix(const std::string& language);
bool more_inside_text(const std::string& language, const std::string& text, std::size_t line, std::size_t column);
std::string more_member_owner(const std::string& language, const std::string& text, std::size_t separator, const std::string& separator_text);
std::vector<std::string> more_code_owners(const std::string& language, const std::string& text, const std::string& path, bool line_start);
std::optional<HoverResult> more_hover(const std::string& language, const std::string& text, const std::string& path, std::size_t line, std::size_t column);

} // namespace canopy

#endif // CANOPY_LANG_INTEL_HPP
