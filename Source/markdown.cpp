#include <canopy/markdown.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace canopy {

namespace {

struct Style {
    bool bold = false;
    bool italic = false;
    bool strike = false;
    std::string href;
};

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool is_punct(char c) {
    return std::ispunct(static_cast<unsigned char>(c)) != 0;
}

bool is_alnum(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || static_cast<unsigned char>(c) >= 0x80;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && is_space(s[a])) ++a;
    while (b > a && is_space(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string expand_tabs(const std::string& line) {
    if (line.find('\t') == std::string::npos) return line;
    std::string out;
    std::size_t col = 0;
    for (char c : line) {
        if (c == '\t') {
            const std::size_t n = 4 - col % 4;
            out.append(n, ' ');
            col += n;
        } else {
            out += c;
            ++col;
        }
    }
    return out;
}

std::size_t indent_of(const std::string& line) {
    std::size_t n = 0;
    while (n < line.size() && line[n] == ' ') ++n;
    return n;
}

bool blank(const std::string& line) {
    return line.find_first_not_of(" \t\r") == std::string::npos;
}

std::string strip_indent(const std::string& line, std::size_t n) {
    const std::size_t have = indent_of(line);
    return line.substr(std::min(have, n));
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
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
}

bool read_entity(const std::string& s, std::size_t at, std::string& out, std::size_t& used) {
    const std::size_t semi = s.find(';', at);
    if (semi == std::string::npos || semi - at > 10) return false;
    const std::string name = s.substr(at + 1, semi - at - 1);
    if (name.empty()) return false;
    used = semi - at + 1;
    if (name[0] == '#') {
        std::uint32_t cp = 0;
        const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
        const std::string digits = name.substr(hex ? 2 : 1);
        if (digits.empty()) return false;
        for (char c : digits) {
            const int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : hex && std::isxdigit(static_cast<unsigned char>(c)) ? std::tolower(static_cast<unsigned char>(c)) - 'a' + 10 : -1;
            if (v < 0) return false;
            cp = cp * (hex ? 16u : 10u) + static_cast<std::uint32_t>(v);
            if (cp > 0x10FFFF) return false;
        }
        append_utf8(out, cp);
        return true;
    }
    static const std::pair<const char*, const char*> named[] = {
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" }, { "quot", "\"" }, { "apos", "'" }, { "nbsp", "\xC2\xA0" }, { "copy", "\xC2\xA9" },
        { "reg", "\xC2\xAE" }, { "trade", "\xE2\x84\xA2" }, { "mdash", "\xE2\x80\x94" }, { "ndash", "\xE2\x80\x93" }, { "hellip", "\xE2\x80\xA6" },
        { "rarr", "\xE2\x86\x92" }, { "larr", "\xE2\x86\x90" }, { "times", "\xC3\x97" }, { "middot", "\xC2\xB7" }, { "bull", "\xE2\x80\xA2" },
    };
    for (const auto& [n, v] : named) {
        if (name == n) {
            out += v;
            return true;
        }
    }
    return false;
}

class InlineParser {
public:
    explicit InlineParser(std::vector<MdSpan>& out) : m_out(out) {}

    void run(const std::string& s, const Style& style) {
        std::string buf;
        auto flush = [&]() {
            if (buf.empty()) return;
            emit(buf, style);
            buf.clear();
        };
        std::size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '\\' && i + 1 < s.size()) {
                if (s[i + 1] == '\n') {
                    flush();
                    emit_break(style);
                    i += 2;
                    continue;
                }
                if (is_punct(s[i + 1])) {
                    buf += s[i + 1];
                    i += 2;
                    continue;
                }
            }
            if (c == '\n') {
                std::size_t spaces = 0;
                while (!buf.empty() && buf.back() == ' ') {
                    buf.pop_back();
                    ++spaces;
                }
                if (spaces >= 2) {
                    flush();
                    emit_break(style);
                } else {
                    buf += ' ';
                }
                ++i;
                while (i < s.size() && s[i] == ' ') ++i;
                continue;
            }
            if (c == '`') {
                std::size_t run = 0;
                while (i + run < s.size() && s[i + run] == '`') ++run;
                const std::size_t close = find_backticks(s, i + run, run);
                if (close == std::string::npos) {
                    buf.append(run, '`');
                    i += run;
                    continue;
                }
                std::string inner = s.substr(i + run, close - i - run);
                for (char& ch : inner) {
                    if (ch == '\n') ch = ' ';
                }
                if (inner.size() >= 2 && inner.front() == ' ' && inner.back() == ' ' && inner.find_first_not_of(' ') != std::string::npos) inner = inner.substr(1, inner.size() - 2);
                flush();
                MdSpan span = base(style);
                span.code = true;
                span.text = inner;
                m_out.push_back(span);
                i = close + run;
                continue;
            }
            if ((c == '!' && i + 1 < s.size() && s[i + 1] == '[') || c == '[') {
                const bool image = c == '!';
                const std::size_t open = image ? i + 1 : i;
                std::string label;
                std::string dest;
                std::size_t end = 0;
                if (read_link(s, open, label, dest, end)) {
                    flush();
                    if (image) {
                        MdSpan span = base(style);
                        span.image = true;
                        span.text = label;
                        span.href = dest;
                        m_out.push_back(span);
                    } else {
                        Style inner = style;
                        inner.href = dest;
                        const std::size_t before = m_out.size();
                        run(label, inner);
                        if (m_out.size() == before) emit(dest, inner);
                    }
                    i = end;
                    continue;
                }
            }
            if (c == '<') {
                const std::size_t close = s.find('>', i + 1);
                if (close != std::string::npos) {
                    const std::string inside = s.substr(i + 1, close - i - 1);
                    const bool url = inside.find("://") != std::string::npos && inside.find(' ') == std::string::npos;
                    const bool mail = !url && inside.find('@') != std::string::npos && inside.find(' ') == std::string::npos && inside.find(':') == std::string::npos;
                    if (url || mail) {
                        flush();
                        Style inner = style;
                        inner.href = mail ? "mailto:" + inside : inside;
                        emit(inside, inner);
                        i = close + 1;
                        continue;
                    }
                    if (looks_like_tag(inside)) {
                        if (lower(inside).rfind("br", 0) == 0) {
                            flush();
                            emit_break(style);
                        }
                        i = close + 1;
                        continue;
                    }
                }
            }
            if (style.href.empty() && (s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0) && (i == 0 || !is_alnum(s[i - 1]))) {
                std::size_t end = i;
                while (end < s.size() && !is_space(s[end]) && s[end] != '<') ++end;
                while (end > i && (s[end - 1] == '.' || s[end - 1] == ',' || s[end - 1] == ';' || s[end - 1] == ':' || s[end - 1] == '!' || s[end - 1] == '?' || s[end - 1] == ')' || s[end - 1] == '\'' || s[end - 1] == '"')) --end;
                flush();
                Style inner = style;
                inner.href = s.substr(i, end - i);
                emit(inner.href, inner);
                i = end;
                continue;
            }
            if (c == '~' && i + 1 < s.size() && s[i + 1] == '~') {
                const std::size_t close = s.find("~~", i + 2);
                if (close != std::string::npos && close > i + 2 && !is_space(s[i + 2]) && !is_space(s[close - 1])) {
                    flush();
                    Style inner = style;
                    inner.strike = true;
                    run(s.substr(i + 2, close - i - 2), inner);
                    i = close + 2;
                    continue;
                }
            }
            if (c == '*' || c == '_') {
                std::size_t run_len = 0;
                while (i + run_len < s.size() && s[i + run_len] == c) ++run_len;
                const char after = i + run_len < s.size() ? s[i + run_len] : ' ';
                const char before = i > 0 ? s[i - 1] : ' ';
                const bool can_open = !is_space(after) && (c == '*' || !is_alnum(before));
                bool done = false;
                if (can_open) {
                    for (std::size_t take = std::min<std::size_t>(run_len, 3); take >= 1 && !done; --take) {
                        const std::size_t close = find_closer(s, i + run_len, c, take);
                        if (close == std::string::npos) continue;
                        buf.append(run_len - take, c);
                        flush();
                        Style inner = style;
                        if (take >= 2) inner.bold = true;
                        if (take != 2) inner.italic = true;
                        run(s.substr(i + run_len, close - i - run_len), inner);
                        i = close + take;
                        done = true;
                    }
                }
                if (!done) {
                    buf.append(run_len, c);
                    i += run_len;
                }
                continue;
            }
            if (c == '&') {
                std::string decoded;
                std::size_t used = 0;
                if (read_entity(s, i, decoded, used)) {
                    buf += decoded;
                    i += used;
                    continue;
                }
            }
            buf += c;
            ++i;
        }
        flush();
    }

private:
    std::vector<MdSpan>& m_out;

    static MdSpan base(const Style& style) {
        MdSpan span;
        span.bold = style.bold;
        span.italic = style.italic;
        span.strike = style.strike;
        span.href = style.href;
        return span;
    }

    void emit(const std::string& text, const Style& style) {
        if (text.empty()) return;
        if (!m_out.empty()) {
            MdSpan& last = m_out.back();
            if (!last.code && !last.image && !last.line_break && last.bold == style.bold && last.italic == style.italic && last.strike == style.strike && last.href == style.href) {
                last.text += text;
                return;
            }
        }
        MdSpan span = base(style);
        span.text = text;
        m_out.push_back(span);
    }

    void emit_break(const Style& style) {
        MdSpan span = base(style);
        span.line_break = true;
        m_out.push_back(span);
    }

    static bool looks_like_tag(const std::string& inside) {
        if (inside.empty()) return false;
        if (inside.rfind("!--", 0) == 0) return true;
        const std::size_t start = inside[0] == '/' ? 1 : 0;
        if (start >= inside.size() || !std::isalpha(static_cast<unsigned char>(inside[start]))) return false;
        for (std::size_t k = start; k < inside.size() && !is_space(inside[k]) && inside[k] != '/'; ++k) {
            if (!std::isalnum(static_cast<unsigned char>(inside[k])) && inside[k] != '-') return false;
        }
        return true;
    }

    static std::size_t find_backticks(const std::string& s, std::size_t from, std::size_t run) {
        std::size_t i = from;
        while (i < s.size()) {
            if (s[i] != '`') {
                ++i;
                continue;
            }
            std::size_t n = 0;
            while (i + n < s.size() && s[i + n] == '`') ++n;
            if (n == run) return i;
            i += n;
        }
        return std::string::npos;
    }

    static std::size_t skip_code(const std::string& s, std::size_t i) {
        std::size_t run = 0;
        while (i + run < s.size() && s[i + run] == '`') ++run;
        const std::size_t close = find_backticks(s, i + run, run);
        return close == std::string::npos ? i + run : close + run;
    }

    static std::size_t find_closer(const std::string& s, std::size_t from, char c, std::size_t take) {
        std::size_t i = from;
        int depth = 0;
        while (i < s.size()) {
            const char ch = s[i];
            if (ch == '\\') {
                i += 2;
                continue;
            }
            if (ch == '`') {
                i = skip_code(s, i);
                continue;
            }
            if (ch == '[') ++depth;
            if (ch == ']' && depth > 0) --depth;
            if (ch != c) {
                ++i;
                continue;
            }
            std::size_t n = 0;
            while (i + n < s.size() && s[i + n] == c) ++n;
            const char before = i > 0 ? s[i - 1] : ' ';
            const char after = i + n < s.size() ? s[i + n] : ' ';
            const bool can_close = !is_space(before) && (c == '*' || !is_alnum(after)) && i > from;
            const bool fits = take == 1 ? (n == 1 || n == 3) : take == 2 ? (n == 2 || n == 3) : n >= 3;
            if (can_close && depth == 0 && fits) return i + n - take;
            i += n;
        }
        return std::string::npos;
    }

    static bool read_link(const std::string& s, std::size_t open, std::string& label, std::string& dest, std::size_t& end) {
        if (open >= s.size() || s[open] != '[') return false;
        int depth = 0;
        std::size_t close = std::string::npos;
        for (std::size_t k = open; k < s.size(); ++k) {
            if (s[k] == '\\') {
                ++k;
                continue;
            }
            if (s[k] == '`') {
                k = skip_code(s, k) - 1;
                continue;
            }
            if (s[k] == '[') ++depth;
            else if (s[k] == ']' && --depth == 0) {
                close = k;
                break;
            }
        }
        if (close == std::string::npos || close + 1 >= s.size() || s[close + 1] != '(') return false;
        std::size_t k = close + 2;
        while (k < s.size() && is_space(s[k])) ++k;
        std::string target;
        if (k < s.size() && s[k] == '<') {
            const std::size_t gt = s.find('>', k + 1);
            if (gt == std::string::npos) return false;
            target = s.substr(k + 1, gt - k - 1);
            k = gt + 1;
        } else {
            int parens = 0;
            const std::size_t start = k;
            while (k < s.size() && !is_space(s[k])) {
                if (s[k] == '\\' && k + 1 < s.size()) {
                    k += 2;
                    continue;
                }
                if (s[k] == '(') ++parens;
                else if (s[k] == ')') {
                    if (parens == 0) break;
                    --parens;
                }
                ++k;
            }
            target = s.substr(start, k - start);
        }
        while (k < s.size() && is_space(s[k])) ++k;
        if (k < s.size() && (s[k] == '"' || s[k] == '\'' || s[k] == '(')) {
            const char quote = s[k] == '(' ? ')' : s[k];
            const std::size_t q = s.find(quote, k + 1);
            if (q == std::string::npos) return false;
            k = q + 1;
            while (k < s.size() && is_space(s[k])) ++k;
        }
        if (k >= s.size() || s[k] != ')') return false;
        label = s.substr(open + 1, close - open - 1);
        dest = target;
        end = k + 1;
        return true;
    }
};

bool fence_start(const std::string& line, char& ch, std::size_t& len, std::string& info, std::size_t& indent) {
    indent = indent_of(line);
    if (indent > 3 || indent >= line.size()) return false;
    ch = line[indent];
    if (ch != '`' && ch != '~') return false;
    len = 0;
    while (indent + len < line.size() && line[indent + len] == ch) ++len;
    if (len < 3) return false;
    info = trim(line.substr(indent + len));
    if (ch == '`' && info.find('`') != std::string::npos) return false;
    return true;
}

bool fence_end(const std::string& line, char ch, std::size_t len) {
    const std::size_t ind = indent_of(line);
    if (ind > 3) return false;
    std::size_t n = 0;
    while (ind + n < line.size() && line[ind + n] == ch) ++n;
    return n >= len && trim(line.substr(ind + n)).empty();
}

int heading_level(const std::string& line, std::string& text) {
    const std::size_t ind = indent_of(line);
    if (ind > 3) return 0;
    std::size_t n = 0;
    while (ind + n < line.size() && line[ind + n] == '#') ++n;
    if (n == 0 || n > 6) return 0;
    if (ind + n < line.size() && line[ind + n] != ' ') return 0;
    std::string rest = trim(line.substr(ind + n));
    std::size_t end = rest.size();
    while (end > 0 && rest[end - 1] == '#') --end;
    if (end < rest.size() && (end == 0 || rest[end - 1] == ' ')) rest = trim(rest.substr(0, end));
    text = rest;
    return static_cast<int>(n);
}

bool is_rule(const std::string& line) {
    if (indent_of(line) > 3) return false;
    char ch = 0;
    std::size_t count = 0;
    for (char c : line) {
        if (c == ' ') continue;
        if (c != '-' && c != '*' && c != '_') return false;
        if (ch && c != ch) return false;
        ch = c;
        ++count;
    }
    return count >= 3;
}

int setext_level(const std::string& line) {
    if (indent_of(line) > 3) return 0;
    const std::string t = trim(line);
    if (t.empty()) return 0;
    if (t.find_first_not_of('=') == std::string::npos) return 1;
    if (t.find_first_not_of('-') == std::string::npos) return 2;
    return 0;
}

struct ListMarker {
    bool ok = false;
    bool ordered = false;
    char bullet = 0;
    long long number = 1;
    std::size_t content = 0;
    bool empty = false;
};

ListMarker list_marker(const std::string& line) {
    ListMarker m;
    const std::size_t ind = indent_of(line);
    if (ind > 3 || ind >= line.size()) return m;
    std::size_t k = ind;
    const char c = line[k];
    if (c == '-' || c == '*' || c == '+') {
        m.bullet = c;
        ++k;
    } else if (std::isdigit(static_cast<unsigned char>(c))) {
        long long n = 0;
        std::size_t digits = 0;
        while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k])) && digits < 9) {
            n = n * 10 + (line[k] - '0');
            ++k;
            ++digits;
        }
        if (k >= line.size() || (line[k] != '.' && line[k] != ')')) return m;
        m.ordered = true;
        m.bullet = line[k];
        m.number = n;
        ++k;
    } else {
        return m;
    }
    if (k < line.size() && line[k] != ' ') return m;
    std::size_t spaces = 0;
    while (k + spaces < line.size() && line[k + spaces] == ' ') ++spaces;
    m.empty = k + spaces >= line.size();
    if (m.empty || spaces > 4) spaces = 1;
    if (spaces == 0) spaces = 1;
    m.content = k + spaces;
    m.ok = true;
    return m;
}

bool quote_line(const std::string& line) {
    const std::size_t ind = indent_of(line);
    return ind <= 3 && ind < line.size() && line[ind] == '>';
}

bool html_start(const std::string& line) {
    const std::size_t ind = indent_of(line);
    if (ind > 3 || ind + 1 >= line.size() || line[ind] != '<') return false;
    const char n = line[ind + 1];
    return std::isalpha(static_cast<unsigned char>(n)) || n == '/' || n == '!';
}

std::vector<std::string> split_row(const std::string& line) {
    std::string t = trim(line);
    if (!t.empty() && t.front() == '|') t.erase(0, 1);
    if (!t.empty() && t.back() == '|' && (t.size() < 2 || t[t.size() - 2] != '\\')) t.pop_back();
    std::vector<std::string> cells;
    std::string cell;
    bool code = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '\\' && i + 1 < t.size() && t[i + 1] == '|') {
            cell += '|';
            ++i;
            continue;
        }
        if (t[i] == '`') code = !code;
        if (t[i] == '|' && !code) {
            cells.push_back(trim(cell));
            cell.clear();
            continue;
        }
        cell += t[i];
    }
    cells.push_back(trim(cell));
    return cells;
}

bool delimiter_row(const std::string& line, std::vector<MdAlign>& align) {
    if (line.find('-') == std::string::npos) return false;
    align.clear();
    for (const std::string& c : split_row(line)) {
        if (c.empty()) return false;
        const bool left = c.front() == ':';
        const bool right = c.back() == ':';
        const std::string dashes = c.substr(left ? 1 : 0, c.size() - (left ? 1 : 0) - (right ? 1 : 0));
        if (dashes.empty() || dashes.find_first_not_of('-') != std::string::npos) return false;
        align.push_back(left && right ? MdAlign::Center : right ? MdAlign::Right : left ? MdAlign::Left : MdAlign::None);
    }
    return !align.empty();
}

bool interrupts_paragraph(const std::string& line) {
    std::string tmp;
    char ch = 0;
    std::size_t len = 0;
    std::size_t ind = 0;
    if (heading_level(line, tmp) > 0) return true;
    if (fence_start(line, ch, len, tmp, ind)) return true;
    if (quote_line(line)) return true;
    if (is_rule(line)) return true;
    if (html_start(line)) return true;
    const ListMarker m = list_marker(line);
    if (m.ok && !m.empty && (!m.ordered || m.number == 1)) return true;
    return false;
}

std::string strip_html(const std::string& html) {
    std::string out;
    bool in_tag = false;
    for (std::size_t i = 0; i < html.size(); ++i) {
        const char c = html[i];
        if (c == '<') {
            in_tag = true;
            const std::string rest = lower(html.substr(i, 5));
            if (rest.rfind("<br", 0) == 0 || rest.rfind("</p", 0) == 0 || rest.rfind("<p", 0) == 0) out += '\n';
            continue;
        }
        if (c == '>' && in_tag) {
            in_tag = false;
            continue;
        }
        if (!in_tag) out += c;
    }
    return out;
}

void parse_blocks(const std::vector<std::string>& lines, std::size_t base_line, std::vector<MdBlock>& out);

void finish_paragraph(std::vector<std::string>& para, std::size_t line, std::vector<MdBlock>& out) {
    if (para.empty()) return;
    std::string text;
    for (std::size_t k = 0; k < para.size(); ++k) {
        if (k) text += '\n';
        text += para[k].substr(std::min(indent_of(para[k]), para[k].size()));
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.pop_back();
    MdBlock b;
    b.kind = MdBlock::Kind::Paragraph;
    b.line = line;
    b.spans = parse_markdown_inline(text);
    out.push_back(std::move(b));
    para.clear();
}

void parse_blocks(const std::vector<std::string>& lines, std::size_t base_line, std::vector<MdBlock>& out) {
    std::vector<std::string> para;
    std::size_t para_line = 0;
    std::size_t i = 0;
    const std::size_t n = lines.size();
    while (i < n) {
        const std::string& line = lines[i];
        if (blank(line)) {
            finish_paragraph(para, para_line, out);
            ++i;
            continue;
        }
        if (!para.empty()) {
            const int level = setext_level(line);
            if (level > 0) {
                std::string text;
                for (std::size_t k = 0; k < para.size(); ++k) {
                    if (k) text += '\n';
                    text += trim(para[k]);
                }
                MdBlock b;
                b.kind = MdBlock::Kind::Heading;
                b.level = level;
                b.line = para_line;
                b.spans = parse_markdown_inline(text);
                out.push_back(std::move(b));
                para.clear();
                ++i;
                continue;
            }
            if (!interrupts_paragraph(line)) {
                para.push_back(line);
                ++i;
                continue;
            }
            finish_paragraph(para, para_line, out);
        }

        char fch = 0;
        std::size_t flen = 0;
        std::size_t find = 0;
        std::string info;
        if (fence_start(line, fch, flen, info, find)) {
            MdBlock b;
            b.kind = MdBlock::Kind::Code;
            b.line = base_line + i;
            const std::size_t space = info.find_first_of(" \t{");
            b.language = lower(space == std::string::npos ? info : info.substr(0, space));
            ++i;
            std::string code;
            bool first = true;
            while (i < n && !fence_end(lines[i], fch, flen)) {
                if (!first) code += '\n';
                code += strip_indent(lines[i], find);
                first = false;
                ++i;
            }
            if (i < n) ++i;
            b.code = code;
            out.push_back(std::move(b));
            continue;
        }

        std::string htext;
        if (const int level = heading_level(line, htext)) {
            MdBlock b;
            b.kind = MdBlock::Kind::Heading;
            b.level = level;
            b.line = base_line + i;
            b.spans = parse_markdown_inline(htext);
            out.push_back(std::move(b));
            ++i;
            continue;
        }

        if (is_rule(line)) {
            MdBlock b;
            b.kind = MdBlock::Kind::Rule;
            b.line = base_line + i;
            out.push_back(std::move(b));
            ++i;
            continue;
        }

        if (quote_line(line)) {
            std::vector<std::string> inner;
            const std::size_t start = i;
            bool last_blank = false;
            while (i < n) {
                const std::string& l = lines[i];
                if (quote_line(l)) {
                    std::string rest = l.substr(indent_of(l) + 1);
                    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                    inner.push_back(rest);
                    last_blank = blank(rest);
                    ++i;
                } else if (!blank(l) && !last_blank && !interrupts_paragraph(l) && !inner.empty()) {
                    inner.push_back(l);
                    ++i;
                } else {
                    break;
                }
            }
            MdBlock b;
            b.kind = MdBlock::Kind::Quote;
            b.line = base_line + start;
            parse_blocks(inner, base_line + start, b.children);
            out.push_back(std::move(b));
            continue;
        }

        if (const ListMarker first = list_marker(line); first.ok) {
            MdBlock list;
            list.kind = MdBlock::Kind::List;
            list.ordered = first.ordered;
            list.start = first.number;
            list.line = base_line + i;
            while (i < n) {
                const ListMarker m = list_marker(lines[i]);
                if (!m.ok || m.ordered != first.ordered || m.bullet != first.bullet || is_rule(lines[i])) break;
                const std::size_t item_line = base_line + i;
                std::vector<std::string> body;
                body.push_back(m.empty ? std::string() : lines[i].substr(m.content));
                ++i;
                while (i < n) {
                    const std::string& l = lines[i];
                    if (blank(l)) {
                        body.push_back(std::string());
                        ++i;
                        continue;
                    }
                    if (indent_of(l) >= m.content) {
                        body.push_back(l.substr(m.content));
                        ++i;
                        continue;
                    }
                    const bool after_blank = !body.empty() && blank(body.back());
                    if (!after_blank && !interrupts_paragraph(l) && !list_marker(l).ok) {
                        body.push_back(trim(l));
                        ++i;
                        continue;
                    }
                    break;
                }
                std::size_t trailing = 0;
                while (!body.empty() && blank(body.back())) {
                    body.pop_back();
                    ++trailing;
                }
                MdItem item;
                if (!body.empty()) {
                    std::string& head = body.front();
                    if (head.size() >= 3 && head[0] == '[' && head[2] == ']' && (head[1] == ' ' || head[1] == 'x' || head[1] == 'X') && (head.size() == 3 || head[3] == ' ')) {
                        item.task = true;
                        item.checked = head[1] != ' ';
                        head = head.size() > 4 ? head.substr(4) : std::string();
                    }
                }
                parse_blocks(body, item_line, item.blocks);
                list.items.push_back(std::move(item));
                if (trailing > 0 && i < n) {
                    const ListMarker next = list_marker(lines[i]);
                    if (!next.ok || next.ordered != first.ordered || next.bullet != first.bullet) {
                        i -= std::min(trailing, i);
                        while (i < n && blank(lines[i])) ++i;
                        break;
                    }
                }
            }
            out.push_back(std::move(list));
            continue;
        }

        if (indent_of(line) >= 4) {
            MdBlock b;
            b.kind = MdBlock::Kind::Code;
            b.line = base_line + i;
            std::vector<std::string> code;
            while (i < n && (blank(lines[i]) || indent_of(lines[i]) >= 4)) {
                code.push_back(blank(lines[i]) ? std::string() : lines[i].substr(4));
                ++i;
            }
            while (!code.empty() && code.back().empty()) code.pop_back();
            for (std::size_t k = 0; k < code.size(); ++k) {
                if (k) b.code += '\n';
                b.code += code[k];
            }
            out.push_back(std::move(b));
            continue;
        }

        std::vector<MdAlign> align;
        if (line.find('|') != std::string::npos && i + 1 < n && delimiter_row(lines[i + 1], align)) {
            const std::vector<std::string> head = split_row(line);
            if (head.size() == align.size()) {
                MdBlock b;
                b.kind = MdBlock::Kind::Table;
                b.line = base_line + i;
                b.align = align;
                auto add_row = [&](const std::vector<std::string>& cells) {
                    std::vector<std::vector<MdSpan>> row;
                    for (std::size_t c = 0; c < align.size(); ++c) row.push_back(parse_markdown_inline(c < cells.size() ? cells[c] : std::string()));
                    b.rows.push_back(std::move(row));
                };
                add_row(head);
                i += 2;
                while (i < n && !blank(lines[i]) && lines[i].find('|') != std::string::npos && !interrupts_paragraph(lines[i])) {
                    add_row(split_row(lines[i]));
                    ++i;
                }
                out.push_back(std::move(b));
                continue;
            }
        }

        if (html_start(line)) {
            MdBlock b;
            b.kind = MdBlock::Kind::Html;
            b.line = base_line + i;
            std::string raw;
            while (i < n && !blank(lines[i])) {
                if (!raw.empty()) raw += '\n';
                raw += lines[i];
                ++i;
            }
            b.code = raw;
            const std::string text = trim(strip_html(raw));
            if (!text.empty()) b.spans = parse_markdown_inline(text);
            out.push_back(std::move(b));
            continue;
        }

        para_line = base_line + i;
        para.push_back(line);
        ++i;
    }
    finish_paragraph(para, para_line, out);
}

} // namespace

bool is_markdown_path(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return false;
    const std::string ext = lower(path.substr(dot));
    return ext == ".md" || ext == ".markdown" || ext == ".mdown" || ext == ".mkd" || ext == ".mkdn";
}

std::vector<MdSpan> parse_markdown_inline(const std::string& text) {
    std::vector<MdSpan> out;
    InlineParser parser(out);
    parser.run(text, Style{});
    return out;
}

std::vector<MdBlock> parse_markdown(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    std::string body = text;
    if (body.rfind("\xEF\xBB\xBF", 0) == 0) body.erase(0, 3);
    while (start <= body.size()) {
        const std::size_t end = body.find('\n', start);
        std::string line = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(expand_tabs(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::size_t first = 0;
    if (!lines.empty() && trim(lines[0]) == "---") {
        for (std::size_t k = 1; k < lines.size(); ++k) {
            const std::string t = trim(lines[k]);
            if (t == "---" || t == "...") {
                first = k + 1;
                break;
            }
        }
    }
    std::vector<MdBlock> out;
    parse_blocks(std::vector<std::string>(lines.begin() + static_cast<std::ptrdiff_t>(first), lines.end()), first, out);
    return out;
}

std::string markdown_plain_text(const std::vector<MdSpan>& spans) {
    std::string out;
    for (const MdSpan& s : spans) {
        if (s.line_break) out += ' ';
        else out += s.text;
    }
    return out;
}

std::string markdown_anchor(const std::string& heading) {
    std::string out;
    for (const char c : lower(heading)) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || static_cast<unsigned char>(c) >= 0x80) out += c;
        else if (c == ' ') out += '-';
    }
    return out;
}

} // namespace canopy
