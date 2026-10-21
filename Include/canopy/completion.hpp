#ifndef CANOPY_COMPLETION_HPP
#define CANOPY_COMPLETION_HPP

#include <canopy/code_intel.hpp>
#include <grove/input/completion.hpp>
#include <fizmo/../../Source/Collections/trie.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace canopy {

class CompletionIndex {
public:
    void clear();
    bool add(grove::CompletionItem item, int priority = 0);
    std::size_t size() const { return m_entries.size(); }
    bool contains(const std::string& label) const;
    std::size_t count_prefix(const std::string& typed) const { return m_trie.count_prefix(typed); }
    std::vector<grove::CompletionItem> query(const std::string& typed, std::size_t limit = 300) const;

private:
    struct Entry {
        grove::CompletionItem item;
        int priority = 0;
    };

    std::vector<Entry> m_entries;
    std::map<std::string, std::size_t> m_labels;
    fizmo::TrieMap<std::vector<std::size_t>, fizmo::collections::TrieIgnoreCase> m_trie;
};

enum class CompletionSite {
    None,
    Code,
    Member,
    Scope,
    Preprocessor,
    Include,
    Tag,
    ClosingTag,
    Attribute,
    AttributeValue,
    Property,
    Value,
    Selector,
    Pseudo,
    ClassName,
    IdName,
    Variable,
    Command,
    Argument,
    Module,
    SpecialCharacter
};

struct CompletionRequest {
    std::string language;
    std::string path;
    std::string text;
    std::size_t line = 0;
    std::size_t column = 0;
    bool manual = false;
    bool words = true;
};

struct CompletionContext {
    CompletionSite site = CompletionSite::None;
    std::string language;
    std::size_t start = 0;
    std::string typed;
    std::string tag;
    std::string attribute;
    std::string open_tag;
    std::string include_dir;
    std::string module_prefix;
    bool angled = false;
    bool rest_has_colon = false;
    bool rest_has_paren = false;
    bool rest_has_quote = false;
    bool rest_has_brace = false;
};

std::string completion_word_chars(const std::string& language);
std::string completion_triggers(const std::string& language);
std::string completion_kind(const std::string& symbol_kind);
CompletionContext completion_context(const CompletionRequest& request);

struct CompletionSources {
    std::vector<Symbol> symbols;
    std::vector<std::string> include_dirs;
    std::vector<grove::CompletionItem> extra;
    bool pending = false;
};

grove::CompletionList complete_code(const CompletionRequest& request, const CompletionSources& sources);
grove::CompletionList complete_code(const CompletionRequest& request, const CompletionSources& sources, const CompletionContext& ctx);
std::vector<grove::CompletionItem> include_completions(const std::vector<std::string>& dirs, const std::string& sub, const std::string& typed);

} // namespace canopy

#endif // CANOPY_COMPLETION_HPP
