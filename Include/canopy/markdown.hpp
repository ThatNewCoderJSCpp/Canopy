#ifndef CANOPY_MARKDOWN_HPP
#define CANOPY_MARKDOWN_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace canopy {

struct MdSpan {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    bool image = false;
    bool line_break = false;
    std::string href;
};

struct MdBlock;

struct MdItem {
    std::vector<MdBlock> blocks;
    bool task = false;
    bool checked = false;
};

enum class MdAlign {
    None,
    Left,
    Center,
    Right
};

struct MdBlock {
    enum class Kind {
        Heading,
        Paragraph,
        Code,
        Quote,
        List,
        Rule,
        Table,
        Html
    };

    Kind kind = Kind::Paragraph;
    int level = 0;
    std::vector<MdSpan> spans;
    std::string code;
    std::string language;
    bool ordered = false;
    long long start = 1;
    std::vector<MdItem> items;
    std::vector<MdBlock> children;
    std::vector<std::vector<std::vector<MdSpan>>> rows;
    std::vector<MdAlign> align;
    std::size_t line = 0;
};

bool is_markdown_path(const std::string& path);
std::vector<MdBlock> parse_markdown(const std::string& text);
std::vector<MdSpan> parse_markdown_inline(const std::string& text);
std::string markdown_plain_text(const std::vector<MdSpan>& spans);
std::string markdown_anchor(const std::string& heading);

} // namespace canopy

#endif // CANOPY_MARKDOWN_HPP
