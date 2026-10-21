#ifndef CANOPY_WEB_DOCS_HPP
#define CANOPY_WEB_DOCS_HPP

#include <string>
#include <string_view>
#include <vector>

namespace canopy {

struct WebDoc {
    std::string owner;
    std::string kind;
    std::string name;
    std::string signature;
    std::string text;
};

std::string_view web_docs_text() noexcept;
std::string_view language_docs_text() noexcept;
std::vector<WebDoc> parse_web_docs(std::string_view text);
const std::vector<WebDoc>& web_docs();
std::vector<const WebDoc*> web_docs_of(const std::string& owner);
const WebDoc* find_web_doc(const std::string& owner, const std::string& name);

} // namespace canopy

#endif // CANOPY_WEB_DOCS_HPP
