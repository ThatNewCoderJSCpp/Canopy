#ifndef CANOPY_WEB_INTEL_HPP
#define CANOPY_WEB_INTEL_HPP

#include <canopy/build.hpp>
#include <canopy/code_intel.hpp>
#include <canopy/web_docs.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

enum class WebRegion { Html, Css, Script };

struct WebRegionAt {
    WebRegion region = WebRegion::Html;
    std::size_t start = 0;
};

bool is_web_language(const std::string& language);
WebRegionAt web_region_at(const std::string& html, std::size_t offset);
std::string js_object_owner(const std::string& text, std::size_t dot);
std::optional<std::string> js_module_string_at(const std::string& line, std::size_t column);
std::string css_property_before(const std::string& text, std::size_t offset);
std::vector<std::string> node_package_names(const std::string& file);
std::vector<CompilerMessage> lint_web(const std::string& language, const std::string& text, const std::string& path);
std::string web_script_text(const std::string& html, bool& module);
bool javascript_is_module(const std::string& js, const std::string& path);
std::vector<CompilerMessage> parse_node_check(const std::string& output, const std::string& path);
std::optional<HoverResult> web_hover(const std::string& language, const std::string& text, std::size_t line, std::size_t column);

} // namespace canopy

#endif // CANOPY_WEB_INTEL_HPP
