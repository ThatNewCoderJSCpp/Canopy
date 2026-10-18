#ifndef CANOPY_LANGUAGE_DOCS_HPP
#define CANOPY_LANGUAGE_DOCS_HPP

#include <string>
#include <vector>

namespace canopy {

struct DocEntry {
    std::string name;
    std::string on;
    std::string text;
};

const std::vector<DocEntry>& grove_tag_docs();
const std::vector<DocEntry>& grove_attribute_docs();
const std::vector<DocEntry>& bark_property_docs();
const std::vector<DocEntry>& bark_pseudo_docs();
const std::vector<DocEntry>& bark_function_docs();
const std::vector<DocEntry>& bark_unit_docs();
const std::vector<DocEntry>& cmake_command_docs();
const std::vector<DocEntry>& cmake_variable_docs();
const std::vector<DocEntry>& c_word_docs();
const DocEntry* find_doc(const std::vector<DocEntry>& docs, const std::string& name);

} // namespace canopy

#endif // CANOPY_LANGUAGE_DOCS_HPP
