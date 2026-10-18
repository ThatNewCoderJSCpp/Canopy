#include <canopy/code_intel.hpp>
#include <canopy/language_docs.hpp>

#include <grove/input/input.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

std::string trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return std::string();
    const std::size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string ext_of(const std::string& path) { return lower(fs::path(path).extension().string()); }

std::string family_of_path(const std::string& path) {
    const std::string ext = ext_of(path);
    const std::string name = lower(fs::path(path).filename().string());
    if (name == "cmakelists.txt" || ext == ".cmake") return "cmake";
    if (ext == ".c" || ext == ".h" || ext == ".cpp" || ext == ".hpp" || ext == ".cc" || ext == ".cxx" || ext == ".hh" || ext == ".hxx" || ext == ".ipp" || ext == ".tpp" || ext == ".inl") return "c";
    if (ext == ".bark") return "bark";
    if (ext == ".grove") return "grove";
    return std::string();
}

std::string family_of_language(const std::string& language) {
    if (language == "c" || language == "cpp") return "c";
    return language;
}

std::string shorten(std::string s, std::size_t most = 300) {
    if (s.size() > most) s = s.substr(0, most) + "\xE2\x80\xA6";
    return s;
}

std::string code_block(const std::string& language, const std::string& code) { return "```" + language + "\n" + code + "\n```\n"; }

std::string inline_code(const std::string& text) {
    std::string t = shorten(text, 160);
    std::replace(t.begin(), t.end(), '`', '\'');
    return "`" + t + "`";
}

std::string doc_above(const std::vector<std::string>& raw, std::size_t line, const char* marker) {
    std::vector<std::string> out;
    std::size_t i = line;
    const std::string mark(marker);
    while (i > 0) {
        const std::string t = trim(raw[i - 1]);
        if (mark == "//" && t.size() >= 2 && t.compare(t.size() - 2, 2, "*/") == 0) {
            std::vector<std::string> block;
            std::size_t j = i;
            bool opened = false;
            while (j > 0) {
                std::string b = trim(raw[j - 1]);
                const bool start = b.rfind("/*", 0) == 0;
                if (start) b = b.substr(b.rfind("/**", 0) == 0 ? 3 : 2);
                if (b.size() >= 2 && b.compare(b.size() - 2, 2, "*/") == 0) b = b.substr(0, b.size() - 2);
                b = trim(b);
                if (!b.empty() && b[0] == '*') b = trim(b.substr(1));
                block.push_back(b);
                --j;
                if (start) {
                    opened = true;
                    break;
                }
            }
            if (!opened) break;
            for (auto it = block.begin(); it != block.end(); ++it) {
                if (!it->empty()) out.push_back(*it);
            }
            i = j;
            continue;
        }
        if (t.rfind(mark, 0) != 0) break;
        std::string body = t.substr(mark.size());
        while (!body.empty() && (body[0] == '/' || body[0] == '!' || body[0] == '#')) body.erase(body.begin());
        out.push_back(trim(body));
        --i;
    }
    std::reverse(out.begin(), out.end());
    std::string joined;
    for (const std::string& s : out) {
        if (s.empty()) continue;
        if (!joined.empty()) joined += ' ';
        joined += s;
    }
    return shorten(joined, 600);
}

struct Tok {
    std::string text;
    std::size_t line = 0;
    std::size_t col = 0;
    bool ident = false;
};

std::vector<Tok> tokenize(const std::vector<std::string>& lines) {
    std::vector<Tok> out;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::string& l = lines[li];
        std::size_t i = 0;
        while (i < l.size()) {
            const char c = l[i];
            if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
                continue;
            }
            if (ident_start(c)) {
                std::size_t e = i;
                while (e < l.size() && ident_char(l[e])) ++e;
                out.push_back(Tok{ l.substr(i, e - i), li, i, true });
                i = e;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t e = i;
                while (e < l.size() && (ident_char(l[e]) || l[e] == '.' || l[e] == '\'')) ++e;
                out.push_back(Tok{ l.substr(i, e - i), li, i, false });
                i = e;
                continue;
            }
            static const char* pairs[] = { "::", "->", "&&", "||", "<<", ">>", "==", "!=", "<=", ">=" };
            bool two = false;
            for (const char* p : pairs) {
                if (l.compare(i, 2, p) == 0) {
                    out.push_back(Tok{ p, li, i, false });
                    i += 2;
                    two = true;
                    break;
                }
            }
            if (two) continue;
            out.push_back(Tok{ std::string(1, c), li, i, false });
            ++i;
        }
    }
    return out;
}

const std::set<std::string>& not_a_declarer() {
    static const std::set<std::string> k = {
        "return", "else", "case", "new", "delete", "throw", "sizeof", "co_return", "co_yield", "co_await", "goto", "if", "while", "for", "switch",
        "do", "typedef", "using", "operator", "decltype", "alignof", "static_assert", "and", "or", "not",
    };
    return k;
}

const std::set<std::string>& not_a_function() {
    static const std::set<std::string> k = {
        "if", "for", "while", "switch", "return", "sizeof", "alignof", "decltype", "catch", "static_assert", "alignas", "noexcept", "throw", "defined",
        "typeid", "requires", "operator", "co_await",
    };
    return k;
}

std::string join_raw(const std::vector<std::string>& raw, std::size_t l1, std::size_t c1, std::size_t l2, std::size_t c2) {
    std::string out;
    for (std::size_t l = l1; l <= l2 && l < raw.size(); ++l) {
        std::string part = raw[l];
        if (l == l2) part = part.substr(0, std::min(part.size(), c2));
        if (l == l1) part = part.substr(std::min(part.size(), c1));
        part = trim(part);
        if (part.empty()) continue;
        if (!out.empty()) out += ' ';
        out += part;
    }
    return shorten(out, 260);
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

class ExprParser {
public:
    ExprParser(const std::string& text, const std::function<std::optional<std::string>(const std::string&)>& lookup, int depth) : m_text(text), m_lookup(lookup), m_depth(depth) {}

    std::optional<std::int64_t> run() {
        auto v = ternary();
        skip();
        if (!v || m_at != m_text.size()) return std::nullopt;
        return v;
    }

private:
    void skip() {
        while (m_at < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_at]))) ++m_at;
    }

    bool eat(const char* op) {
        skip();
        const std::string o(op);
        if (m_text.compare(m_at, o.size(), o) != 0) return false;
        if (o.size() == 1 && m_at + 1 < m_text.size()) {
            const char n = m_text[m_at + 1];
            if ((o == "<" || o == ">") && (n == o[0] || n == '=')) return false;
            if ((o == "&" || o == "|") && n == o[0]) return false;
            if ((o == "!" || o == "=") && n == '=') return false;
        }
        m_at += o.size();
        return true;
    }

    std::optional<std::int64_t> ternary() {
        auto c = binary(0);
        if (!c) return std::nullopt;
        if (!eat("?")) return c;
        auto a = ternary();
        if (!a || !eat(":")) return std::nullopt;
        auto b = ternary();
        if (!b) return std::nullopt;
        return *c ? a : b;
    }

    std::optional<std::int64_t> binary(int level) {
        static const std::vector<std::vector<const char*>> levels = {
            { "||" }, { "&&" }, { "|" }, { "^" }, { "&" }, { "==", "!=" }, { "<=", ">=", "<", ">" }, { "<<", ">>" }, { "+", "-" }, { "*", "/", "%" },
        };
        if (level >= static_cast<int>(levels.size())) return unary();
        auto left = binary(level + 1);
        if (!left) return std::nullopt;
        while (true) {
            const char* found = nullptr;
            for (const char* op : levels[static_cast<std::size_t>(level)]) {
                if (eat(op)) {
                    found = op;
                    break;
                }
            }
            if (!found) return left;
            auto right = binary(level + 1);
            if (!right) return std::nullopt;
            const std::string op(found);
            const std::int64_t a = *left;
            const std::int64_t b = *right;
            if (op == "||") left = (a || b) ? 1 : 0;
            else if (op == "&&") left = (a && b) ? 1 : 0;
            else if (op == "|") left = a | b;
            else if (op == "^") left = a ^ b;
            else if (op == "&") left = a & b;
            else if (op == "==") left = a == b;
            else if (op == "!=") left = a != b;
            else if (op == "<=") left = a <= b;
            else if (op == ">=") left = a >= b;
            else if (op == "<") left = a < b;
            else if (op == ">") left = a > b;
            else if (op == "<<") left = (b < 0 || b > 63) ? 0 : static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << b);
            else if (op == ">>") left = (b < 0 || b > 63) ? 0 : a >> b;
            else if (op == "+") left = a + b;
            else if (op == "-") left = a - b;
            else if (op == "*") left = a * b;
            else if (op == "/") {
                if (b == 0) return std::nullopt;
                left = a / b;
            } else if (op == "%") {
                if (b == 0) return std::nullopt;
                left = a % b;
            }
        }
    }

    std::optional<std::int64_t> unary() {
        if (eat("-")) {
            auto v = unary();
            return v ? std::optional<std::int64_t>(-*v) : std::nullopt;
        }
        if (eat("+")) return unary();
        if (eat("~")) {
            auto v = unary();
            return v ? std::optional<std::int64_t>(~*v) : std::nullopt;
        }
        if (eat("!")) {
            auto v = unary();
            return v ? std::optional<std::int64_t>(!*v) : std::nullopt;
        }
        return primary();
    }

    std::optional<std::int64_t> primary() {
        skip();
        if (m_at >= m_text.size()) return std::nullopt;
        if (eat("(")) {
            auto v = ternary();
            if (!v || !eat(")")) return std::nullopt;
            return v;
        }
        const char c = m_text[m_at];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t e = m_at;
            while (e < m_text.size() && (std::isalnum(static_cast<unsigned char>(m_text[e])) || m_text[e] == '\'')) ++e;
            std::string lit = m_text.substr(m_at, e - m_at);
            m_at = e;
            lit.erase(std::remove(lit.begin(), lit.end(), '\''), lit.end());
            while (!lit.empty() && (lit.back() == 'u' || lit.back() == 'U' || lit.back() == 'l' || lit.back() == 'L')) lit.pop_back();
            int base = 10;
            std::size_t skip_prefix = 0;
            if (lit.size() > 2 && lit[0] == '0' && (lit[1] == 'x' || lit[1] == 'X')) base = 16, skip_prefix = 2;
            else if (lit.size() > 2 && lit[0] == '0' && (lit[1] == 'b' || lit[1] == 'B')) base = 2, skip_prefix = 2;
            else if (lit.size() > 1 && lit[0] == '0') base = 8, skip_prefix = 1;
            const std::string digits = lit.substr(skip_prefix);
            if (digits.empty()) return lit == "0" ? std::optional<std::int64_t>(0) : std::nullopt;
            char* end = nullptr;
            const unsigned long long v = std::strtoull(digits.c_str(), &end, base);
            if (!end || *end != '\0') return std::nullopt;
            return static_cast<std::int64_t>(v);
        }
        if (c == '\'' && m_at + 2 < m_text.size()) {
            std::size_t e = m_text.find('\'', m_at + 1);
            if (e == std::string::npos) return std::nullopt;
            const std::string body = m_text.substr(m_at + 1, e - m_at - 1);
            m_at = e + 1;
            if (body.size() == 1) return static_cast<unsigned char>(body[0]);
            if (body == "\\n") return 10;
            if (body == "\\t") return 9;
            if (body == "\\0") return 0;
            return std::nullopt;
        }
        if (ident_start(c)) {
            std::size_t e = m_at;
            while (e < m_text.size() && (ident_char(m_text[e]) || (m_text[e] == ':' && e + 1 < m_text.size() && m_text[e + 1] == ':'))) {
                e += m_text[e] == ':' ? 2 : 1;
            }
            std::string name = m_text.substr(m_at, e - m_at);
            m_at = e;
            if (name == "true") return 1;
            if (name == "false" || name == "nullptr" || name == "NULL") return 0;
            const std::size_t scope = name.rfind("::");
            if (scope != std::string::npos) name = name.substr(scope + 2);
            if (!m_lookup || m_depth > 8) return std::nullopt;
            const std::optional<std::string> body = m_lookup(name);
            if (!body) return std::nullopt;
            return ExprParser(*body, m_lookup, m_depth + 1).run();
        }
        return std::nullopt;
    }

    const std::string& m_text;
    const std::function<std::optional<std::string>(const std::string&)>& m_lookup;
    int m_depth = 0;
    std::size_t m_at = 0;
};

std::string with_separators(std::string digits, std::size_t group) {
    std::string out;
    std::size_t count = 0;
    for (std::size_t i = digits.size(); i > 0; --i) {
        if (count && count % group == 0) out.insert(out.begin(), '\'');
        out.insert(out.begin(), digits[i - 1]);
        ++count;
    }
    return out;
}

std::string integer_forms(std::int64_t v) {
    char hex[32];
    std::snprintf(hex, sizeof hex, "0x%llx", static_cast<unsigned long long>(v));
    std::string bin;
    std::uint64_t u = static_cast<std::uint64_t>(v);
    if (u == 0) bin = "0";
    while (u) {
        bin.insert(bin.begin(), static_cast<char>('0' + (u & 1)));
        u >>= 1;
    }
    std::string out = inline_code(std::to_string(v)) + " \xC2\xB7 " + inline_code(hex);
    if (v >= 0 && v < (1ll << 32)) out += " \xC2\xB7 " + inline_code("0b" + with_separators(bin, 4));
    if (v > 32 && v < 127) out += " \xC2\xB7 " + inline_code(std::string("'") + static_cast<char>(v) + "'");
    return out;
}

std::size_t utf8_decode(const std::string& s, std::size_t i, std::uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (std::size_t k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    return len;
}

std::string open_action(const CodeLocation& where) { return "open:" + std::to_string(where.line) + ":" + std::to_string(where.column) + ":" + where.file; }

std::string location_label(const std::string& root, const CodeLocation& where) {
    std::error_code ec;
    std::string shown = where.file;
    if (!root.empty()) {
        const fs::path rel = fs::relative(where.file, root, ec);
        if (!ec && !rel.empty() && rel.native().rfind(fs::path("..").native(), 0) != 0) shown = rel.generic_string();
    }
    return shown + ":" + std::to_string(where.line + 1);
}

struct SymbolCompare {
    bool operator()(const Symbol& a, const Symbol& b) const { return a.definition > b.definition; }
};

} // namespace

std::vector<std::string> split_text_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return lines;
}

std::pair<std::size_t, std::size_t> word_bounds(const std::string& line, std::size_t column, bool dashes) {
    auto part = [&](char c) { return ident_char(c) || (dashes && c == '-'); };
    if (column >= line.size() || !part(line[column])) return { column, column };
    std::size_t b = column;
    std::size_t e = column;
    while (b > 0 && part(line[b - 1])) --b;
    while (e < line.size() && part(line[e])) ++e;
    return { b, e };
}

std::string mask_c_like(const std::string& text) {
    std::string out = text;
    std::size_t i = 0;
    const std::size_t n = text.size();
    auto blank = [&](std::size_t a, std::size_t b) {
        for (std::size_t k = a; k < b && k < n; ++k) {
            if (out[k] != '\n') out[k] = ' ';
        }
    };
    while (i < n) {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            const std::size_t e = text.find('\n', i);
            const std::size_t stop = e == std::string::npos ? n : e;
            blank(i, stop);
            i = stop;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const std::size_t e = text.find("*/", i + 2);
            const std::size_t stop = e == std::string::npos ? n : e + 2;
            blank(i, stop);
            i = stop;
            continue;
        }
        if (c == 'R' && i + 1 < n && text[i + 1] == '"' && (i == 0 || !ident_char(text[i - 1]))) {
            const std::size_t open = text.find('(', i + 2);
            if (open != std::string::npos && open - i < 20) {
                const std::string close = ")" + text.substr(i + 2, open - i - 2) + "\"";
                const std::size_t e = text.find(close, open);
                const std::size_t stop = e == std::string::npos ? n : e + close.size();
                blank(i + 1, stop);
                i = stop;
                continue;
            }
        }
        if (c == '"' || (c == '\'' && !(i > 0 && std::isxdigit(static_cast<unsigned char>(text[i - 1])) && i + 1 < n && std::isxdigit(static_cast<unsigned char>(text[i + 1]))))) {
            std::size_t e = i + 1;
            while (e < n && text[e] != c && text[e] != '\n') {
                if (text[e] == '\\') ++e;
                ++e;
            }
            blank(i + 1, std::min(e, n));
            i = std::min(e + 1, n);
            continue;
        }
        ++i;
    }
    return out;
}

std::vector<Symbol> scan_c_symbols(const std::string& text, const std::string& file) {
    std::vector<Symbol> out;
    const std::vector<std::string> raw = split_text_lines(text);
    std::vector<std::string> masked = split_text_lines(mask_c_like(text));
    for (std::size_t i = 0; i < masked.size(); ++i) {
        const std::string t = trim(masked[i]);
        if (t.empty() || t[0] != '#') continue;
        std::size_t last = i;
        while (last < masked.size() && !masked[last].empty() && masked[last].back() == '\\') ++last;
        std::string joined;
        for (std::size_t k = i; k <= last && k < raw.size(); ++k) {
            std::string part = raw[k];
            if (!part.empty() && part.back() == '\\') part.pop_back();
            joined += (joined.empty() ? "" : " ") + trim(part);
        }
        std::string body = trim(trim(masked[i]).substr(1));
        if (body.rfind("define", 0) == 0) {
            const std::size_t at = raw[i].find("define");
            std::size_t p = at + 6;
            while (p < raw[i].size() && (raw[i][p] == ' ' || raw[i][p] == '\t')) ++p;
            std::size_t e = p;
            while (e < raw[i].size() && ident_char(raw[i][e])) ++e;
            if (e > p) {
                Symbol s;
                s.name = raw[i].substr(p, e - p);
                s.kind = "macro";
                s.file = file;
                s.line = i;
                s.column = p;
                s.signature = shorten(joined, 260);
                std::string rest = joined.substr(std::min(joined.size(), joined.find(s.name) + s.name.size()));
                if (!rest.empty() && rest[0] == '(') {
                    const std::size_t close = rest.find(')');
                    rest = close == std::string::npos ? std::string() : rest.substr(close + 1);
                    s.kind = "function macro";
                }
                const std::string masked_rest = trim(mask_c_like(rest));
                s.value = masked_rest.empty() ? std::string() : trim(rest);
                s.doc = doc_above(raw, i, "//");
                out.push_back(std::move(s));
            }
        }
        for (std::size_t k = i; k <= last && k < masked.size(); ++k) masked[k].assign(masked[k].size(), ' ');
        i = last;
    }

    const std::vector<Tok> T = tokenize(masked);
    struct Scope {
        int kind = 0;
        std::string name;
    };
    std::vector<Scope> scopes;
    int pending = 3;
    std::string pending_name;
    std::set<std::size_t> bodies;
    std::size_t stmt = 0;
    auto in_code = [&] {
        for (const Scope& s : scopes) {
            if (s.kind >= 2) return true;
        }
        return false;
    };
    auto add = [&](const std::string& name, const std::string& kind, const Tok& at, std::size_t end_line, std::size_t end_col, bool definition) {
        Symbol s;
        s.name = name;
        s.kind = kind;
        s.file = file;
        s.line = at.line;
        s.column = at.col;
        const Tok& first = T[std::min(stmt, T.size() - 1)];
        std::size_t start_line = first.line;
        std::size_t start_col = first.col;
        if (start_line > at.line) {
            start_line = at.line;
            start_col = 0;
        }
        s.signature = join_raw(raw, start_line, start_col, end_line, end_col);
        s.doc = doc_above(raw, start_line, "//");
        s.definition = definition;
        out.push_back(std::move(s));
        return &out.back();
    };
    auto match_close = [&](std::size_t open, const char* o, const char* c) {
        int depth = 0;
        for (std::size_t j = open; j < T.size(); ++j) {
            if (T[j].text == o) ++depth;
            else if (T[j].text == c && --depth == 0) return j;
        }
        return T.size();
    };
    for (std::size_t k = 0; k < T.size(); ++k) {
        const Tok& tok = T[k];
        if (tok.text == "{") {
            if (bodies.count(k)) scopes.push_back(Scope{ 3, std::string() });
            else if (in_code()) scopes.push_back(Scope{ 3, std::string() });
            else {
                scopes.push_back(Scope{ pending, pending_name });
            }
            pending = 3;
            pending_name.clear();
            stmt = k + 1;
            continue;
        }
        if (tok.text == "}") {
            if (!scopes.empty()) scopes.pop_back();
            stmt = k + 1;
            pending = 3;
            continue;
        }
        if (tok.text == ";") {
            if (!in_code() && stmt < k) {
                const std::string& head = T[stmt].text;
                if (head == "typedef") {
                    std::size_t name_at = 0;
                    for (std::size_t j = stmt; j + 2 < k; ++j) {
                        if (T[j].text == "(" && T[j + 1].text == "*" && T[j + 2].ident) name_at = j + 2;
                    }
                    if (!name_at) {
                        int depth = 0;
                        for (std::size_t j = stmt; j < k; ++j) {
                            if (T[j].text == "(" || T[j].text == "[" || T[j].text == "{") ++depth;
                            else if (T[j].text == ")" || T[j].text == "]" || T[j].text == "}") --depth;
                            else if (depth == 0 && T[j].ident) name_at = j;
                        }
                    }
                    if (name_at) add(T[name_at].text, "type", T[name_at], tok.line, tok.col + 1, true);
                } else {
                    static const std::set<std::string> skip_heads = { "return", "using", "friend", "static_assert", "namespace", "template", "class", "struct", "enum", "union", "extern", "goto", "break", "continue", "public", "private", "protected", "operator" };
                    bool paren = false;
                    std::size_t name_at = 0;
                    int depth = 0;
                    for (std::size_t j = stmt; j < k; ++j) {
                        const std::string& t = T[j].text;
                        if (t == "(" && depth == 0) paren = true;
                        if (t == "=" || ((t == "{" || t == "[") && depth == 0)) break;
                        if (t == "<" || t == "(" || t == "[") ++depth;
                        else if (t == ">" || t == ")" || t == "]") --depth;
                        else if (depth == 0 && T[j].ident) name_at = j;
                    }
                    if (!paren && name_at > stmt && !skip_heads.count(head) && !not_a_declarer().count(T[name_at - 1].text) && (T[name_at - 1].ident || T[name_at - 1].text == "*" || T[name_at - 1].text == "&" || T[name_at - 1].text == ">")) {
                        const bool field = !scopes.empty() && scopes.back().kind == 1;
                        Symbol* s = add(T[name_at].text, field ? "field" : "variable", T[name_at], tok.line, tok.col + 1, true);
                        for (std::size_t j = stmt; j < k; ++j) {
                            if (T[j].text != "=") continue;
                            s->value = join_raw(masked, T[j].line, T[j].col + 1, tok.line, tok.col);
                            break;
                        }
                    }
                }
            }
            stmt = k + 1;
            pending = 3;
            continue;
        }
        if (tok.text == ":" && k > 0 && (T[k - 1].text == "public" || T[k - 1].text == "private" || T[k - 1].text == "protected")) {
            stmt = k + 1;
            continue;
        }
        if (!tok.ident || in_code()) continue;
        if (tok.text == "namespace" && k + 1 < T.size()) {
            std::size_t j = k + 1;
            std::size_t name_at = 0;
            while (j < T.size() && (T[j].ident || T[j].text == "::")) {
                if (T[j].ident) name_at = j;
                ++j;
            }
            if (name_at && j < T.size() && T[j].text == "{") {
                add(T[name_at].text, "namespace", T[name_at], T[name_at].line, T[name_at].col + T[name_at].text.size(), true);
                pending = 0;
                pending_name = T[name_at].text;
            } else if (j < T.size() && T[j].text == "{") {
                pending = 0;
            }
            continue;
        }
        if (tok.text == "extern" && k + 2 < T.size() && T[k + 1].text == "\"") continue;
        if (tok.text == "struct" || tok.text == "class" || tok.text == "union" || tok.text == "enum") {
            if (k > 0 && (T[k - 1].text == "<" || T[k - 1].text == "," || T[k - 1].text == "friend")) continue;
            std::size_t j = k + 1;
            const bool is_enum = tok.text == "enum";
            if (is_enum && j < T.size() && (T[j].text == "class" || T[j].text == "struct")) ++j;
            while (j + 1 < T.size() && T[j].text == "[" && T[j + 1].text == "[") j = match_close(j, "[", "]") + 1;
            while (j < T.size() && (T[j].text == "alignas" || T[j].text == "__declspec" || T[j].text == "__attribute__") && j + 1 < T.size() && T[j + 1].text == "(") j = match_close(j + 1, "(", ")") + 1;
            if (j >= T.size() || !T[j].ident) continue;
            const std::size_t name_at = j;
            std::size_t after = j + 1;
            if (after < T.size() && T[after].text == "final") ++after;
            if (after >= T.size()) continue;
            const std::string& next = T[after].text;
            if (next != "{" && next != ":" && next != ";") continue;
            if (next == ";" && k != stmt) continue;
            std::size_t body = after;
            while (body < T.size() && T[body].text != "{" && T[body].text != ";") ++body;
            const Tok& end_tok = T[std::min(body, T.size() - 1)];
            add(T[name_at].text, is_enum ? "enum" : tok.text, T[name_at], end_tok.line, end_tok.col, next != ";");
            if (body < T.size() && T[body].text == "{") {
                pending = is_enum ? 2 : 1;
                pending_name = T[name_at].text;
                if (is_enum) {
                    const std::size_t close = match_close(body, "{", "}");
                    std::size_t item = body + 1;
                    std::int64_t next_value = 0;
                    bool known = true;
                    std::map<std::string, std::string> seen;
                    while (item < close) {
                        if (!T[item].ident) {
                            ++item;
                            continue;
                        }
                        std::size_t stop = item + 1;
                        int depth = 0;
                        while (stop < close && !(depth == 0 && T[stop].text == ",")) {
                            if (T[stop].text == "(" || T[stop].text == "{") ++depth;
                            else if (T[stop].text == ")" || T[stop].text == "}") --depth;
                            ++stop;
                        }
                        Symbol s;
                        s.name = T[item].text;
                        s.kind = "enum member";
                        s.file = file;
                        s.line = T[item].line;
                        s.column = T[item].col;
                        std::string expr;
                        if (item + 1 < stop && T[item + 1].text == "=") {
                            const Tok& last_tok = T[stop - 1];
                            expr = join_raw(masked, T[item + 1].line, T[item + 1].col + 1, last_tok.line, last_tok.col + last_tok.text.size());
                        }
                        std::optional<std::int64_t> v;
                        if (!expr.empty()) {
                            v = evaluate_integer(expr, [&](const std::string& n) -> std::optional<std::string> {
                                const auto it = seen.find(n);
                                if (it == seen.end()) return std::nullopt;
                                return it->second;
                            });
                            known = v.has_value();
                        } else if (known) {
                            v = next_value;
                        }
                        if (v) {
                            next_value = *v + 1;
                            s.value = std::to_string(*v);
                            seen[s.name] = s.value;
                        } else {
                            s.value = expr;
                        }
                        s.signature = T[name_at].text + "::" + s.name + (s.value.empty() ? std::string() : " = " + s.value);
                        s.doc = doc_above(raw, s.line, "//");
                        out.push_back(std::move(s));
                        item = stop + 1;
                    }
                }
            }
            continue;
        }
        if (tok.text == "using" && k + 2 < T.size() && T[k + 1].ident && T[k + 2].text == "=") {
            std::size_t end = k + 2;
            while (end < T.size() && T[end].text != ";") ++end;
            const Tok& e = T[std::min(end, T.size() - 1)];
            add(T[k + 1].text, "type", T[k + 1], e.line, e.col, true);
            continue;
        }
        if (k + 1 < T.size() && T[k + 1].text == "(" && !not_a_function().count(tok.text)) {
            bool allowed = false;
            if (k > 0) {
                const Tok& p = T[k - 1];
                if (p.ident && !not_a_declarer().count(p.text)) allowed = true;
                else if (p.text == "*" || p.text == "&" || p.text == "&&" || p.text == ">" || p.text == "~") allowed = true;
                else if (p.text == "::" && k >= 2 && T[k - 2].ident) allowed = k >= 3 ? (T[k - 3].ident || T[k - 3].text == "*" || T[k - 3].text == "&" || T[k - 3].text == ">" || T[k - 3].text == ";" || T[k - 3].text == "}" || T[k - 3].text == "{" || T[k - 2].text == tok.text) : true;
                else if (!scopes.empty() && scopes.back().kind == 1 && scopes.back().name == tok.text && (p.text == ";" || p.text == "{" || p.text == "}" || p.text == ":")) allowed = true;
            }
            if (!allowed) continue;
            const std::size_t close = match_close(k + 1, "(", ")");
            if (close >= T.size()) continue;
            std::size_t j = close + 1;
            while (j < T.size()) {
                const std::string& t = T[j].text;
                if (t == "const" || t == "override" || t == "final" || t == "volatile" || t == "mutable" || t == "&" || t == "&&") {
                    ++j;
                } else if ((t == "noexcept" || t == "throw") && j + 1 < T.size() && T[j + 1].text == "(") {
                    j = match_close(j + 1, "(", ")") + 1;
                } else if (t == "noexcept") {
                    ++j;
                } else if (t == "->") {
                    ++j;
                    while (j < T.size() && T[j].text != "{" && T[j].text != ";" && T[j].text != "=") ++j;
                } else {
                    break;
                }
            }
            if (j >= T.size()) continue;
            const std::string& next = T[j].text;
            if (next == "{") {
                add(tok.text, "function", tok, T[j].line, T[j].col, true);
                bodies.insert(j);
            } else if (next == ":") {
                std::size_t b = j + 1;
                while (b < T.size() && !(T[b].text == "{" && (T[b - 1].text == ")" || T[b - 1].text == "}"))) {
                    if (T[b].text == ";") break;
                    ++b;
                }
                if (b < T.size() && T[b].text == "{") {
                    add(tok.text, "function", tok, T[j].line, T[j].col, true);
                    bodies.insert(b);
                }
            } else if (next == ";" || next == "=") {
                const Tok& e = T[close];
                add(tok.text, "function", tok, e.line, e.col + 1, false);
            }
        }
    }
    return out;
}

std::vector<Symbol> scan_cmake_symbols(const std::string& text, const std::string& file) {
    std::vector<Symbol> out;
    const std::vector<std::string> raw = split_text_lines(text);
    std::size_t i = 0;
    std::size_t line = 0;
    std::size_t line_start = 0;
    const std::size_t n = text.size();
    auto advance = [&](std::size_t to) {
        for (; i < to && i < n; ++i) {
            if (text[i] == '\n') {
                ++line;
                line_start = i + 1;
            }
        }
    };
    while (i < n) {
        const char c = text[i];
        if (c == '#') {
            const std::size_t e = text.find('\n', i);
            advance(e == std::string::npos ? n : e);
            continue;
        }
        if (!ident_start(c)) {
            advance(i + 1);
            continue;
        }
        std::size_t e = i;
        while (e < n && ident_char(text[e])) ++e;
        std::size_t p = e;
        while (p < n && (text[p] == ' ' || text[p] == '\t')) ++p;
        if (p >= n || text[p] != '(') {
            advance(e);
            continue;
        }
        const std::string command = lower(text.substr(i, e - i));
        const std::size_t cmd_line = line;
        const std::size_t cmd_col = i - line_start;
        advance(p + 1);
        struct Arg {
            std::string text;
            std::size_t line;
            std::size_t col;
        };
        std::vector<Arg> args;
        int depth = 1;
        while (i < n && depth > 0) {
            const char d = text[i];
            if (d == '\n' || d == ' ' || d == '\t' || d == '\r') {
                advance(i + 1);
                continue;
            }
            if (d == '#') {
                const std::size_t stop = text.find('\n', i);
                advance(stop == std::string::npos ? n : stop);
                continue;
            }
            if (d == '(') {
                ++depth;
                advance(i + 1);
                continue;
            }
            if (d == ')') {
                --depth;
                advance(i + 1);
                continue;
            }
            const std::size_t a_line = line;
            const std::size_t a_col = i - line_start;
            std::string value;
            if (d == '"') {
                std::size_t q = i + 1;
                while (q < n && text[q] != '"') {
                    if (text[q] == '\\') ++q;
                    ++q;
                }
                value = text.substr(i + 1, std::min(q, n) - i - 1);
                advance(std::min(q + 1, n));
            } else {
                std::size_t q = i;
                while (q < n && !std::isspace(static_cast<unsigned char>(text[q])) && text[q] != '(' && text[q] != ')') ++q;
                value = text.substr(i, q - i);
                advance(q);
            }
            args.push_back(Arg{ value, a_line, a_col });
        }
        if (args.empty()) continue;
        Symbol s;
        s.file = file;
        s.line = args[0].line;
        s.column = args[0].col;
        s.name = args[0].text;
        s.signature = join_raw(raw, cmd_line, cmd_col, line, raw.size() > line ? raw[line].size() : 0);
        const std::size_t close = s.signature.rfind(')');
        if (close != std::string::npos) s.signature = s.signature.substr(0, close + 1);
        s.doc = doc_above(raw, cmd_line, "#");
        auto rest = [&](std::size_t from, std::set<std::string> stop) {
            std::string v;
            for (std::size_t k = from; k < args.size(); ++k) {
                if (stop.count(args[k].text)) break;
                if (!v.empty()) v += ';';
                v += args[k].text;
            }
            return v;
        };
        if (command == "function" || command == "macro") {
            s.kind = "command";
            s.name = lower(s.name);
            out.push_back(s);
        } else if (command == "set") {
            s.kind = "variable";
            s.value = rest(1, { "CACHE", "PARENT_SCOPE" });
            out.push_back(s);
        } else if (command == "option") {
            s.kind = "variable";
            s.value = args.size() > 2 ? args[2].text : std::string("OFF");
            if (args.size() > 1 && s.doc.empty()) s.doc = args[1].text;
            out.push_back(s);
        } else if (command == "list" && args.size() > 1 && (args[0].text == "APPEND" || args[0].text == "PREPEND")) {
            s.kind = "variable";
            s.name = args[1].text;
            s.line = args[1].line;
            s.column = args[1].col;
            s.value = rest(2, {});
            s.definition = false;
            out.push_back(s);
        } else if (command == "project") {
            s.kind = "variable";
            s.name = "PROJECT_NAME";
            s.value = args[0].text;
            out.push_back(s);
            Symbol p = s;
            p.kind = "project";
            p.name = args[0].text;
            p.value.clear();
            out.push_back(p);
        } else if (command == "add_executable" || command == "add_library" || command == "add_custom_target") {
            s.kind = "target";
            out.push_back(s);
        }
    }
    return out;
}

std::vector<Symbol> scan_bark_symbols(const std::string& text, const std::string& file) {
    std::vector<Symbol> out;
    const std::vector<std::string> raw = split_text_lines(text);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const std::string t = trim(raw[i]);
        if (t.empty() || t[0] != '$') continue;
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        Symbol s;
        s.name = trim(t.substr(0, eq));
        s.kind = "variable";
        s.file = file;
        s.line = i;
        s.column = raw[i].find('$');
        std::string value = trim(t.substr(eq + 1));
        if (!value.empty() && value.back() == ';') value.pop_back();
        s.value = trim(value);
        s.signature = t;
        s.doc = doc_above(raw, i, "//");
        out.push_back(std::move(s));
    }
    return out;
}

std::optional<std::int64_t> evaluate_integer(const std::string& expression, const std::function<std::optional<std::string>(const std::string&)>& lookup) {
    if (trim(expression).empty()) return std::nullopt;
    return ExprParser(expression, lookup, 0).run();
}

std::string number_preview(const std::string& literal) {
    if (auto v = evaluate_integer(literal)) return "**Value:** " + integer_forms(*v);
    std::string clean = literal;
    clean.erase(std::remove(clean.begin(), clean.end(), '\''), clean.end());
    while (!clean.empty() && (clean.back() == 'f' || clean.back() == 'F' || clean.back() == 'l' || clean.back() == 'L')) clean.pop_back();
    char* end = nullptr;
    const double d = std::strtod(clean.c_str(), &end);
    if (end && *end == '\0' && !clean.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.17g", d);
        return "**Value:** " + inline_code(buf);
    }
    return std::string();
}

std::string color_preview(const std::string& literal) {
    const std::optional<grove::bark::Rgba> c = grove::parse_color_text(literal);
    if (!c) return std::string();
    const std::string hex = grove::format_color(*c);
    return "[](color:" + hex + ") " + inline_code(hex) + " \xC2\xB7 " + inline_code(grove::format_color(*c, "rgb")) + " \xC2\xB7 " + inline_code(grove::format_color(*c, "hsl"));
}

std::string hover_link(const std::string& label, const CodeLocation& where) { return "[" + label + "](" + open_action(where) + ")"; }

std::optional<CodeLocation> parse_hover_link(const std::string& action) {
    if (action.rfind("open:", 0) != 0) return std::nullopt;
    const std::size_t a = action.find(':', 5);
    const std::size_t b = a == std::string::npos ? std::string::npos : action.find(':', a + 1);
    if (b == std::string::npos) return std::nullopt;
    CodeLocation where;
    where.line = static_cast<std::size_t>(std::strtoull(action.substr(5, a - 5).c_str(), nullptr, 10));
    where.column = static_cast<std::size_t>(std::strtoull(action.substr(a + 1, b - a - 1).c_str(), nullptr, 10));
    where.file = action.substr(b + 1);
    return where;
}

void CodeIntel::set_workspace(const std::string& root) {
    if (root == m_root) return;
    m_root = root;
    m_files.clear();
    m_listing.clear();
    m_listed_at.clear();
}

void CodeIntel::set_open_texts(std::function<std::optional<std::string>(const std::string&)> provider) { m_open = std::move(provider); }

void CodeIntel::set_include_dirs(std::vector<std::string> dirs) { m_include_dirs = std::move(dirs); }

void CodeIntel::set_system_include_dirs(std::function<std::vector<std::string>()> provider) {
    m_system_provider = std::move(provider);
    m_system_dirs.reset();
}

std::optional<std::string> CodeIntel::text_of(const std::string& path) {
    if (m_open) {
        if (auto t = m_open(path)) return t;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return std::nullopt;
    return read_file(path);
}

std::vector<std::string> CodeIntel::workspace_files(const std::string& family) {
    const auto now = std::chrono::steady_clock::now();
    const auto at = m_listed_at.find(family);
    if (at != m_listed_at.end() && now - at->second < std::chrono::seconds(5)) return m_listing[family];
    std::vector<std::string> files;
    std::error_code ec;
    if (!m_root.empty() && fs::is_directory(m_root, ec)) {
        static const std::set<std::string> skipped = { ".git", ".svn", "node_modules", ".vs", ".vscode", ".idea", "_deps", "out", ".cache" };
        fs::recursive_directory_iterator it(m_root, fs::directory_options::skip_permission_denied, ec);
        std::size_t seen = 0;
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (++seen > 20000 || files.size() > 3000) break;
            const fs::directory_entry& e = *it;
            const std::string name = e.path().filename().string();
            if (e.is_directory(ec)) {
                const std::string l = lower(name);
                if (skipped.count(l) || l.rfind("build", 0) == 0 || l.rfind("cmake-build", 0) == 0) it.disable_recursion_pending();
                continue;
            }
            if (family_of_path(e.path().string()) != family) continue;
            if (e.file_size(ec) > 2 * 1024 * 1024) continue;
            files.push_back(e.path().string());
        }
    }
    m_listing[family] = files;
    m_listed_at[family] = now;
    return files;
}

const std::vector<Symbol>& CodeIntel::symbols_in(const std::string& path, const std::string& family) {
    static const std::vector<Symbol> none;
    if (m_open) {
        if (auto t = m_open(path)) {
            FileEntry& e = m_files["open:" + path];
            e.symbols = family == "c" ? scan_c_symbols(*t, path) : family == "cmake" ? scan_cmake_symbols(*t, path) : family == "bark" ? scan_bark_symbols(*t, path) : std::vector<Symbol>{};
            return e.symbols;
        }
    }
    std::error_code ec;
    const auto time = fs::last_write_time(path, ec);
    if (ec) return none;
    const std::uintmax_t size = fs::file_size(path, ec);
    auto it = m_files.find(path);
    if (it != m_files.end() && it->second.time == time && it->second.size == size) return it->second.symbols;
    FileEntry entry;
    entry.time = time;
    entry.size = size;
    const std::string text = read_file(path);
    entry.symbols = family == "c" ? scan_c_symbols(text, path) : family == "cmake" ? scan_cmake_symbols(text, path) : family == "bark" ? scan_bark_symbols(text, path) : std::vector<Symbol>{};
    FileEntry& stored = m_files[path];
    stored = std::move(entry);
    return stored.symbols;
}

std::vector<std::string> CodeIntel::includes_of(const std::string& path, const std::string& text) {
    std::vector<std::string> out;
    for (const std::string& line : split_text_lines(text)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] != '#') continue;
        const std::string body = trim(t.substr(1));
        if (body.rfind("include", 0) != 0) continue;
        const std::size_t q = body.find_first_of("\"<");
        if (q == std::string::npos) continue;
        const char close = body[q] == '"' ? '"' : '>';
        const std::size_t e = body.find(close, q + 1);
        if (e == std::string::npos) continue;
        if (auto p = include_path(path, body.substr(q + 1, e - q - 1), close == '>')) out.push_back(*p);
    }
    return out;
}

std::optional<std::string> CodeIntel::include_path(const std::string& from_file, const std::string& name, bool angled) {
    std::vector<std::string> dirs;
    if (!angled) dirs.push_back(fs::path(from_file).parent_path().string());
    for (const std::string& d : m_include_dirs) dirs.push_back(d);
    if (!m_root.empty()) {
        dirs.push_back(m_root);
        for (const char* sub : { "Include", "include", "src", "Source", "source" }) dirs.push_back((fs::path(m_root) / sub).string());
    }
    if (angled) dirs.push_back(fs::path(from_file).parent_path().string());
    if (!m_system_dirs && m_system_provider) m_system_dirs = m_system_provider();
    if (m_system_dirs) {
        for (const std::string& d : *m_system_dirs) dirs.push_back(d);
    }
    std::error_code ec;
    for (const std::string& d : dirs) {
        const fs::path p = fs::path(d) / name;
        if (fs::is_regular_file(p, ec)) return fs::weakly_canonical(p, ec).string();
    }
    return std::nullopt;
}

std::vector<Symbol> CodeIntel::find(const std::string& name, const std::string& family, const std::string& near_file) {
    std::vector<Symbol> found;
    std::set<std::string> visited;
    auto take = [&](const std::string& path) {
        if (!visited.insert(path).second) return;
        for (const Symbol& s : symbols_in(path, family)) {
            if (s.name == name) found.push_back(s);
        }
    };
    if (!near_file.empty()) {
        take(near_file);
        if (family == "c") {
            std::vector<std::string> frontier = { near_file };
            for (int depth = 0; depth < 3 && !frontier.empty(); ++depth) {
                std::vector<std::string> next;
                for (const std::string& f : frontier) {
                    const std::optional<std::string> t = text_of(f);
                    if (!t) continue;
                    for (const std::string& inc : includes_of(f, *t)) {
                        if (visited.count(inc)) continue;
                        take(inc);
                        next.push_back(inc);
                    }
                }
                frontier = std::move(next);
            }
            const std::string stem = fs::path(near_file).stem().string();
            for (const std::string& f : workspace_files(family)) {
                if (fs::path(f).stem().string() == stem) take(f);
            }
        }
    }
    const std::size_t before_workspace = found.size();
    if (found.empty() || family != "c") {
        for (const std::string& f : workspace_files(family)) take(f);
    } else {
        bool definition = false;
        for (const Symbol& s : found) definition = definition || s.definition;
        if (!definition) {
            for (const std::string& f : workspace_files(family)) take(f);
        }
    }
    (void)before_workspace;
    std::stable_sort(found.begin(), found.end(), SymbolCompare());
    return found;
}

std::optional<std::string> CodeIntel::macro_value(const std::string& name, const std::string& near_file, int depth) {
    if (depth > 8) return std::nullopt;
    for (const Symbol& s : find(name, "c", near_file)) {
        if ((s.kind == "macro" || s.kind == "enum member") && !s.value.empty()) return s.value;
        if (s.kind == "variable" && !s.value.empty() && s.signature.find("const") != std::string::npos) return s.value;
    }
    return std::nullopt;
}

std::optional<HoverResult> CodeIntel::hover(const IntelRequest& request) {
    const std::string family = family_of_language(request.language);
    if (family == "c") return c_hover(request);
    if (family == "cmake") return cmake_hover(request);
    if (family == "bark") return bark_hover(request);
    if (family == "grove") return grove_hover(request);
    return std::nullopt;
}

std::optional<CodeLocation> CodeIntel::definition(const IntelRequest& request) {
    const std::string family = family_of_language(request.language);
    if (family == "c") return c_definition(request);
    if (family == "cmake") return cmake_definition(request);
    if (family == "bark") return bark_definition(request);
    if (family == "grove") return grove_definition(request);
    return std::nullopt;
}

namespace {

struct IncludeAt {
    std::string name;
    bool angled = false;
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::optional<IncludeAt> include_at(const std::string& line, std::size_t column) {
    const std::string t = trim(line);
    if (t.empty() || t[0] != '#') return std::nullopt;
    const std::size_t inc = line.find("include");
    if (inc == std::string::npos) return std::nullopt;
    const std::size_t q = line.find_first_of("\"<", inc);
    if (q == std::string::npos) return std::nullopt;
    const char close = line[q] == '"' ? '"' : '>';
    const std::size_t e = line.find(close, q + 1);
    if (e == std::string::npos || column < q || column > e) return std::nullopt;
    return IncludeAt{ line.substr(q + 1, e - q - 1), close == '>', q, e + 1 };
}

struct LocalDecl {
    std::size_t line = 0;
    std::size_t column = 0;
    std::string text;
};

std::optional<LocalDecl> find_local(const std::vector<std::string>& masked, const std::vector<std::string>& raw, std::size_t line, std::size_t column, const std::string& word) {
    if (line >= masked.size()) return std::nullopt;
    for (std::size_t l = line + 1; l-- > 0;) {
        const std::string& m = masked[l];
        std::size_t from = 0;
        while (true) {
            const std::size_t at = m.find(word, from);
            if (at == std::string::npos) break;
            from = at + 1;
            if ((at > 0 && ident_char(m[at - 1])) || (at + word.size() < m.size() && ident_char(m[at + word.size()]))) continue;
            if (l == line && at >= column && !(at <= column && column < at + word.size())) continue;
            std::size_t q = at;
            while (q > 0 && (m[q - 1] == ' ' || m[q - 1] == '\t' || m[q - 1] == '*' || m[q - 1] == '&')) --q;
            if (q == 0) continue;
            const char pc = m[q - 1];
            if (!(ident_char(pc) || pc == '>')) continue;
            if (ident_char(pc)) {
                std::size_t wb = q - 1;
                while (wb > 0 && ident_char(m[wb - 1])) --wb;
                const std::string prev = m.substr(wb, q - wb);
                if (not_a_declarer().count(prev) || std::isdigit(static_cast<unsigned char>(prev[0]))) continue;
                static const std::set<std::string> stoppers = { "struct", "class", "enum", "union", "namespace", "public", "private", "protected" };
                if (stoppers.count(prev)) continue;
            }
            std::size_t r = at + word.size();
            while (r < m.size() && (m[r] == ' ' || m[r] == '\t')) ++r;
            if (r < m.size() && std::string("=;,)[{:").find(m[r]) == std::string::npos) continue;
            if (r < m.size() && m[r] == ':' && r + 1 < m.size() && m[r + 1] == ':') continue;
            return LocalDecl{ l, at, trim(raw[l]) };
        }
        const std::string& rl = raw[l];
        if (l != line && !rl.empty() && !std::isspace(static_cast<unsigned char>(rl[0])) && rl[0] != '{' && rl[0] != '#' && rl[0] != '/') {
            if (rl[0] == '}') break;
            if (rl.find('(') != std::string::npos) break;
        }
        if (line - l > 600) break;
    }
    return std::nullopt;
}

std::pair<std::size_t, std::size_t> number_bounds(const std::string& line, std::size_t column) {
    std::size_t b = column;
    std::size_t e = column;
    auto part = [&](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '\'' || c == '_'; };
    while (b > 0 && part(line[b - 1])) --b;
    while (e < line.size() && part(line[e])) ++e;
    return { b, e };
}

} // namespace

std::optional<HoverResult> CodeIntel::c_hover(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const std::string& line = raw[r.line];
    const std::string lang = r.language == "c" ? "c" : "cpp";
    if (auto inc = include_at(line, r.column)) {
        HoverResult h{ r.line, inc->begin, inc->end - inc->begin, std::string() };
        if (auto p = include_path(r.file, inc->name, inc->angled)) h.markdown = hover_link(*p, CodeLocation{ *p, 0, 0, 0 }) + "\nCtrl+click to open it.";
        else h.markdown = "Can't find " + inline_code(inc->name) + " in the include folders.";
        return h;
    }
    const std::vector<std::string> masked = split_text_lines(mask_c_like(r.text));
    const std::string& mline = r.line < masked.size() ? masked[r.line] : line;
    if (r.column < line.size() && mline[r.column] == ' ' && line[r.column] != ' ') {
        if (line[r.column] == '\'' || (r.column > 0 && line.rfind('\'', r.column) != std::string::npos)) {
            const std::size_t open = line.rfind('\'', r.column);
            const std::size_t close = open == std::string::npos ? std::string::npos : line.find('\'', open + 1);
            if (open != std::string::npos && close != std::string::npos && open <= r.column && r.column <= close) {
                const std::string body = line.substr(open + 1, close - open - 1);
                std::optional<std::int64_t> v;
                if (body.size() >= 1 && body[0] != '\\') {
                    std::uint32_t cp = 0;
                    utf8_decode(body, 0, cp);
                    v = cp;
                } else {
                    v = evaluate_integer("'" + body + "'");
                }
                if (v) return HoverResult{ r.line, open, close + 1 - open, "**Character:** " + integer_forms(*v) };
            }
        }
        return std::nullopt;
    }
    if (r.column < line.size() && std::isdigit(static_cast<unsigned char>(line[r.column]))) {
        const auto [b, e] = number_bounds(line, r.column);
        if (b < e && std::isdigit(static_cast<unsigned char>(line[b]))) {
            const std::string preview = number_preview(line.substr(b, e - b));
            if (!preview.empty()) return HoverResult{ r.line, b, e - b, preview };
        }
    }
    const auto [b, e] = word_bounds(line, r.column);
    if (b == e || std::isdigit(static_cast<unsigned char>(line[b]))) return std::nullopt;
    const std::string word = line.substr(b, e - b);
    HoverResult h{ r.line, b, e - b, std::string() };
    const bool member = b > 0 && (line[b - 1] == '.' || (b > 1 && line[b - 1] == '>' && line[b - 2] == '-'));
    if (!member) {
        if (auto local = find_local(masked, raw, r.line, r.column, word)) {
            {
                h.markdown = code_block(lang, local->text);
                const std::size_t eq = local->text.find('=', local->text.find(word));
                if (eq != std::string::npos) {
                    std::string rhs = local->text.substr(eq + 1);
                    const std::size_t semi = rhs.find_first_of(";,");
                    if (semi != std::string::npos) rhs = rhs.substr(0, semi);
                    if (auto v = evaluate_integer(rhs, [&](const std::string& n) { return macro_value(n, r.file, 0); })) h.markdown += "**Value:** " + integer_forms(*v) + "\n";
                }
                return h;
            }
        }
    }
    std::vector<Symbol> symbols = find(word, "c", r.file);
    if (member) {
        std::vector<Symbol> fields;
        for (const Symbol& s : symbols) {
            if (s.kind == "field" || s.kind == "function") fields.push_back(s);
        }
        symbols = fields;
    }
    if (symbols.empty()) {
        if (const DocEntry* d = find_doc(c_word_docs(), word)) {
            h.markdown = code_block(lang, word) + d->text + "\n";
            return h;
        }
        return std::nullopt;
    }
    const Symbol& best = symbols.front();
    std::string sig = best.signature.empty() ? best.name : best.signature;
    if (!sig.empty() && (sig.back() == '{' || sig.back() == ':')) sig = trim(sig.substr(0, sig.size() - 1));
    h.markdown = code_block(lang, sig);
    std::set<std::string> others;
    for (const Symbol& s : symbols) {
        if (s.kind == "function" && s.signature != best.signature) others.insert(s.signature);
    }
    std::size_t shown = 0;
    for (const std::string& o : others) {
        if (++shown > 3) break;
        std::string t = o;
        if (!t.empty() && (t.back() == '{' || t.back() == ':')) t = trim(t.substr(0, t.size() - 1));
        if (t == sig) continue;
        h.markdown += code_block(lang, t);
    }
    if (others.size() > 3) h.markdown += "+" + std::to_string(others.size() - 3) + " more overloads\n";
    std::string doc = best.doc;
    if (doc.empty()) {
        for (const Symbol& s : symbols) {
            if (!s.doc.empty()) {
                doc = s.doc;
                break;
            }
        }
    }
    if (!doc.empty()) h.markdown += doc + "\n";
    if (best.kind == "macro" || best.kind == "enum member" || ((best.kind == "variable" || best.kind == "field") && !best.value.empty())) {
        const std::string expr = best.value;
        if (auto v = evaluate_integer(expr, [&](const std::string& n) { return macro_value(n, r.file, 0); })) h.markdown += "**Value:** " + integer_forms(*v) + "\n";
        else if (!expr.empty() && best.kind == "macro") h.markdown += "**Expands to:** " + inline_code(expr) + "\n";
    }
    h.markdown += "---\n" + best.kind + " in " + hover_link(location_label(m_root, CodeLocation{ best.file, best.line, best.column, 0 }), CodeLocation{ best.file, best.line, best.column, 0 }) + "\n";
    return h;
}

std::optional<CodeLocation> CodeIntel::c_definition(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const std::string& line = raw[r.line];
    if (auto inc = include_at(line, r.column)) {
        if (auto p = include_path(r.file, inc->name, inc->angled)) return CodeLocation{ *p, 0, 0, 0 };
        return std::nullopt;
    }
    const auto [b, e] = word_bounds(line, r.column);
    if (b == e) return std::nullopt;
    const std::string word = line.substr(b, e - b);
    const bool member = b > 0 && (line[b - 1] == '.' || (b > 1 && line[b - 1] == '>' && line[b - 2] == '-'));
    std::vector<Symbol> symbols = find(word, "c", r.file);
    if (!member) {
        const std::vector<std::string> masked = split_text_lines(mask_c_like(r.text));
        if (auto local = find_local(masked, raw, r.line, r.column, word)) {
            if (!(local->line == r.line && local->column == b)) return CodeLocation{ r.file, local->line, local->column, word.size() };
        }
    }
    if (symbols.empty()) return std::nullopt;
    const Symbol* pick = &symbols.front();
    if (pick->file == r.file && pick->line == r.line && symbols.size() > 1) pick = &symbols[1];
    return CodeLocation{ pick->file, pick->line, pick->column, pick->name.size() };
}

namespace {

std::string cmake_word_at(const std::string& line, std::size_t column, std::size_t& b, std::size_t& e) {
    auto part = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == '/' || c == '+'; };
    if (column >= line.size() || !part(line[column])) return std::string();
    b = column;
    e = column;
    while (b > 0 && part(line[b - 1])) --b;
    while (e < line.size() && part(line[e])) ++e;
    return line.substr(b, e - b);
}

std::string cmake_command_of(const std::string& text, std::size_t line, std::size_t column) {
    const std::vector<std::string> raw = split_text_lines(text);
    int depth = 0;
    for (std::size_t l = line + 1; l-- > 0;) {
        const std::string& s = raw[l];
        std::size_t end = l == line ? std::min(column, s.size()) : s.size();
        for (std::size_t i = end; i-- > 0;) {
            if (s[i] == ')') ++depth;
            else if (s[i] == '(') {
                if (depth == 0) {
                    std::size_t e = i;
                    while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
                    std::size_t b = e;
                    while (b > 0 && ident_char(s[b - 1])) --b;
                    return lower(s.substr(b, e - b));
                }
                --depth;
            }
        }
        if (line - l > 60) break;
    }
    return std::string();
}

} // namespace

std::optional<HoverResult> CodeIntel::cmake_hover(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const std::string& line = raw[r.line];
    std::size_t b = 0;
    std::size_t e = 0;
    const std::string word = cmake_word_at(line, r.column, b, e);
    if (word.empty()) return std::nullopt;
    HoverResult h{ r.line, b, e - b, std::string() };
    std::size_t after = e;
    while (after < line.size() && (line[after] == ' ' || line[after] == '\t')) ++after;
    const bool is_command = after < line.size() && line[after] == '(' && line.find_first_not_of(" \t") == b;
    if (is_command) {
        for (const Symbol& s : find(lower(word), "cmake", r.file)) {
            if (s.kind != "command") continue;
            h.markdown = code_block("cmake", s.signature) + (s.doc.empty() ? std::string() : s.doc + "\n") + "---\n" + hover_link(location_label(m_root, CodeLocation{ s.file, s.line, s.column, 0 }), CodeLocation{ s.file, s.line, s.column, 0 }) + "\n";
            return h;
        }
        for (const DocEntry& d : cmake_command_docs()) {
            if (lower(d.name) != lower(word)) continue;
            const std::size_t dash = d.text.find(" \xE2\x80\x94 ");
            h.markdown = code_block("cmake", dash == std::string::npos ? d.name + "()" : d.text.substr(0, dash)) + (dash == std::string::npos ? d.text : d.text.substr(dash + 5)) + "\n";
            return h;
        }
        return std::nullopt;
    }
    std::string name = word;
    const std::size_t dollar = line.rfind("${", b);
    if (dollar != std::string::npos && dollar + 2 == b) name = word;
    std::vector<Symbol> defs;
    for (const Symbol& s : find(name, "cmake", r.file)) {
        if (s.kind == "variable" || s.kind == "target" || s.kind == "project") defs.push_back(s);
    }
    if (!defs.empty()) {
        const Symbol& first = defs.front();
        if (first.kind == "target" || first.kind == "project") {
            h.markdown = code_block("cmake", first.signature) + (first.doc.empty() ? std::string() : first.doc + "\n") + "---\n" + first.kind + " in " + hover_link(location_label(m_root, CodeLocation{ first.file, first.line, first.column, 0 }), CodeLocation{ first.file, first.line, first.column, 0 }) + "\n";
            return h;
        }
        std::size_t shown = 0;
        for (const Symbol& s : defs) {
            if (++shown > 3) break;
            h.markdown += code_block("cmake", s.signature);
        }
        if (defs.size() > 3) h.markdown += "+" + std::to_string(defs.size() - 3) + " more places set it\n";
        if (!first.doc.empty()) h.markdown += first.doc + "\n";
        if (!first.value.empty()) h.markdown += "**Value:** " + inline_code(first.value) + "\n";
        h.markdown += "---\n" + hover_link(location_label(m_root, CodeLocation{ first.file, first.line, first.column, 0 }), CodeLocation{ first.file, first.line, first.column, 0 }) + "\n";
        if (const DocEntry* d = find_doc(cmake_variable_docs(), name)) h.markdown = d->text + "\n" + h.markdown;
        return h;
    }
    if (const DocEntry* d = find_doc(cmake_variable_docs(), name)) {
        h.markdown = code_block("cmake", "${" + name + "}") + d->text + "\n";
        return h;
    }
    if (word.find('/') != std::string::npos || word.find('.') != std::string::npos || cmake_command_of(r.text, r.line, b) == "add_subdirectory" || cmake_command_of(r.text, r.line, b) == "include") {
        if (auto where = cmake_definition(r)) {
            h.markdown = hover_link(where->file, *where) + "\nCtrl+click to open it.";
            return h;
        }
    }
    return std::nullopt;
}

std::optional<CodeLocation> CodeIntel::cmake_definition(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    std::size_t b = 0;
    std::size_t e = 0;
    const std::string word = cmake_word_at(raw[r.line], r.column, b, e);
    if (word.empty()) return std::nullopt;
    const fs::path dir = fs::path(r.file).parent_path();
    std::error_code ec;
    const std::string command = cmake_command_of(r.text, r.line, b);
    if (command == "add_subdirectory") {
        const fs::path p = dir / word / "CMakeLists.txt";
        if (fs::is_regular_file(p, ec)) return CodeLocation{ p.string(), 0, 0, 0 };
    }
    if (command == "include") {
        for (const fs::path& p : { dir / word, dir / (word + ".cmake"), dir / "cmake" / (word + ".cmake"), dir / "CMake" / (word + ".cmake") }) {
            if (fs::is_regular_file(p, ec)) return CodeLocation{ p.string(), 0, 0, 0 };
        }
    }
    for (const Symbol& s : find(command.empty() ? lower(word) : word, "cmake", r.file)) {
        if (s.file == r.file && s.line == r.line && s.column == b) continue;
        return CodeLocation{ s.file, s.line, s.column, s.name.size() };
    }
    for (const Symbol& s : find(lower(word), "cmake", r.file)) {
        if (s.kind == "command") return CodeLocation{ s.file, s.line, s.column, s.name.size() };
    }
    if (word.find('/') != std::string::npos || word.find('.') != std::string::npos) {
        const fs::path p = dir / word;
        if (fs::is_regular_file(p, ec)) return CodeLocation{ p.string(), 0, 0, 0 };
        if (fs::is_regular_file(p / "CMakeLists.txt", ec)) return CodeLocation{ (p / "CMakeLists.txt").string(), 0, 0, 0 };
    }
    return std::nullopt;
}

std::vector<std::pair<CodeLocation, std::string>> CodeIntel::bark_rules(const std::string& selector) {
    std::vector<std::pair<CodeLocation, std::string>> out;
    for (const std::string& file : workspace_files("bark")) {
        const std::optional<std::string> text = text_of(file);
        if (!text) continue;
        const std::vector<std::string> raw = split_text_lines(*text);
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const std::string t = trim(raw[i]);
            if (t.empty() || t.find('{') == std::string::npos || t.rfind("//", 0) == 0) continue;
            const std::string head = t.substr(0, t.find('{'));
            std::size_t at = head.find(selector);
            bool hit = false;
            while (at != std::string::npos) {
                const std::size_t end = at + selector.size();
                const bool left_ok = selector[0] == '.' || selector[0] == '#' || at == 0 || !ident_char(head[at - 1]);
                if (left_ok && (end >= head.size() || !(ident_char(head[end]) || head[end] == '-'))) {
                    hit = true;
                    break;
                }
                at = head.find(selector, at + 1);
            }
            if (!hit) continue;
            std::string block = t;
            int depth = static_cast<int>(std::count(t.begin(), t.end(), '{')) - static_cast<int>(std::count(t.begin(), t.end(), '}'));
            std::size_t j = i + 1;
            std::size_t lines = 1;
            while (j < raw.size() && depth > 0) {
                depth += static_cast<int>(std::count(raw[j].begin(), raw[j].end(), '{')) - static_cast<int>(std::count(raw[j].begin(), raw[j].end(), '}'));
                if (lines < 10) block += "\n" + raw[j];
                else if (lines == 10) block += "\n    \xE2\x80\xA6";
                ++lines;
                ++j;
            }
            if (depth <= 0 && lines > 10) block += "\n}";
            out.emplace_back(CodeLocation{ file, i, raw[i].find_first_not_of(" \t"), 0 }, block);
            if (out.size() >= 6) return out;
        }
    }
    return out;
}

std::vector<std::pair<CodeLocation, std::string>> CodeIntel::grove_uses(const std::string& kind, const std::string& name) {
    std::vector<std::pair<CodeLocation, std::string>> out;
    for (const std::string& file : workspace_files("grove")) {
        const std::optional<std::string> text = text_of(file);
        if (!text) continue;
        const std::vector<std::string> raw = split_text_lines(*text);
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const std::string& l = raw[i];
            if (kind == "tag") {
                const std::size_t at = l.find("<" + name);
                if (at != std::string::npos && (at + name.size() + 1 >= l.size() || !ident_char(l[at + name.size() + 1]))) out.emplace_back(CodeLocation{ file, i, at + 1, name.size() }, trim(l));
            } else {
                const std::string attr = kind + "=\"";
                std::size_t at = l.find(attr);
                while (at != std::string::npos) {
                    if (at > 0 && (ident_char(l[at - 1]) || l[at - 1] == '-')) {
                        at = l.find(attr, at + 1);
                        continue;
                    }
                    const std::size_t vb = at + attr.size();
                    const std::size_t ve = l.find('"', vb);
                    if (ve == std::string::npos) break;
                    std::istringstream words(l.substr(vb, ve - vb));
                    std::string w;
                    std::size_t offset = vb;
                    const std::string value = l.substr(vb, ve - vb);
                    while (words >> w) {
                        if (w == name) {
                            const std::size_t pos = value.find(w, offset - vb);
                            out.emplace_back(CodeLocation{ file, i, vb + (pos == std::string::npos ? 0 : pos), name.size() }, trim(l));
                            break;
                        }
                    }
                    at = l.find(attr, ve);
                }
            }
            if (out.size() >= 20) return out;
        }
    }
    return out;
}

namespace {

struct BarkWord {
    std::string text;
    std::size_t begin = 0;
    std::size_t end = 0;
};

BarkWord bark_word_at(const std::string& line, std::size_t column) {
    auto part = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || static_cast<unsigned char>(c) >= 0x80; };
    if (column >= line.size()) return {};
    std::size_t b = column;
    std::size_t e = column;
    if (!part(line[column]) && !(line[column] == '$' || line[column] == '.' || line[column] == '#' || line[column] == ':')) return {};
    if (part(line[column])) {
        while (b > 0 && part(line[b - 1])) --b;
        while (e < line.size() && part(line[e])) ++e;
    } else {
        e = column + 1;
        while (e < line.size() && part(line[e])) ++e;
    }
    if (b > 0 && (line[b - 1] == '$' || line[b - 1] == '.' || line[b - 1] == '#' || line[b - 1] == ':')) --b;
    if (b > 0 && line[b] == ':' && line[b - 1] == ':') --b;
    return BarkWord{ line.substr(b, e - b), b, e };
}

bool selector_line(const std::vector<std::string>& raw, std::size_t line) {
    const std::string& l = raw[line];
    const std::size_t brace = l.find('{');
    const std::size_t colon_semi = l.find(';');
    if (brace != std::string::npos && (colon_semi == std::string::npos || colon_semi > brace)) return true;
    const std::string t = trim(l);
    return !t.empty() && t.back() == ',';
}

} // namespace

std::optional<HoverResult> CodeIntel::bark_hover(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const std::string& line = raw[r.line];
    for (const grove::CodeColor& c : grove::find_code_colors(line)) {
        if (r.column < c.begin || r.column >= c.end) continue;
        return HoverResult{ r.line, c.begin, c.end - c.begin, color_preview(line.substr(c.begin, c.end - c.begin)) + "\nClick the swatch beside it to pick a new color." };
    }
    const BarkWord w = bark_word_at(line, r.column);
    if (w.text.empty()) return std::nullopt;
    HoverResult h{ r.line, w.begin, w.end - w.begin, std::string() };
    if (w.text[0] == '$') {
        for (const Symbol& s : find(w.text, "bark", r.file)) {
            h.markdown = code_block("bark", s.signature) + (s.doc.empty() ? std::string() : s.doc + "\n");
            const std::string preview = color_preview(s.value);
            if (!preview.empty()) h.markdown += "**Value:** " + preview + "\n";
            h.markdown += "---\n" + hover_link(location_label(m_root, CodeLocation{ s.file, s.line, s.column, 0 }), CodeLocation{ s.file, s.line, s.column, 0 }) + "\n";
            return h;
        }
        return std::nullopt;
    }
    const bool in_selector = selector_line(raw, r.line);
    if (w.text[0] == ':' && in_selector) {
        if (const DocEntry* d = find_doc(bark_pseudo_docs(), w.text)) {
            h.markdown = code_block("bark", w.text) + d->text + "\n";
            return h;
        }
        return std::nullopt;
    }
    if ((w.text[0] == '.' || w.text[0] == '#') && in_selector && w.text.size() > 1) {
        const std::string name = w.text.substr(1);
        const auto uses = grove_uses(w.text[0] == '.' ? "class" : "id", name);
        h.markdown = code_block("bark", w.text);
        if (uses.empty()) h.markdown += "No element in the workspace's .grove files has " + std::string(w.text[0] == '.' ? "this class." : "this id.") + "\n";
        else {
            h.markdown += "**Used by " + std::to_string(uses.size()) + (uses.size() == 1 ? " element" : " elements") + (uses.size() >= 20 ? " or more" : "") + ":**\n";
            for (std::size_t i = 0; i < uses.size() && i < 5; ++i) h.markdown += hover_link(location_label(m_root, uses[i].first), uses[i].first) + "  " + inline_code(shorten(uses[i].second, 70)) + "\n";
        }
        return h;
    }
    std::size_t after = w.end;
    while (after < line.size() && (line[after] == ' ' || line[after] == '\t')) ++after;
    if (after < line.size() && line[after] == '(') {
        if (const DocEntry* d = find_doc(bark_function_docs(), w.text)) {
            h.markdown = code_block("bark", d->on) + d->text + "\n";
            return h;
        }
    }
    if (!in_selector && after < line.size() && line[after] == ':') {
        if (const DocEntry* d = find_doc(bark_property_docs(), w.text)) {
            h.markdown = code_block("bark", w.text + ":") + (d->on.empty() ? std::string() : "**On:** " + d->on + "\n") + d->text + "\n";
            return h;
        }
        if (w.text.rfind("code-", 0) != 0) {
            h.markdown = inline_code(w.text) + " isn't a Bark property Grove knows.";
            return h;
        }
    }
    if (in_selector) {
        if (const DocEntry* d = find_doc(grove_tag_docs(), w.text)) {
            h.markdown = code_block("grove", "<" + w.text + ">") + d->text + "\n";
            return h;
        }
    }
    if (w.begin > 0 && std::isdigit(static_cast<unsigned char>(line[w.begin - 1]))) {
        std::size_t ub = w.begin;
        if (const DocEntry* d = find_doc(bark_unit_docs(), w.text)) {
            h.column = ub;
            h.markdown = "**" + w.text + ":** 1 unit is 1% of " + d->text + "\n";
            return h;
        }
    }
    return std::nullopt;
}

std::optional<CodeLocation> CodeIntel::bark_definition(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const BarkWord w = bark_word_at(raw[r.line], r.column);
    if (w.text.empty()) return std::nullopt;
    if (w.text[0] == '$') {
        for (const Symbol& s : find(w.text, "bark", r.file)) {
            if (s.file == r.file && s.line == r.line) continue;
            return CodeLocation{ s.file, s.line, s.column, s.name.size() };
        }
        return std::nullopt;
    }
    if ((w.text[0] == '.' || w.text[0] == '#') && w.text.size() > 1) {
        const auto uses = grove_uses(w.text[0] == '.' ? "class" : "id", w.text.substr(1));
        if (!uses.empty()) return uses.front().first;
    }
    return std::nullopt;
}

namespace {

struct MarkupSpot {
    std::string kind;
    std::string attribute;
    std::string text;
    std::size_t begin = 0;
    std::size_t end = 0;
};

MarkupSpot markup_spot(const std::string& line, std::size_t column) {
    MarkupSpot spot;
    if (column >= line.size()) return spot;
    const std::size_t lt = line.rfind('<', column);
    const std::size_t gt = line.rfind('>', column == 0 ? 0 : column - 1);
    const bool in_tag = lt != std::string::npos && (gt == std::string::npos || gt < lt || column == 0);
    if (line[column] == '&' || (line.rfind('&', column) != std::string::npos && line.find(';', line.rfind('&', column)) != std::string::npos && line.find(';', line.rfind('&', column)) >= column)) {
        const std::size_t amp = line.rfind('&', column);
        const std::size_t semi = line.find(';', amp);
        if (semi != std::string::npos && semi - amp < 12 && std::all_of(line.begin() + static_cast<std::ptrdiff_t>(amp) + 1, line.begin() + static_cast<std::ptrdiff_t>(semi), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '#'; })) {
            return MarkupSpot{ "entity", std::string(), line.substr(amp, semi + 1 - amp), amp, semi + 1 };
        }
    }
    if (!in_tag) return spot;
    std::size_t q = lt + 1;
    if (q < line.size() && line[q] == '/') ++q;
    std::size_t te = q;
    while (te < line.size() && (ident_char(line[te]) || line[te] == '-')) ++te;
    if (column >= q && column < te) return MarkupSpot{ "tag", std::string(), line.substr(q, te - q), q, te };
    std::size_t open_quote = std::string::npos;
    bool inside = false;
    for (std::size_t i = te; i < column && i < line.size(); ++i) {
        if (line[i] == '"') {
            inside = !inside;
            if (inside) open_quote = i;
        }
    }
    if (inside) {
        const std::size_t close = line.find('"', open_quote + 1);
        std::size_t ab = open_quote;
        while (ab > 0 && (line[ab - 1] == '=' || line[ab - 1] == ' ')) --ab;
        std::size_t ae = ab;
        while (ab > 0 && (ident_char(line[ab - 1]) || line[ab - 1] == '-')) --ab;
        const std::string attribute = line.substr(ab, ae - ab);
        const std::size_t value_end = close == std::string::npos ? line.size() : close;
        if (attribute == "class") {
            std::size_t b = column;
            std::size_t e = column;
            while (b > open_quote + 1 && line[b - 1] != ' ') --b;
            while (e < value_end && line[e] != ' ') ++e;
            if (b >= e) return spot;
            return MarkupSpot{ "value", attribute, line.substr(b, e - b), b, e };
        }
        return MarkupSpot{ "value", attribute, line.substr(open_quote + 1, value_end - open_quote - 1), open_quote + 1, value_end };
    }
    if (ident_char(line[column]) || line[column] == '-') {
        std::size_t b = column;
        std::size_t e = column;
        while (b > 0 && (ident_char(line[b - 1]) || line[b - 1] == '-')) --b;
        while (e < line.size() && (ident_char(line[e]) || line[e] == '-')) ++e;
        return MarkupSpot{ "attribute", std::string(), line.substr(b, e - b), b, e };
    }
    return spot;
}

std::string entity_text(const std::string& entity) {
    static const std::map<std::string, std::string> names = {
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }, { "nbsp", "\xC2\xA0" }, { "copy", "\xC2\xA9" }, { "reg", "\xC2\xAE" },
        { "trade", "\xE2\x84\xA2" }, { "mdash", "\xE2\x80\x94" }, { "ndash", "\xE2\x80\x93" }, { "hellip", "\xE2\x80\xA6" }, { "times", "\xC3\x97" }, { "middot", "\xC2\xB7" },
        { "larr", "\xE2\x86\x90" }, { "rarr", "\xE2\x86\x92" }, { "uarr", "\xE2\x86\x91" }, { "darr", "\xE2\x86\x93" }, { "deg", "\xC2\xB0" },
    };
    const std::string body = entity.substr(1, entity.size() - 2);
    if (!body.empty() && body[0] == '#') {
        const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
        const unsigned long cp = std::strtoul(body.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
        std::string out;
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        return out;
    }
    const auto it = names.find(body);
    return it == names.end() ? std::string() : it->second;
}

} // namespace

std::optional<HoverResult> CodeIntel::grove_hover(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const std::string& line = raw[r.line];
    for (const grove::CodeColor& c : grove::find_code_colors(line)) {
        if (r.column < c.begin || r.column >= c.end) continue;
        return HoverResult{ r.line, c.begin, c.end - c.begin, color_preview(line.substr(c.begin, c.end - c.begin)) };
    }
    const MarkupSpot spot = markup_spot(line, r.column);
    if (spot.kind.empty()) return std::nullopt;
    HoverResult h{ r.line, spot.begin, spot.end - spot.begin, std::string() };
    if (spot.kind == "entity") {
        const std::string shown = entity_text(spot.text);
        if (shown.empty()) return std::nullopt;
        h.markdown = inline_code(spot.text) + " is " + inline_code(shown);
        return h;
    }
    if (spot.kind == "tag") {
        if (const DocEntry* d = find_doc(grove_tag_docs(), spot.text)) {
            h.markdown = code_block("grove", "<" + spot.text + ">") + d->text + "\n";
            const auto rules = bark_rules(spot.text);
            if (!rules.empty()) h.markdown += "---\n**Styled by:**\n" + code_block("bark", rules.front().second) + hover_link(location_label(m_root, rules.front().first), rules.front().first) + "\n";
            return h;
        }
        if (spot.text.find('-') != std::string::npos) {
            h.markdown = inline_code(spot.text) + " is a custom element.";
            return h;
        }
        return std::nullopt;
    }
    if (spot.kind == "attribute") {
        if (const DocEntry* d = find_doc(grove_attribute_docs(), spot.text)) {
            h.markdown = code_block("grove", spot.text + "=\"\"") + (d->on.empty() ? std::string() : "**On:** " + d->on + "\n") + d->text + "\n";
            return h;
        }
        if (spot.text.rfind("on", 0) == 0) return std::nullopt;
        return std::nullopt;
    }
    if (spot.attribute == "class" || spot.attribute == "id") {
        const std::string selector = (spot.attribute == "class" ? "." : "#") + spot.text;
        const auto rules = bark_rules(selector);
        if (rules.empty()) {
            h.markdown = "No Bark rule in the workspace uses " + inline_code(selector) + ".";
            return h;
        }
        for (std::size_t i = 0; i < rules.size() && i < 3; ++i) h.markdown += code_block("bark", rules[i].second) + hover_link(location_label(m_root, rules[i].first), rules[i].first) + "\n";
        if (rules.size() > 3) h.markdown += "+" + std::to_string(rules.size() - 3) + " more rules\n";
        return h;
    }
    if (spot.attribute == "to") {
        if (auto where = grove_definition(r)) {
            const std::optional<std::string> text = text_of(where->file);
            const std::vector<std::string> lines = text ? split_text_lines(*text) : std::vector<std::string>{};
            h.markdown = code_block("grove", where->line < lines.size() ? trim(lines[where->line]) : spot.text) + hover_link(location_label(m_root, *where), *where) + "\n";
            return h;
        }
        h.markdown = "No element has " + inline_code("id=\"" + spot.text + "\"") + ".";
        return h;
    }
    if (spot.attribute == "src" || spot.attribute == "href" || spot.attribute == "icon") {
        if (auto where = grove_definition(r)) {
            h.markdown = hover_link(where->file, *where) + "\nCtrl+click to open it.";
            return h;
        }
        h.markdown = "Can't find " + inline_code(spot.text) + ".";
        return h;
    }
    if (spot.attribute == "style") {
        const std::size_t cut = r.column - spot.begin;
        std::size_t b = spot.text.rfind(';', cut);
        b = b == std::string::npos ? 0 : b + 1;
        std::size_t colon = spot.text.find(':', b);
        if (colon != std::string::npos && cut < colon) {
            const std::string name = trim(spot.text.substr(b, colon - b));
            if (const DocEntry* d = find_doc(bark_property_docs(), name)) {
                h.column = spot.begin + spot.text.find(name, b);
                h.length = name.size();
                h.markdown = code_block("bark", name + ":") + (d->on.empty() ? std::string() : "**On:** " + d->on + "\n") + d->text + "\n";
                return h;
            }
        }
    }
    return std::nullopt;
}

std::optional<CodeLocation> CodeIntel::grove_definition(const IntelRequest& r) {
    const std::vector<std::string> raw = split_text_lines(r.text);
    if (r.line >= raw.size()) return std::nullopt;
    const MarkupSpot spot = markup_spot(raw[r.line], r.column);
    std::error_code ec;
    if (spot.attribute == "src" || spot.attribute == "href" || spot.attribute == "icon") {
        std::string target = spot.text;
        if (spot.attribute == "icon") {
            const std::size_t at = r.column - spot.begin;
            std::size_t b = target.rfind(' ', at);
            b = b == std::string::npos ? 0 : b + 1;
            std::size_t e = target.find(' ', at);
            target = target.substr(b, e == std::string::npos ? std::string::npos : e - b);
        }
        const fs::path p = fs::path(r.file).parent_path() / target;
        if (fs::exists(p, ec)) return CodeLocation{ fs::weakly_canonical(p, ec).string(), 0, 0, 0 };
        return std::nullopt;
    }
    if (spot.attribute == "class" || spot.attribute == "id") {
        const auto rules = bark_rules((spot.attribute == "class" ? "." : "#") + spot.text);
        if (!rules.empty()) return rules.front().first;
        return std::nullopt;
    }
    if (spot.attribute == "to") {
        const std::string needle = "id=\"" + spot.text + "\"";
        for (std::size_t i = 0; i < raw.size(); ++i) {
            const std::size_t at = raw[i].find(needle);
            if (at != std::string::npos) return CodeLocation{ r.file, i, at + 4, spot.text.size() };
        }
        const auto uses = grove_uses("id", spot.text);
        if (!uses.empty()) return uses.front().first;
        return std::nullopt;
    }
    if (spot.kind == "tag") {
        const auto rules = bark_rules(spot.text);
        if (!rules.empty()) return rules.front().first;
    }
    return std::nullopt;
}

} // namespace canopy
