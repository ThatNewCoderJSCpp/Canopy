#include <canopy/completion.hpp>
#include <canopy/language_docs.hpp>
#include <canopy/special_names.hpp>
#include <canopy/web_intel.hpp>
#include <canopy/lang_intel.hpp>
#include <grove/input/code_buffer.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim_text(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string trim_right(const std::string& s) {
    std::size_t b = s.size();
    while (b > 0 && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(0, b);
}

bool ends_with(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }

bool starts_with(const std::string& s, const std::string& head) { return s.rfind(head, 0) == 0; }

std::size_t offset_of(const std::string& text, std::size_t line, std::size_t column) {
    std::size_t at = 0;
    for (std::size_t l = 0; l < line; ++l) {
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) return text.size();
        at = nl + 1;
    }
    return std::min(at + column, text.size());
}

bool tag_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.'; }

template <typename T, typename = void>
struct item_has_preview : std::false_type {};

template <typename T>
struct item_has_preview<T, std::void_t<decltype(std::declval<T&>().preview)>> : std::true_type {};

template <typename T>
bool set_completion_preview(T& item, const std::string& text) {
    if constexpr (item_has_preview<T>::value) {
        item.preview = text;
        return true;
    } else {
        (void)item;
        (void)text;
        return false;
    }
}

grove::CodeLanguage grove_language(const std::string& language) { return grove::code_language_from_name(language); }

std::string word_kind(grove::CodeTokenKind kind) {
    if (kind == grove::CodeTokenKind::BuiltinType || kind == grove::CodeTokenKind::Type) return "type";
    if (kind == grove::CodeTokenKind::Constant) return "constant";
    return "keyword";
}

struct BarkScan {
    int depth = 0;
    std::size_t statement = 0;
    bool comment = false;
};

BarkScan scan_bark(const std::string& text, std::size_t from, std::size_t to) {
    BarkScan s;
    s.statement = from;
    std::size_t i = from;
    while (i < to) {
        const char c = text[i];
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            const std::size_t e = text.find('\n', i);
            if (e == std::string::npos || e >= to) {
                s.comment = true;
                return s;
            }
            i = e + 1;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            const std::size_t e = text.find("*/", i + 2);
            if (e == std::string::npos || e + 2 > to) {
                s.comment = true;
                return s;
            }
            i = e + 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t e = i + 1;
            while (e < to && text[e] != c && text[e] != '\n') e += text[e] == '\\' ? 2 : 1;
            if (e >= to) {
                s.comment = true;
                return s;
            }
            i = e + 1;
            continue;
        }
        if (c == '{') {
            ++s.depth;
            s.statement = i + 1;
        } else if (c == '}') {
            s.depth = std::max(0, s.depth - 1);
            s.statement = i + 1;
        } else if (c == ';') {
            s.statement = i + 1;
        }
        ++i;
    }
    return s;
}

void bark_context(CompletionContext& ctx, const std::string& text, std::size_t region, std::size_t word_offset, const std::string& rest) {
    ctx.language = "bark";
    const BarkScan scan = scan_bark(text, region, word_offset);
    if (scan.comment) {
        ctx.site = CompletionSite::None;
        return;
    }
    const std::string segment = trim_text(text.substr(scan.statement, word_offset - scan.statement));
    const char prev = word_offset > 0 ? text[word_offset - 1] : '\0';
    if (!ctx.typed.empty() && ctx.typed[0] == '$') {
        ctx.site = CompletionSite::Variable;
        return;
    }
    if (!segment.empty() && segment[0] == '$' && segment.find('=') != std::string::npos) {
        ctx.site = CompletionSite::Value;
        return;
    }
    if (scan.depth > 0 && segment.find('{') == std::string::npos) {
        if (segment.find(':') != std::string::npos) {
            ctx.site = CompletionSite::Value;
            return;
        }
        if (prev == ':' && !segment.empty() && segment != ":") {
            ctx.site = CompletionSite::Pseudo;
            return;
        }
        ctx.site = CompletionSite::Property;
        ctx.rest_has_colon = !trim_text(rest).empty() && trim_text(rest)[0] == ':';
        return;
    }
    if (prev == ':') {
        ctx.site = CompletionSite::Pseudo;
        if (ctx.start > 0) {
            --ctx.start;
            ctx.typed = ":" + ctx.typed;
        }
        return;
    }
    if (prev == '.') {
        ctx.site = CompletionSite::ClassName;
        return;
    }
    if (prev == '#') {
        ctx.site = CompletionSite::IdName;
        return;
    }
    ctx.site = CompletionSite::Selector;
}

std::vector<std::string> open_tags(const std::string& text, std::size_t to) {
    static const std::set<std::string> lonely = { "br", "img", "hr", "input", "meta", "link", "area", "base", "col", "embed", "param", "source", "track", "wbr" };
    std::vector<std::string> stack;
    std::size_t i = 0;
    while (i < to) {
        if (text.compare(i, 4, "<!--") == 0) {
            const std::size_t e = text.find("-->", i + 4);
            if (e == std::string::npos || e >= to) break;
            i = e + 3;
            continue;
        }
        if (text[i] != '<') {
            ++i;
            continue;
        }
        const bool closing = i + 1 < to && text[i + 1] == '/';
        std::size_t j = i + (closing ? 2 : 1);
        const std::size_t name_at = j;
        while (j < to && tag_char(text[j])) ++j;
        const std::string name = text.substr(name_at, j - name_at);
        std::size_t gt = text.find('>', j);
        if (gt == std::string::npos || gt >= to) break;
        const bool self_closing = gt > 0 && text[gt - 1] == '/';
        if (!name.empty()) {
            if (closing) {
                for (std::size_t k = stack.size(); k > 0; --k) {
                    if (stack[k - 1] == name) {
                        stack.resize(k - 1);
                        break;
                    }
                }
            } else if (!self_closing && !lonely.count(name)) {
                stack.push_back(name);
                if (name == "style") {
                    const std::size_t close = text.find("</style", gt);
                    if (close == std::string::npos || close >= to) break;
                    gt = close - 1;
                }
            }
        }
        i = gt + 1;
    }
    return stack;
}

void grove_context(CompletionContext& ctx, const std::string& text, std::size_t word_offset, const std::string& rest) {
    const std::string head = text.substr(0, word_offset);
    const std::size_t style_open = head.rfind("<style");
    const std::size_t style_close = head.rfind("</style");
    if (style_open != std::string::npos && (style_close == std::string::npos || style_close < style_open)) {
        const std::size_t gt = head.find('>', style_open);
        if (gt != std::string::npos) {
            bark_context(ctx, text, gt + 1, word_offset, rest);
            return;
        }
    }
    const std::size_t comment_open = head.rfind("<!--");
    if (comment_open != std::string::npos) {
        const std::size_t comment_close = head.find("-->", comment_open);
        if (comment_close == std::string::npos) {
            ctx.site = CompletionSite::None;
            return;
        }
    }
    const std::size_t lt = head.rfind('<');
    const std::size_t gt = head.rfind('>');
    if (lt == std::string::npos || (gt != std::string::npos && gt > lt)) {
        ctx.site = CompletionSite::Code;
        return;
    }
    const std::string inside = head.substr(lt + 1);
    if (inside.empty()) {
        ctx.site = CompletionSite::Tag;
        return;
    }
    if (inside == "/") {
        ctx.site = CompletionSite::ClosingTag;
        const std::vector<std::string> stack = open_tags(text, lt);
        if (!stack.empty()) ctx.open_tag = stack.back();
        return;
    }
    std::size_t n = 0;
    while (n < inside.size() && tag_char(inside[n])) ++n;
    ctx.tag = inside.substr(0, n);
    if (n == inside.size() || ctx.tag.empty()) {
        ctx.site = CompletionSite::None;
        return;
    }
    const std::size_t quotes = static_cast<std::size_t>(std::count(inside.begin(), inside.end(), '"'));
    if (quotes % 2 == 1) {
        ctx.site = CompletionSite::AttributeValue;
        const std::size_t q = inside.rfind('"');
        std::size_t e = q;
        while (e > 0 && (inside[e - 1] == '=' || std::isspace(static_cast<unsigned char>(inside[e - 1])))) --e;
        std::size_t b = e;
        while (b > 0 && tag_char(inside[b - 1])) --b;
        ctx.attribute = inside.substr(b, e - b);
        return;
    }
    const char last = inside.back();
    if (!std::isspace(static_cast<unsigned char>(last))) {
        ctx.site = CompletionSite::None;
        return;
    }
    ctx.site = CompletionSite::Attribute;
    const std::string r = trim_text(rest);
    ctx.rest_has_quote = !r.empty() && r[0] == '=';
}

void css_context(CompletionContext& ctx, const std::string& text, std::size_t region, std::size_t word_offset, const std::string& rest, bool manual) {
    bark_context(ctx, text, region, word_offset, rest);
    ctx.language = "css";
    if (ctx.site == CompletionSite::None) return;
    const char prev = word_offset > 0 ? text[word_offset - 1] : '\0';
    if (prev == '@') {
        ctx.site = CompletionSite::Command;
        const std::size_t back = std::min<std::size_t>(ctx.start, 1);
        ctx.start -= back;
        ctx.typed = "@" + ctx.typed;
        return;
    }
    if (ctx.site == CompletionSite::Pseudo && ctx.start > 0 && word_offset >= 2 && text[word_offset - 1] == ':' && text[word_offset - 2] == ':') {
        --ctx.start;
        ctx.typed = ":" + ctx.typed;
    }
    if (ctx.site == CompletionSite::Value) ctx.attribute = css_property_before(text, word_offset);
    if (ctx.typed.empty() && !manual && (ctx.site == CompletionSite::Selector || ctx.site == CompletionSite::Property)) ctx.site = CompletionSite::None;
}

void js_context(CompletionContext& ctx, const CompletionRequest& r, const std::string& line, std::size_t column, std::size_t word_offset, const std::string& rest) {
    ctx.language = "javascript";
    if (const std::optional<std::string> module = js_module_string_at(line, column)) {
        std::size_t q = column;
        while (q > 0 && line[q - 1] != '"' && line[q - 1] != '\'' && line[q - 1] != '`') --q;
        ctx.site = CompletionSite::Module;
        ctx.start = q;
        ctx.typed = line.substr(q, column - q);
        const std::size_t end = line.find_first_of("\"'`", column);
        ctx.rest_has_quote = end != std::string::npos;
        return;
    }
    char quote = 0;
    for (std::size_t i = 0; i < ctx.start && i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && (line[i + 1] == '/' || line[i + 1] == '*')) {
            ctx.site = CompletionSite::None;
            return;
        }
        if (c == '"' || c == '\'' || c == '`') quote = c;
    }
    if (quote) {
        ctx.site = CompletionSite::None;
        return;
    }
    const std::string r2 = trim_text(rest);
    ctx.rest_has_paren = !r2.empty() && r2[0] == '(';
    if (ctx.start > 0 && line[ctx.start - 1] == '.' && !(ctx.start > 1 && line[ctx.start - 2] == '.')) {
        ctx.site = CompletionSite::Member;
        ctx.tag = js_object_owner(r.text, word_offset - 1);
        return;
    }
    if (ctx.typed.empty() && !r.manual) {
        ctx.site = CompletionSite::None;
        return;
    }
    ctx.site = CompletionSite::Code;
}

void more_context(CompletionContext& ctx, const CompletionRequest& r, std::size_t word_offset, const std::string& before, const std::string& rest) {
    ctx.language = r.language;
    const std::string& language = r.language;
    if (more_inside_text(language, r.text, r.line, ctx.start)) {
        ctx.site = CompletionSite::None;
        return;
    }
    const std::string trimmed = trim_text(before);
    const std::string r2 = trim_text(rest);
    ctx.rest_has_paren = !r2.empty() && r2[0] == '(';
    if (language == "python" && (trimmed == "import" || (starts_with(trimmed, "from ") && trimmed.find(" import") == std::string::npos && trimmed.find('.') == std::string::npos))) {
        ctx.site = CompletionSite::Module;
        ctx.tag = "python-module";
        return;
    }
    std::string sep;
    if (!before.empty() && before.back() == '.' && !(before.size() > 1 && before[before.size() - 2] == '.') && language != "asm") sep = ".";
    else if (language == "rust" && ends_with(before, "::")) sep = "::";
    else if (language == "lua" && !before.empty() && before.back() == ':' && !ends_with(before, "::")) sep = ":";
    if (!sep.empty()) {
        ctx.site = CompletionSite::Member;
        ctx.attribute = sep;
        ctx.tag = more_member_owner(language, r.text, word_offset - sep.size(), sep);
        return;
    }
    if (language == "asm") {
        ctx.angled = trimmed.empty() || trimmed.back() == ':';
    }
    if (ctx.typed.empty() && !r.manual) {
        ctx.site = CompletionSite::None;
        return;
    }
    ctx.site = CompletionSite::Code;
}

void c_context(CompletionContext& ctx, const CompletionRequest& r, const std::string& line, const std::string& masked_line, const std::string& before, std::size_t column, const std::string& rest) {
    const bool walnut = r.language == "walnut";
    const std::string t = trim_text(line);
    if (walnut) {
        const std::string tb = trim_text(before);
        const std::size_t at = tb.find("module.");
        if (starts_with(tb, "import ") && at != std::string::npos && tb.find(' ', at) == std::string::npos && !tb.empty() && tb.back() == '.') {
            ctx.site = CompletionSite::Module;
            ctx.module_prefix = tb.substr(at + 7);
            return;
        }
    }
    if (!walnut && !t.empty() && t[0] == '#') {
        const std::string tb = trim_text(before);
        if (tb == "#") {
            ctx.site = CompletionSite::Preprocessor;
            return;
        }
        std::string body = trim_text(tb.substr(tb.find('#') + 1));
        if (starts_with(body, "include") || starts_with(body, "import")) {
            const std::size_t q = line.find_first_of("\"<", line.find('#'));
            if (q != std::string::npos && q < column) {
                const char close = line[q] == '"' ? '"' : '>';
                const std::size_t closed = line.find(close, q + 1);
                if (closed == std::string::npos || closed >= column) {
                    const std::string path = line.substr(q + 1, column - q - 1);
                    const std::size_t slash = path.find_last_of("/\\");
                    ctx.site = CompletionSite::Include;
                    ctx.angled = close == '>';
                    ctx.include_dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
                    ctx.start = q + 1 + (slash == std::string::npos ? 0 : slash + 1);
                    ctx.typed = line.substr(ctx.start, column - ctx.start);
                    ctx.rest_has_quote = !rest.empty() && rest[0] == close;
                    return;
                }
            }
            ctx.site = CompletionSite::None;
            return;
        }
    }
    const std::size_t probe = ctx.typed.empty() ? (column > 0 ? column - 1 : std::string::npos) : ctx.start;
    if (probe != std::string::npos && probe < line.size() && probe < masked_line.size() && masked_line[probe] != line[probe]) {
        ctx.site = CompletionSite::None;
        return;
    }
    const std::string p = trim_right(before);
    if (ends_with(p, "->")) {
        ctx.site = CompletionSite::Member;
        return;
    }
    if (ends_with(p, "::")) {
        ctx.site = CompletionSite::Scope;
        return;
    }
    if (ends_with(p, ".") && !ends_with(p, "..")) {
        std::size_t k = p.size() - 1;
        bool digits = k > 0;
        std::size_t b = k;
        while (b > 0 && grove::is_completion_word_char(p[b - 1])) --b;
        for (std::size_t i = b; i < k; ++i) digits = digits && std::isdigit(static_cast<unsigned char>(p[i]));
        if (digits && b < k) {
            ctx.site = CompletionSite::None;
            return;
        }
        ctx.site = CompletionSite::Member;
        return;
    }
    if (ctx.typed.empty() && !r.manual) {
        ctx.site = CompletionSite::None;
        return;
    }
    ctx.site = CompletionSite::Code;
}

void cmake_context(CompletionContext& ctx, const std::string& text, std::size_t word_offset, const std::string& before, const std::string& rest) {
    int depth = 0;
    bool quote = false;
    for (std::size_t i = 0; i < word_offset; ++i) {
        const char c = text[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == '"') quote = false;
            continue;
        }
        if (c == '#') {
            const std::size_t e = text.find('\n', i);
            if (e == std::string::npos || e >= word_offset) {
                ctx.site = CompletionSite::None;
                return;
            }
            i = e;
            continue;
        }
        if (c == '"') quote = true;
        else if (c == '(') ++depth;
        else if (c == ')') depth = std::max(0, depth - 1);
    }
    if (ends_with(before, "${") || ends_with(before, "$ENV{")) {
        ctx.site = CompletionSite::Variable;
        ctx.rest_has_quote = !rest.empty() && rest[0] == '}';
        return;
    }
    if (quote) {
        ctx.site = CompletionSite::None;
        return;
    }
    if (depth == 0) {
        ctx.site = CompletionSite::Command;
        const std::string r = trim_text(rest);
        ctx.rest_has_paren = !r.empty() && r[0] == '(';
        return;
    }
    ctx.site = CompletionSite::Argument;
}

const std::vector<std::string>& bark_value_words() {
    static const std::vector<std::string> words = { "none", "auto", "inherit", "initial", "transparent", "currentColor", "normal", "bold", "italic", "center", "left", "right", "top", "bottom", "start", "end", "flex", "grid", "block", "inline", "inline-block", "row", "column", "wrap", "nowrap", "hidden", "visible", "scroll", "solid", "dashed", "dotted", "pointer", "default", "text", "space-between", "space-around", "stretch", "absolute", "relative", "fixed", "true", "false" };
    return words;
}

const std::vector<std::string>& cmake_argument_words() {
    static const std::vector<std::string> words = { "PUBLIC", "PRIVATE", "INTERFACE", "REQUIRED", "COMPONENTS", "STATIC", "SHARED", "MODULE", "OBJECT", "ALIAS", "IMPORTED", "ON", "OFF", "TRUE", "FALSE", "NOT", "AND", "OR", "DEFINED", "EQUAL", "LESS", "GREATER", "STREQUAL", "VERSION_LESS", "VERSION_GREATER", "VERSION_EQUAL", "VERSION_GREATER_EQUAL", "MATCHES", "EXISTS", "IS_DIRECTORY", "TARGET", "CACHE", "STRING", "BOOL", "PATH", "FILEPATH", "INTERNAL", "FORCE", "PARENT_SCOPE", "APPEND", "PREPEND", "GLOB", "GLOB_RECURSE", "CONFIGURE_DEPENDS", "DESTINATION", "TARGETS", "FILES", "DIRECTORY", "COMMAND", "DEPENDS", "WORKING_DIRECTORY", "OUTPUT", "VERBATIM", "GIT_REPOSITORY", "GIT_TAG", "URL", "LANGUAGES", "VERSION", "DESCRIPTION", "FATAL_ERROR", "WARNING", "STATUS", "SEND_ERROR", "AUTHOR_WARNING" };
    return words;
}

bool header_name(const std::string& name, bool angled) {
    const std::string ext = lower(fs::path(name).extension().string());
    static const std::set<std::string> headers = { ".h", ".hh", ".hpp", ".hxx", ".h++", ".inl", ".ipp", ".tpp", ".inc", ".walh" };
    if (headers.count(ext)) return true;
    return angled && ext.empty() && !name.empty() && name[0] != '.';
}

} // namespace

void CompletionIndex::clear() {
    m_entries.clear();
    m_labels.clear();
    m_trie.clear();
}

bool CompletionIndex::add(grove::CompletionItem item, int priority) {
    if (item.label.empty() || m_labels.count(item.label)) return false;
    const std::size_t index = m_entries.size();
    m_labels[item.label] = index;
    m_trie[item.label].push_back(index);
    m_entries.push_back(Entry{ std::move(item), priority });
    return true;
}

bool CompletionIndex::contains(const std::string& label) const { return m_labels.count(label) > 0; }

std::vector<grove::CompletionItem> CompletionIndex::query(const std::string& typed, std::size_t limit) const {
    std::vector<std::size_t> hits;
    std::vector<char> taken(m_entries.size(), 0);
    auto by_rank = [&](std::size_t a, std::size_t b) {
        const Entry& x = m_entries[a];
        const Entry& y = m_entries[b];
        const bool xc = starts_with(x.item.label, typed);
        const bool yc = starts_with(y.item.label, typed);
        if (xc != yc) return xc;
        if (x.priority != y.priority) return x.priority < y.priority;
        if (x.item.label.size() != y.item.label.size()) return x.item.label.size() < y.item.label.size();
        return x.item.label < y.item.label;
    };
    if (typed.empty()) {
        for (std::size_t i = 0; i < m_entries.size(); ++i) hits.push_back(i);
    } else {
        m_trie.for_each_prefix(typed, [&](const std::string&, const std::vector<std::size_t>& list) {
            for (std::size_t i : list) {
                hits.push_back(i);
                taken[i] = 1;
            }
            return hits.size() < 20000;
        });
    }
    std::stable_sort(hits.begin(), hits.end(), by_rank);
    if (hits.size() > limit) hits.resize(limit);
    if (!typed.empty() && hits.size() < limit) {
        std::vector<std::pair<int, std::size_t>> fuzzy;
        for (std::size_t i = 0; i < m_entries.size(); ++i) {
            if (taken[i]) continue;
            int score = 0;
            if (grove::completion_match_positions(m_entries[i].item.label, typed, score)) fuzzy.emplace_back(score, i);
        }
        std::stable_sort(fuzzy.begin(), fuzzy.end(), [&](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return by_rank(a.second, b.second);
        });
        for (const auto& f : fuzzy) {
            if (hits.size() >= limit) break;
            hits.push_back(f.second);
        }
    }
    std::vector<grove::CompletionItem> out;
    out.reserve(hits.size());
    for (std::size_t i : hits) out.push_back(m_entries[i].item);
    return out;
}

std::string completion_word_chars(const std::string& language) {
    if (language == "bark") return "-$";
    if (language == "grove") return "-$";
    if (language == "css" || language == "html") return "-";
    if (language == "javascript") return "$";
    if (language == "zig") return "@";
    if (language == "asm") return ".%";
    return "";
}

std::string completion_triggers(const std::string& language) {
    if (language == "c" || language == "cpp") return ". -> :: # \" < /";
    if (language == "walnut") return ". -> :: \\s{";
    if (language == "grove") return "< </";
    if (language == "bark") return ":";
    if (language == "cmake") return "${";
    if (language == "html") return "< </ . : \" '";
    if (language == "css") return ": @";
    if (language == "javascript") return ". \" '";
    if (language == "python" || language == "java" || language == "csharp") return ".";
    if (language == "rust") return ". ::";
    if (language == "zig") return ". @";
    if (language == "lua") return ". :";
    return "";
}

std::string completion_kind(const std::string& symbol_kind) {
    if (symbol_kind == "function macro") return "macro-function";
    if (symbol_kind == "command") return "function";
    if (symbol_kind == "target") return "value";
    if (symbol_kind == "project") return "module";
    return symbol_kind;
}

CompletionContext completion_context(const CompletionRequest& request) {
    CompletionContext ctx;
    ctx.language = request.language;
    const std::optional<std::string> found = text_line_at(request.text, request.line);
    if (!found) return ctx;
    const std::string& line = *found;
    const std::size_t column = std::min(request.column, line.size());
    const std::string extra = completion_word_chars(request.language);
    ctx.start = grove::completion_word_start(line, column, extra);
    ctx.typed = line.substr(ctx.start, column - ctx.start);
    std::size_t end = column;
    while (end < line.size() && grove::is_completion_word_char(line[end], extra)) ++end;
    const std::string rest = line.substr(end);
    const std::string before = line.substr(0, ctx.start);
    const std::size_t word_offset = offset_of(request.text, request.line, ctx.start);
    if (!ctx.typed.empty() && std::isdigit(static_cast<unsigned char>(ctx.typed[0]))) return ctx;
    const std::string& language = request.language;
    if (language == "walnut" && before.size() >= 3 && before.compare(before.size() - 3, 3, "\\s{") == 0) {
        std::size_t slashes = 0;
        for (std::size_t k = before.size() - 3; k-- > 0 && before[k] == '\\';) ++slashes;
        if (slashes % 2 == 0) {
            ctx.site = CompletionSite::SpecialCharacter;
            ctx.rest_has_brace = !rest.empty() && rest[0] == '}';
            return ctx;
        }
    }
    if (language == "c" || language == "cpp" || language == "walnut") {
        const std::string masked = mask_c_like_line(request.text, request.line);
        c_context(ctx, request, line, masked.size() == line.size() ? masked : line, before, column, rest);
    } else if (language == "bark") {
        bark_context(ctx, request.text, 0, word_offset, rest);
    } else if (language == "grove") {
        grove_context(ctx, request.text, word_offset, rest);
    } else if (language == "cmake") {
        cmake_context(ctx, request.text, word_offset, before, rest);
    } else if (is_more_language(language)) {
        more_context(ctx, request, word_offset, before, rest);
    } else if (language == "javascript") {
        js_context(ctx, request, line, column, word_offset, rest);
    } else if (language == "css") {
        css_context(ctx, request.text, 0, word_offset, rest, request.manual);
    } else if (language == "html") {
        const WebRegionAt region = web_region_at(request.text, word_offset);
        if (region.region == WebRegion::Script) {
            js_context(ctx, request, line, column, word_offset, rest);
        } else if (region.region == WebRegion::Css) {
            css_context(ctx, request.text, region.start, word_offset, rest, request.manual);
        } else {
            grove_context(ctx, request.text, word_offset, rest);
            ctx.language = "html";
            if (ctx.site == CompletionSite::Code && !request.manual) ctx.site = CompletionSite::None;
        }
    } else {
        ctx.site = CompletionSite::Code;
    }
    return ctx;
}

std::vector<grove::CompletionItem> include_completions(const std::vector<std::string>& dirs, const std::string& sub, const std::string& typed) {
    std::vector<grove::CompletionItem> out;
    std::set<std::string> seen;
    const std::string want = lower(typed);
    for (const std::string& d : dirs) {
        std::error_code ec;
        const fs::path dir = fs::path(d) / sub;
        if (!fs::is_directory(dir, ec)) continue;
        std::size_t looked = 0;
        for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (++looked > 4000 || out.size() > 600) break;
            const std::string name = it->path().filename().string();
            if (name.empty() || name[0] == '.') continue;
            if (!want.empty() && lower(name).find(want.substr(0, 1)) == std::string::npos) continue;
            const bool folder = it->is_directory(ec);
            if (!folder && !header_name(name, true)) continue;
            if (!seen.insert(name).second) continue;
            grove::CompletionItem item;
            item.label = name;
            item.kind = folder ? "folder" : "file";
            item.detail = (fs::path(d) / sub / name).lexically_normal().string();
            if (folder) item.insert = name + "/";
            out.push_back(std::move(item));
        }
    }
    return out;
}

grove::CompletionList complete_code(const CompletionRequest& request, const CompletionSources& sources) {
    return complete_code(request, sources, completion_context(request));
}

grove::CompletionList complete_code(const CompletionRequest& request, const CompletionSources& sources, const CompletionContext& ctx) {
    grove::CompletionList list;
    list.line = request.line;
    list.column = ctx.start;
    if (ctx.site == CompletionSite::None) return list;
    CompletionIndex index;
    std::optional<std::vector<std::string>> split;
    auto lines = [&]() -> const std::vector<std::string>& {
        if (!split) split = split_text_lines(request.text);
        return *split;
    };
    auto add = [&](const std::string& label, const std::string& kind, const std::string& detail, const std::string& insert, const std::string& doc, int priority) {
        grove::CompletionItem item;
        item.label = label;
        item.kind = kind;
        item.detail = detail;
        item.insert = insert;
        item.documentation = doc;
        index.add(std::move(item), priority);
    };
    auto add_extra = [&](int priority) {
        for (const grove::CompletionItem& item : sources.extra) index.add(item, priority);
    };
    std::set<std::string> keywords;
    if (ctx.site == CompletionSite::Member || ctx.site == CompletionSite::Scope) {
        for (const grove::CodeWord& w : grove::code_language_words(grove_language(request.language))) keywords.insert(w.word);
    }
    auto add_words = [&](int priority, char only = '\0') {
        if (!request.words) return;
        for (grove::CompletionItem& item : grove::word_completions(lines(), request.line, request.column, completion_word_chars(ctx.language))) {
            if (only && item.label[0] != only) continue;
            if (keywords.count(item.label)) continue;
            if (!only && item.label[0] == '$' && ctx.site != CompletionSite::Value) continue;
            index.add(std::move(item), priority);
        }
    };
    auto add_symbols = [&](const std::function<bool(const Symbol&)>& keep) {
        for (const Symbol& s : sources.symbols) {
            if (!keep(s)) continue;
            std::string detail = s.signature.empty() ? s.value : s.signature;
            if (detail.size() > 120) detail = detail.substr(0, 117) + "...";
            const int priority = s.file == request.path ? 0 : 1;
            std::string kind = completion_kind(s.kind);
            if (kind == "function" && request.language == "cmake") {
                add(s.name, kind, detail, ctx.rest_has_paren ? std::string() : s.name + "($0)", s.doc, priority);
                continue;
            }
            add(s.name, kind, detail, std::string(), s.doc, priority);
        }
    };
    auto add_keywords = [&](grove::CodeLanguage language, int priority) {
        for (const grove::CodeWord& w : grove::code_language_words(language)) {
            const DocEntry* doc = find_doc(c_word_docs(), w.word);
            add(w.word, word_kind(w.kind), std::string(), std::string(), doc ? doc->text : std::string(), priority);
        }
    };
    const std::string& lang = request.language;
    if (is_more_language(ctx.language)) {
        auto add_doc = [&](const WebDoc& d, int priority) {
            const bool callable = d.kind == "method" || d.kind == "function" || d.kind == "macro";
            std::string insert;
            if (callable && !ctx.rest_has_paren) insert = d.name + "($0)";
            if (d.kind == "macro" && d.name == "vec!" && !ctx.rest_has_paren) insert = "vec![$0]";
            const std::string kind = d.kind == "instruction" ? std::string("keyword") : d.kind == "directive" ? std::string("keyword") : d.kind == "register" ? std::string("variable") : d.kind == "macro" ? std::string("function") : d.kind;
            add(d.name, kind, d.signature.empty() ? d.text : d.signature, insert, d.signature.empty() ? std::string() : d.text, priority);
        };
        switch (ctx.site) {
            case CompletionSite::Member:
                if (!ctx.tag.empty()) {
                    for (const WebDoc* d : web_docs_of(ctx.tag)) {
                        if (ctx.language == "rust" && ctx.attribute == "::" && d->kind == "method") continue;
                        if (ctx.language == "rust" && ctx.attribute == "." && d->kind == "function") continue;
                        add_doc(*d, 0);
                    }
                }
                add_words(4);
                break;
            case CompletionSite::Module:
                for (const WebDoc* d : web_docs_of(ctx.tag)) add_doc(*d, 0);
                break;
            case CompletionSite::Code: {
                int priority = 0;
                for (std::string owner : more_code_owners(ctx.language, request.text, request.path, ctx.angled)) {
                    std::string only;
                    const std::size_t hash = owner.find('#');
                    if (hash != std::string::npos) {
                        only = owner.substr(hash + 1);
                        owner = owner.substr(0, hash);
                    }
                    for (const WebDoc* d : web_docs_of(owner)) {
                        if (!only.empty() && d->kind != only) continue;
                        if (ctx.language == "asm" && ctx.angled && d->kind == "register") continue;
                        add_doc(*d, priority);
                    }
                    priority = std::min(priority + 1, 1);
                }
                for (const grove::CodeWord& w : grove::code_language_words(grove_language(lang))) {
                    if (ctx.language == "asm") continue;
                    add(w.word, word_kind(w.kind), std::string(), std::string(), std::string(), 2);
                }
                add_extra(1);
                add_words(3);
                break;
            }
            default:
                add_words(3);
                break;
        }
        list.items = index.query(ctx.typed, 300);
        return list;
    }
    if (ctx.language == "javascript" || ctx.language == "css" || ctx.language == "html") {
        auto kind_of = [](const WebDoc& d) -> std::string {
            if (d.kind == "flag") return "attribute";
            return d.kind;
        };
        auto add_doc = [&](const WebDoc& d, const std::string& insert, int priority) {
            add(d.name, kind_of(d), d.signature.empty() ? d.text : d.signature, insert, d.signature.empty() ? std::string() : d.text, priority);
        };
        auto add_owner = [&](const std::string& owner, int priority, bool calls) {
            for (const WebDoc* d : web_docs_of(owner)) {
                const bool callable = calls && (d->kind == "method" || d->kind == "function");
                add_doc(*d, callable && !ctx.rest_has_paren ? d->name + "($0)" : std::string(), priority);
            }
        };
        switch (ctx.site) {
            case CompletionSite::Code:
                if (ctx.language == "javascript") {
                    add_owner("js-keyword", 2, false);
                    add_owner("js-global", 1, false);
                    if (lang == "javascript") add_owner("node-global", 1, false);
                    add_extra(1);
                }
                add_words(3);
                break;
            case CompletionSite::Member:
                if (!ctx.tag.empty()) add_owner(ctx.tag, 0, true);
                add_words(4);
                break;
            case CompletionSite::Module:
                for (const WebDoc* d : web_docs_of("node-module")) add_doc(*d, std::string(), d->text.rfind("(package)", 0) == 0 ? 2 : 0);
                for (const std::string& name : node_package_names(request.path)) {
                    if (index.contains(name)) continue;
                    add(name, "module", "node_modules", std::string(), std::string(), 1);
                }
                break;
            case CompletionSite::Tag:
                for (const WebDoc* d : web_docs_of("html-tag")) add(d->name, "tag", d->text, std::string(), std::string(), 0);
                break;
            case CompletionSite::ClosingTag:
                if (!ctx.open_tag.empty()) add(ctx.open_tag, "tag", "closes <" + ctx.open_tag + ">", ctx.open_tag + ">", std::string(), -1);
                for (const WebDoc* d : web_docs_of("html-tag")) add(d->name, "tag", d->text, d->name + ">", std::string(), 1);
                break;
            case CompletionSite::Attribute:
                for (const WebDoc* d : web_docs_of("html-attr")) {
                    int priority = 1;
                    if (!d->signature.empty()) {
                        priority = 2;
                        std::size_t w = 0;
                        while (w < d->signature.size()) {
                            std::size_t e = d->signature.find(' ', w);
                            if (e == std::string::npos) e = d->signature.size();
                            if (d->signature.compare(w, e - w, ctx.tag) == 0) priority = 0;
                            w = e + 1;
                        }
                    }
                    std::string insert;
                    if (d->name == "data-") insert = "data-";
                    else if (d->kind != "flag" && !ctx.rest_has_quote) insert = d->name + "=\"$0\"";
                    add(d->name, "attribute", d->signature.empty() ? std::string("any element") : d->signature, insert, d->text, priority);
                }
                break;
            case CompletionSite::AttributeValue:
                for (const WebDoc* d : web_docs_of("html-value:" + lower(ctx.attribute))) add_doc(*d, std::string(), 0);
                add_words(3);
                break;
            case CompletionSite::Property:
                for (const WebDoc* d : web_docs_of("css-prop")) add(d->name, "property", d->signature, ctx.rest_has_colon ? std::string() : d->name + ": $0;", d->text, 0);
                add_words(4);
                break;
            case CompletionSite::Value:
                for (const WebDoc* d : web_docs_of("css-value:" + ctx.attribute)) add_doc(*d, std::string(), 0);
                for (const WebDoc* d : web_docs_of("css-value:*")) add_doc(*d, std::string(), 1);
                for (const WebDoc* d : web_docs_of("css-func")) add_doc(*d, d->name + "($0)", 2);
                add_words(3);
                break;
            case CompletionSite::Selector:
                for (const WebDoc* d : web_docs_of("html-tag")) add(d->name, "tag", d->text, std::string(), std::string(), 1);
                add_words(2);
                break;
            case CompletionSite::Pseudo:
                for (const WebDoc* d : web_docs_of("css-pseudo")) {
                    const bool call = d->name.size() > 2 && d->name.compare(d->name.size() - 2, 2, "()") == 0;
                    add(d->name, "keyword", std::string(), call ? d->name.substr(0, d->name.size() - 2) + "($0)" : std::string(), d->text, 0);
                }
                break;
            case CompletionSite::Command:
                for (const WebDoc* d : web_docs_of("css-at")) add(d->name, "keyword", d->signature, d->name + " ", d->text, 0);
                break;
            default:
                add_words(3);
                break;
        }
        list.items = index.query(ctx.typed, 300);
        return list;
    }
    switch (ctx.site) {
        case CompletionSite::None:
            break;
        case CompletionSite::Preprocessor:
            for (const char* d : { "include", "define", "undef", "if", "ifdef", "ifndef", "elif", "else", "endif", "error", "warning", "pragma", "line" }) add(d, "keyword", "#" + std::string(d), std::string(), std::string(), 0);
            break;
        case CompletionSite::Module:
            add_extra(0);
            break;
        case CompletionSite::SpecialCharacter:
            for (const std::string_view name : special_character_names(std::string_view())) {
                std::uint32_t code = 0;
                if (!special_character(name, code)) continue;
                std::string glyph;
                if (code > 0x20 && code != 0x7F && !(code >= 0x80 && code < 0xA0)) {
                    if (code < 0x80) glyph += static_cast<char>(code);
                    else if (code < 0x800) {
                        glyph += static_cast<char>(0xC0 | (code >> 6));
                        glyph += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        glyph += static_cast<char>(0xE0 | (code >> 12));
                        glyph += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        glyph += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        glyph += static_cast<char>(0xF0 | (code >> 18));
                        glyph += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        glyph += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        glyph += static_cast<char>(0x80 | (code & 0x3F));
                    }
                }
                char hex[16];
                std::snprintf(hex, sizeof hex, "U+%04X", static_cast<unsigned>(code));
                const std::string label(name);
                grove::CompletionItem item;
                item.label = label;
                item.insert = ctx.rest_has_brace ? std::string() : label + "}";
                std::string others;
                for (const std::string_view other : builtin_names_for(code)) {
                    if (other != name) others += (others.empty() ? "" : ", ") + std::string("\\s{") + std::string(other) + "}";
                }
                if (!others.empty()) item.documentation = "Also written " + others;
                if (set_completion_preview(item, glyph.empty() ? std::string("\xE2\x90\xA3") : glyph)) {
                    item.kind = "character";
                    item.detail = hex;
                } else {
                    item.kind = "constant";
                    item.detail = glyph.empty() ? std::string(hex) : glyph + "  " + hex;
                }
                index.add(std::move(item), label.size() <= 6 ? 0 : 1);
            }
            break;
        case CompletionSite::Include:
            for (grove::CompletionItem& item : include_completions(sources.include_dirs, ctx.include_dir, ctx.typed)) {
                if (item.kind == "file" && !ctx.rest_has_quote) item.insert = item.label + (ctx.angled ? ">" : "\"");
                const int priority = item.kind == "folder" ? 1 : 0;
                index.add(std::move(item), priority);
            }
            break;
        case CompletionSite::Member:
            if (lang == "walnut") {
                add_extra(0);
                if (!sources.pending && sources.extra.empty()) add_words(3);
                break;
            }
            add_symbols([](const Symbol& s) { return s.kind == "field"; });
            for (const Symbol& s : sources.symbols) {
                if (s.kind == "function" && s.name != "main") add(s.name, "method", s.signature, std::string(), s.doc, 2);
            }
            add_words(3);
            break;
        case CompletionSite::Scope:
            if (lang == "walnut") {
                add_extra(0);
                if (!sources.pending && sources.extra.empty()) add_words(3);
                break;
            }
            add_symbols([](const Symbol& s) { return s.kind != "macro" && s.kind != "function macro"; });
            add_words(3);
            break;
        case CompletionSite::Code:
            if (lang == "c" || lang == "cpp") {
                add_symbols([](const Symbol&) { return true; });
                add_keywords(grove_language(lang), 2);
                add_words(3);
            } else if (lang == "walnut") {
                add_extra(0);
                add_keywords(grove::CodeLanguage::Walnut, 2);
                add_words(3);
            } else {
                add_extra(1);
                add_words(3);
            }
            break;
        case CompletionSite::Command:
            add_symbols([](const Symbol& s) { return s.kind == "command"; });
            for (const DocEntry& d : cmake_command_docs()) add(d.name, "function", d.on, ctx.rest_has_paren ? std::string() : d.name + "($0)", d.text, 1);
            add_words(3);
            break;
        case CompletionSite::Variable:
            if (lang == "cmake") {
                for (const Symbol& s : sources.symbols) {
                    if (s.kind == "variable") add(s.name, "variable", s.value, ctx.rest_has_quote ? std::string() : s.name + "}", s.doc, s.file == request.path ? 0 : 1);
                }
                for (const DocEntry& d : cmake_variable_docs()) add(d.name, "variable", std::string(), ctx.rest_has_quote ? std::string() : d.name + "}", d.text, 2);
            } else {
                add_symbols([](const Symbol& s) { return s.kind == "variable"; });
                add_words(3, '$');
            }
            break;
        case CompletionSite::Argument:
            add_symbols([](const Symbol& s) { return s.kind == "variable" || s.kind == "target" || s.kind == "project"; });
            for (const std::string& w : cmake_argument_words()) add(w, "keyword", std::string(), std::string(), std::string(), 2);
            for (const DocEntry& d : cmake_variable_docs()) add(d.name, "variable", std::string(), std::string(), d.text, 3);
            add_words(3);
            break;
        case CompletionSite::Property:
            for (const DocEntry& d : bark_property_docs()) add(d.name, "property", d.on, ctx.rest_has_colon ? std::string() : d.name + ": $0;", d.text, 0);
            add_words(4);
            break;
        case CompletionSite::Value:
            add_symbols([](const Symbol& s) { return s.kind == "variable"; });
            for (const DocEntry& d : bark_function_docs()) add(d.name, "function", d.on, d.name + "($0)", d.text, 1);
            for (const std::string& w : bark_value_words()) add(w, "value", std::string(), std::string(), std::string(), 2);
            add_words(3);
            break;
        case CompletionSite::Selector:
            for (const DocEntry& d : grove_tag_docs()) add(d.name, "tag", d.on.empty() ? d.text : d.on, std::string(), d.text, 0);
            add_words(3);
            break;
        case CompletionSite::Pseudo:
            for (const DocEntry& d : bark_pseudo_docs()) add(d.name, "keyword", std::string(), std::string(), d.text, 0);
            break;
        case CompletionSite::ClassName:
        case CompletionSite::IdName:
        case CompletionSite::AttributeValue:
            add_extra(0);
            add_words(3);
            break;
        case CompletionSite::Tag:
            for (const DocEntry& d : grove_tag_docs()) add(d.name, "tag", d.text, std::string(), std::string(), 0);
            break;
        case CompletionSite::ClosingTag:
            if (!ctx.open_tag.empty()) add(ctx.open_tag, "tag", "closes <" + ctx.open_tag + ">", ctx.open_tag + ">", std::string(), -1);
            for (const DocEntry& d : grove_tag_docs()) add(d.name, "tag", d.text, d.name + ">", std::string(), 1);
            break;
        case CompletionSite::Attribute:
            for (const DocEntry& d : grove_attribute_docs()) {
                const bool fits = d.on == ctx.tag || "input_" + d.on == ctx.tag;
                add(d.name, "attribute", d.on, ctx.rest_has_quote ? std::string() : d.name + "=\"$0\"", d.text, fits ? 0 : 1);
            }
            break;
    }
    list.items = index.query(ctx.typed, 300);
    return list;
}

} // namespace canopy
