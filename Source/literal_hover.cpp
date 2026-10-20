#include <canopy/code_intel.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

namespace {

const char* const k_dot = " \xC2\xB7 ";

struct BigUint {
    std::vector<std::uint32_t> limbs;

    bool zero() const { return limbs.empty(); }

    void trim() {
        while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
    }

    void mul_add(std::uint32_t mul, std::uint32_t add) {
        std::uint64_t carry = add;
        for (std::uint32_t& l : limbs) {
            const std::uint64_t v = static_cast<std::uint64_t>(l) * mul + carry;
            l = static_cast<std::uint32_t>(v);
            carry = v >> 32;
        }
        if (carry) limbs.push_back(static_cast<std::uint32_t>(carry));
    }

    std::uint32_t div_small(std::uint32_t d) {
        std::uint64_t rem = 0;
        for (std::size_t i = limbs.size(); i-- > 0;) {
            const std::uint64_t cur = (rem << 32) | limbs[i];
            limbs[i] = static_cast<std::uint32_t>(cur / d);
            rem = cur % d;
        }
        trim();
        return static_cast<std::uint32_t>(rem);
    }

    std::size_t bits() const {
        if (limbs.empty()) return 0;
        std::size_t n = (limbs.size() - 1) * 32;
        std::uint32_t top = limbs.back();
        while (top) {
            ++n;
            top >>= 1;
        }
        return n;
    }

    std::string to_base(std::uint32_t base) const {
        if (limbs.empty()) return "0";
        BigUint copy = *this;
        std::string out;
        while (!copy.zero()) {
            const std::uint32_t d = copy.div_small(base);
            out.push_back("0123456789ABCDEF"[d]);
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    std::optional<std::uint64_t> small() const {
        if (limbs.size() > 2) return std::nullopt;
        std::uint64_t v = 0;
        for (std::size_t i = limbs.size(); i-- > 0;) v = (v << 32) | limbs[i];
        return v;
    }
};

std::string grouped(const std::string& digits, std::size_t group, const std::string& sep) {
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % group == 0) out += sep;
        out += digits[i];
    }
    return out;
}

std::string code(const std::string& s) {
    std::size_t ticks = 1;
    for (std::size_t run = 0, i = 0; i < s.size(); ++i) {
        run = s[i] == '`' ? run + 1 : 0;
        ticks = std::max(ticks, run + 1);
    }
    const std::string fence(ticks, '`');
    const bool pad = !s.empty() && (s.front() == '`' || s.back() == '`');
    return fence + (pad ? " " : "") + s + (pad ? " " : "") + fence;
}

std::string hex_upper(std::uint64_t v, int width) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%0*llX", width, static_cast<unsigned long long>(v));
    return buf;
}

void utf8_append(std::uint32_t cp, std::string& out) {
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

bool utf8_next(const std::string& s, std::size_t& i, std::uint32_t& cp) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (len == 0 || i + len > s.size()) {
        cp = c;
        ++i;
        return false;
    }
    cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (std::size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    return true;
}

const char* control_name(std::uint32_t cp) {
    static const char* const low[] = { "null (NUL)", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "bell (BEL)", "backspace (BS)", "tab", "newline (LF)", "vertical tab (VT)", "form feed (FF)", "carriage return (CR)", "SO", "SI", "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB", "CAN", "EM", "SUB", "escape (ESC)", "FS", "GS", "RS", "US" };
    if (cp < 32) return low[cp];
    if (cp == 32) return "space";
    if (cp == 127) return "DEL (delete)";
    if (cp == 0xA0) return "no-break space";
    if (cp == 0xAD) return "soft hyphen";
    if (cp == 0x200B) return "zero-width space";
    if (cp == 0x200D) return "zero-width joiner";
    if (cp == 0xFEFF) return "byte order mark";
    return nullptr;
}

std::string visible(std::uint32_t cp) {
    std::string out;
    if (cp < 32) {
        utf8_append(0x2400 + cp, out);
        if (cp == '\n') out += "\n";
    } else if (cp == 127) {
        utf8_append(0x2421, out);
    } else {
        utf8_append(cp, out);
    }
    return out;
}

std::string code_point_text(std::uint32_t cp) {
    std::string out = std::to_string(cp) + " (0x" + hex_upper(cp, cp > 0xFF ? 4 : 2) + ", U+" + hex_upper(cp, 4) + ")";
    return out;
}

std::string utf8_bytes_text(std::uint32_t cp) {
    std::string bytes;
    utf8_append(cp, bytes);
    std::string out;
    for (unsigned char b : bytes) {
        if (!out.empty()) out += ' ';
        out += hex_upper(b, 2);
    }
    return out;
}

struct Decoded {
    std::vector<std::uint32_t> points;
    std::vector<std::pair<std::string, std::uint32_t>> escapes;
    std::string error;
    bool interpolated = false;
};

bool hex_digit(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

std::uint32_t hex_value(char c) {
    if (c >= '0' && c <= '9') return static_cast<std::uint32_t>(c - '0');
    return static_cast<std::uint32_t>(std::tolower(static_cast<unsigned char>(c)) - 'a' + 10);
}

Decoded decode_body(const std::string& body, bool walnut, bool raw, bool text_literal) {
    Decoded d;
    std::size_t i = 0;
    while (i < body.size()) {
        if (text_literal && body[i] == '$' && i + 1 < body.size() && body[i + 1] == '{') {
            d.interpolated = true;
            int depth = 0;
            const std::size_t from = i;
            for (; i < body.size(); ++i) {
                if (body[i] == '{') ++depth;
                else if (body[i] == '}' && --depth == 0) {
                    ++i;
                    break;
                }
            }
            for (std::size_t k = from; k < i; ++k) d.points.push_back(static_cast<unsigned char>(body[k]));
            continue;
        }
        if (raw || body[i] != '\\') {
            std::uint32_t cp = 0;
            utf8_next(body, i, cp);
            d.points.push_back(cp);
            continue;
        }
        const std::size_t start = i;
        if (i + 1 >= body.size()) {
            d.error = "ends with a lone backslash";
            return d;
        }
        const char e = body[i + 1];
        i += 2;
        std::uint32_t cp = 0;
        bool ok = true;
        switch (e) {
            case 'n': cp = 10; break;
            case 't': cp = 9; break;
            case 'r': cp = 13; break;
            case 'a': cp = 7; break;
            case 'b': cp = 8; break;
            case 'f': cp = 12; break;
            case 'v': cp = 11; break;
            case 'e': cp = 27; break;
            case '\\': case '\'': case '"': case '?': cp = static_cast<unsigned char>(e); break;
            case '`': case '$':
                if (walnut) cp = static_cast<unsigned char>(e);
                else ok = false;
                break;
            case 'x': {
                if (i >= body.size() || !hex_digit(body[i])) {
                    ok = false;
                    break;
                }
                while (i < body.size() && hex_digit(body[i])) {
                    cp = cp * 16 + hex_value(body[i]);
                    ++i;
                    if (cp > 0x10FFFF) break;
                }
                break;
            }
            case 'u':
            case 'U': {
                if (walnut && e == 'u' && i < body.size() && body[i] == '{') {
                    ++i;
                    std::size_t digits = 0;
                    while (i < body.size() && hex_digit(body[i])) {
                        cp = cp * 16 + hex_value(body[i]);
                        ++i;
                        ++digits;
                        if (cp > 0x10FFFF) break;
                    }
                    if (digits == 0 || i >= body.size() || body[i] != '}') ok = false;
                    else ++i;
                    break;
                }
                const std::size_t want = e == 'u' ? 4 : 8;
                for (std::size_t k = 0; k < want; ++k) {
                    if (i >= body.size() || !hex_digit(body[i])) {
                        ok = false;
                        break;
                    }
                    cp = cp * 16 + hex_value(body[i]);
                    ++i;
                }
                break;
            }
            default:
                if (e >= '0' && e <= '7') {
                    if (walnut && e == '0') {
                        cp = 0;
                        break;
                    }
                    if (walnut) {
                        ok = false;
                        break;
                    }
                    cp = static_cast<std::uint32_t>(e - '0');
                    for (int k = 0; k < 2 && i < body.size() && body[i] >= '0' && body[i] <= '7'; ++k, ++i) cp = cp * 8 + static_cast<std::uint32_t>(body[i] - '0');
                } else {
                    ok = false;
                }
        }
        const std::string spelled = body.substr(start, i - start);
        if (!ok) {
            d.error = "has an escape Walnut doesn't know: " + code(spelled);
            if (!walnut) d.error = "has an escape C doesn't know: " + code(spelled);
            return d;
        }
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF && (walnut || e == 'u' || e == 'U'))) {
            d.error = "has " + code(spelled) + ", which isn't a valid Unicode character";
            return d;
        }
        d.points.push_back(cp);
        if (d.escapes.size() < 8) d.escapes.emplace_back(spelled, cp);
    }
    return d;
}

std::string escapes_text(const Decoded& d) {
    if (d.escapes.empty()) return std::string();
    std::string out = "**Escapes:** ";
    for (std::size_t i = 0; i < d.escapes.size(); ++i) {
        if (i > 0) out += ", ";
        const std::uint32_t cp = d.escapes[i].second;
        out += code(d.escapes[i].first) + " is ";
        if (const char* name = control_name(cp)) out += name;
        else out += code(visible(cp));
        out += " (U+" + hex_upper(cp, 4) + ")";
    }
    return out + "\n";
}

std::string content_block(const std::vector<std::uint32_t>& points) {
    std::string body;
    for (std::uint32_t cp : points) body += visible(cp);
    if (body.size() > 1500) body = body.substr(0, 1500) + "\xE2\x80\xA6";
    std::string fence = "```";
    while (body.find(fence) != std::string::npos) fence += "`";
    return fence + "text\n" + body + "\n" + fence + "\n";
}

std::size_t utf8_size(const std::vector<std::uint32_t>& points) {
    std::size_t n = 0;
    for (std::uint32_t cp : points) n += cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    return n;
}

std::size_t utf16_size(const std::vector<std::uint32_t>& points) {
    std::size_t n = 0;
    for (std::uint32_t cp : points) n += cp >= 0x10000 ? 2 : 1;
    return n;
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

struct Span {
    char kind = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

bool ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }
bool ident_char(char c) { return ident_start(c) || std::isdigit(static_cast<unsigned char>(c)); }

std::optional<Span> literal_at(const std::string& text, bool walnut, std::size_t offset) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const std::size_t close = text.find("*/", i + 2);
            i = close == std::string::npos ? n : close + 2;
            continue;
        }
        if (!walnut && c == '#' ) {
            std::size_t k = i;
            while (k > 0 && (text[k - 1] == ' ' || text[k - 1] == '\t')) --k;
            if (k == 0 || text[k - 1] == '\n') {
                std::size_t e = i + 1;
                while (e < n && (text[e] == ' ' || text[e] == '\t')) ++e;
                if (text.compare(e, 7, "include") == 0) {
                    while (i < n && text[i] != '\n') ++i;
                    continue;
                }
            }
        }
        if (ident_start(c)) {
            const std::size_t from = i;
            while (i < n && ident_char(text[i])) ++i;
            const std::string word = text.substr(from, i - from);
            if (!walnut && i < n && (text[i] == '"' || text[i] == '\'') && (word == "u8" || word == "u" || word == "U" || word == "L" || word == "R" || word == "u8R" || word == "uR" || word == "UR" || word == "LR")) {
                const bool raw = word.back() == 'R' && text[i] == '"';
                std::size_t e = i + 1;
                if (raw) {
                    const std::size_t paren = text.find('(', e);
                    if (paren == std::string::npos) return std::nullopt;
                    const std::string close = ")" + text.substr(e, paren - e) + "\"";
                    const std::size_t end = text.find(close, paren + 1);
                    e = end == std::string::npos ? n : end + close.size();
                } else {
                    const char q = text[i];
                    while (e < n && text[e] != q && text[e] != '\n') e += text[e] == '\\' ? 2 : 1;
                    e = std::min(n, e + 1);
                }
                while (e < n && ident_char(text[e])) ++e;
                if (offset >= from && offset < e) return Span{ text[i] == '"' ? 's' : 'c', from, e };
                i = e;
            }
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            const std::size_t from = i;
            ++i;
            while (i < n) {
                const char d = text[i];
                if (ident_char(d) || d == '.') {
                    ++i;
                } else if (d == '\'' && i + 1 < n && std::isxdigit(static_cast<unsigned char>(text[i + 1])) && std::isxdigit(static_cast<unsigned char>(text[i - 1]))) {
                    ++i;
                } else if ((d == '+' || d == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E' || text[i - 1] == 'p' || text[i - 1] == 'P')) {
                    const bool hex = i - from > 2 && text[from] == '0' && (text[from + 1] == 'x' || text[from + 1] == 'X');
                    if (hex && (text[i - 1] == 'e' || text[i - 1] == 'E')) break;
                    ++i;
                } else {
                    break;
                }
            }
            if (offset >= from && offset < i) return Span{ 'n', from, i };
            continue;
        }
        if (c == '"' || c == '\'' || (walnut && c == '`')) {
            const std::size_t from = i;
            std::size_t e = i + 1;
            int depth = 0;
            while (e < n) {
                const char d = text[e];
                if (d == '\\') {
                    e += 2;
                    continue;
                }
                if (c == '`' && d == '$' && e + 1 < n && text[e + 1] == '{') {
                    ++depth;
                    e += 2;
                    continue;
                }
                if (depth > 0) {
                    if (d == '{') ++depth;
                    else if (d == '}') --depth;
                    ++e;
                    continue;
                }
                if (d == c) break;
                if (d == '\n' && (c == '\'' || !walnut)) break;
                ++e;
            }
            e = std::min(n, e + 1);
            if (!walnut) {
                while (e < n && ident_char(text[e])) ++e;
            }
            if (offset >= from && offset < e) return Span{ c == '\'' ? 'c' : c == '`' ? 't' : 's', from, e };
            i = e;
            continue;
        }
        ++i;
    }
    return std::nullopt;
}

std::string int_type_bits(const std::string& type) {
    {
        if (type.find("long long") != std::string::npos) return "64 bits";
        if (type.find("long") != std::string::npos) {
#if defined(_WIN32)
            return "32 bits";
#else
            return "64 bits";
#endif
        }
        if (type.find("size_t") != std::string::npos || type.find("ptrdiff") != std::string::npos) return "64 bits";
        if (type.find("int") != std::string::npos) return "32 bits";
        return std::string();
    }
    return std::string();
}

std::string walnut_number_type(bool unsig, int steps, char family, bool floating, std::size_t& bits) {
    int base_bits = family == 'i' ? 512 : family == 'd' ? 512 : floating ? 128 : 32;
    double b = base_bits * std::pow(2.0, steps);
    bits = static_cast<std::size_t>(b);
    std::string size;
    const int s = steps;
    if (s <= -2) size = "short short ";
    else if (s == -1) size = "short ";
    else if (s == 1) size = "long ";
    else if (s >= 2) size = "long long ";
    std::string word = family == 'i' ? "integer" : family == 'd' ? "double" : floating ? "float" : "int";
    return (unsig ? "unsigned " : "") + size + word;
}

std::optional<HoverResult> number_hover(const std::string& tok, bool walnut) {
    std::string t;
    for (char c : tok) {
        if (c == '\'' || (walnut && c == '_')) continue;
        t += c;
    }
    if (t.empty()) return std::nullopt;
    int base = 10;
    std::size_t at = 0;
    bool leading_zero_octal = false;
    if (t.size() > 1 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        at = 2;
    } else if (t.size() > 1 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B')) {
        base = 2;
        at = 2;
    } else if (walnut && t.size() > 1 && t[0] == '0' && (t[1] == 'o' || t[1] == 'O')) {
        base = 8;
        at = 2;
    } else if (!walnut && t.size() > 1 && t[0] == '0' && std::isdigit(static_cast<unsigned char>(t[1]))) {
        leading_zero_octal = true;
    }
    auto digit_ok = [&](char c) {
        if (base == 16) return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        if (base == 2) return c == '0' || c == '1';
        if (base == 8) return c >= '0' && c <= '7';
        return std::isdigit(static_cast<unsigned char>(c)) != 0;
    };
    std::size_t end = at;
    while (end < t.size() && digit_ok(t[end])) ++end;
    bool floating = false;
    if (base == 10) {
        std::size_t k = end;
        if (k < t.size() && t[k] == '.') {
            floating = true;
            ++k;
            while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
        }
        if (k < t.size() && (t[k] == 'e' || t[k] == 'E') && k + 1 < t.size() && (std::isdigit(static_cast<unsigned char>(t[k + 1])) || ((t[k + 1] == '+' || t[k + 1] == '-') && k + 2 < t.size() && std::isdigit(static_cast<unsigned char>(t[k + 2]))))) {
            floating = true;
            k += 2;
            while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
        }
        end = k;
    } else if (base == 16 && !walnut) {
        std::size_t k = end;
        if (k < t.size() && t[k] == '.') {
            floating = true;
            ++k;
            while (k < t.size() && std::isxdigit(static_cast<unsigned char>(t[k]))) ++k;
        }
        if (k < t.size() && (t[k] == 'p' || t[k] == 'P')) {
            floating = true;
            ++k;
            if (k < t.size() && (t[k] == '+' || t[k] == '-')) ++k;
            while (k < t.size() && std::isdigit(static_cast<unsigned char>(t[k]))) ++k;
        }
        end = k;
    }
    const std::string number = t.substr(0, end);
    const std::string suffix = t.substr(end);
    if (number.size() == at) return std::nullopt;

    std::string type;
    std::string note;
    std::size_t walnut_bits = 0;
    if (walnut) {
        bool unsig = false;
        int s_count = 0;
        int l_count = 0;
        char family = 0;
        for (char raw : suffix) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
            if (c == 'u' && !unsig) unsig = true;
            else if (c == 's' && l_count == 0 && s_count < 2) ++s_count;
            else if (c == 'l' && s_count == 0 && l_count < 2) ++l_count;
            else if ((c == 'i' || c == 'f' || c == 'd') && !family) family = c;
            else return HoverResult{ 0, 0, 0, code(tok) + " has a suffix Walnut doesn't know: " + code(suffix) + ". Suffixes are made of `u`, up to two `s` or `l`, and one of `i`, `f` or `d`.\n" };
        }
        if (family == 'i' && floating) return HoverResult{ 0, 0, 0, code(tok) + ": `i` only goes on whole numbers.\n" };
        if (family == 'f' || family == 'd') floating = true;
        const int steps = l_count - s_count;
        if (!floating && family != 'i' && s_count == 0 && l_count == 0) {
            type.clear();
        } else {
            type = walnut_number_type(unsig, steps, family, floating, walnut_bits);
        }
        if (!floating && type.empty()) {
            BigUint v;
            for (std::size_t k = at; k < number.size(); ++k) v.mul_add(static_cast<std::uint32_t>(base), hex_value(number[k]));
            v.trim();
            const std::size_t need = v.bits() + (unsig ? 0 : 1);
            const char* names[] = { "int", "long int", "long long int", "short integer", "integer", "long integer", "long long integer" };
            const std::size_t widths[] = { 32, 64, 128, 256, 512, 1024, 2048 };
            for (std::size_t k = 0; k < 7; ++k) {
                if (need <= widths[k]) {
                    type = std::string(unsig ? "unsigned " : "") + names[k];
                    walnut_bits = widths[k];
                    break;
                }
            }
            if (type.empty()) {
                type = "too big for any integer type";
                walnut_bits = 0;
            }
        }
        if (!floating && leading_zero_octal) note = "A leading 0 doesn't make it octal in Walnut; write `0o" + number.substr(1) + "` for that.";
        if (!floating && number.size() > 1 && number[0] == '0' && std::isdigit(static_cast<unsigned char>(number[1]))) note = "A leading 0 doesn't make it octal in Walnut; write `0o" + number.substr(number.find_first_not_of('0') == std::string::npos ? number.size() - 1 : number.find_first_not_of('0')) + "` for that.";
    } else {
        std::string sfx;
        for (char c : suffix) sfx += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (floating) {
            if (sfx.empty()) type = "double";
            else if (sfx == "f") type = "float";
            else if (sfx == "l") type = "long double";
            else if (sfx == "f16" || sfx == "f32" || sfx == "f64" || sfx == "f128" || sfx == "bf16") type = "std::" + std::string(sfx == "bf16" ? "bfloat16_t" : "float" + sfx.substr(1) + "_t");
            else return HoverResult{ 0, 0, 0, code(tok) + " has a suffix C++ doesn't know: " + code(suffix) + "\n" };
        } else {
            const bool u = sfx.find('u') != std::string::npos;
            std::string rest = sfx;
            rest.erase(std::remove(rest.begin(), rest.end(), 'u'), rest.end());
            if (std::count(sfx.begin(), sfx.end(), 'u') > 1 || !(rest.empty() || rest == "l" || rest == "ll" || rest == "z")) {
                return HoverResult{ 0, 0, 0, code(tok) + " has a suffix C++ doesn't know: " + code(suffix) + "\n" };
            }
            BigUint v;
            for (std::size_t k = at; k < number.size(); ++k) v.mul_add(static_cast<std::uint32_t>(leading_zero_octal ? 8 : base), hex_value(number[k]));
            v.trim();
            const std::size_t bits = v.bits();
#if defined(_WIN32)
            const std::size_t long_bits = 32;
#else
            const std::size_t long_bits = 64;
#endif
            const bool decimal = base == 10 && !leading_zero_octal;
            std::vector<std::pair<std::string, std::size_t>> order;
            auto add = [&](const std::string& name, std::size_t width, bool is_unsigned) {
                if (u && !is_unsigned) return;
                if (decimal && !u && is_unsigned) return;
                order.emplace_back(name, is_unsigned ? width : width - 1);
            };
            if (rest == "z") {
                add(u ? "std::size_t" : "std::make_signed_t<std::size_t>", 64, u);
            } else {
                if (rest.empty()) {
                    add("int", 32, false);
                    add("unsigned int", 32, true);
                }
                if (rest.empty() || rest == "l") {
                    add("long", long_bits, false);
                    add("unsigned long", long_bits, true);
                }
                add("long long", 64, false);
                add("unsigned long long", 64, true);
            }
            for (const auto& [name, room] : order) {
                if (bits <= room) {
                    type = name;
                    break;
                }
            }
            if (type.empty()) type = "too big for any integer type";
            if (leading_zero_octal) note = "The leading 0 makes this octal in C and C++.";
        }
    }

    std::string md;
    if (floating) {
        std::string clean = number;
        if (!walnut && base == 16) clean = "0x" + number.substr(2);
        const long double value = std::strtold(clean.c_str(), nullptr);
        char exact[96];
        std::snprintf(exact, sizeof exact, "%.21Lg", value);
        char sci[96];
        std::snprintf(sci, sizeof sci, "%.6Le", value);
        md = "**Floating-point literal** " + code(type);
        if (walnut && walnut_bits) md += " (" + std::to_string(walnut_bits) + " bits)";
        else if (!walnut) md += type == "float" ? " (32 bits)" : type == "double" ? " (64 bits)" : "";
        md += "\n**Value:** " + code(exact) + k_dot + code(sci) + "\n";
        if (!walnut && type != "long double" && type.find("std::") == std::string::npos) {
            char rounded[96];
            if (type == "float") std::snprintf(rounded, sizeof rounded, "%.9g", static_cast<double>(static_cast<float>(value)));
            else std::snprintf(rounded, sizeof rounded, "%.17g", static_cast<double>(value));
            md += "**Stored as:** " + code(rounded) + "\n";
        }
        if (std::isinf(static_cast<double>(value))) md += "Too large to store; it becomes infinity.\n";
    } else {
        BigUint v;
        const int used_base = leading_zero_octal ? 8 : base;
        for (std::size_t k = at; k < number.size(); ++k) v.mul_add(static_cast<std::uint32_t>(used_base), hex_value(number[k]));
        v.trim();
        md = "**Integer literal** " + code(type);
        const std::string bits = walnut ? (walnut_bits ? std::to_string(walnut_bits) + " bits" : std::string()) : int_type_bits(type);
        if (!bits.empty()) md += " (" + bits + ")";
        md += "\n";
        const std::string dec = grouped(v.to_base(10), 3, walnut ? "_" : "'");
        const std::string hex = v.to_base(16);
        const std::string oct = v.to_base(8);
        std::string bin = v.to_base(2);
        md += "**Decimal** " + code(dec) + k_dot + "**Hex** " + code("0x" + grouped(hex, 4, walnut ? "_" : "'")) + k_dot + "**Octal** " + code((walnut ? "0o" : "0") + oct) + "\n";
        if (bin.size() <= 128) md += "**Binary** " + code("0b" + grouped(bin, 4, walnut ? "_" : "'")) + "\n";
        else md += "**Binary** " + std::to_string(bin.size()) + " digits\n";
        if (const auto small = v.small(); small && ((*small >= 32 && *small < 127) || (*small >= 0xA0 && *small <= 0x2FFFF && !(*small >= 0xD800 && *small <= 0xDFFF) && !(*small >= 0xFDD0 && *small <= 0xFDEF) && (*small & 0xFFFE) != 0xFFFE))) {
            md += "**As a character:** " + code(visible(static_cast<std::uint32_t>(*small))) + " (U+" + hex_upper(*small, 4) + ")\n";
        }
        if (walnut && walnut_bits && v.bits() > walnut_bits - (type.rfind("unsigned", 0) == 0 ? 0 : 1)) md += "Too big for " + code(type) + "; it wraps around.\n";
    }
    if (!suffix.empty()) md += "**Suffix:** " + code(suffix) + "\n";
    if (!note.empty()) md += note + "\n";
    return HoverResult{ 0, 0, 0, md };
}

} // namespace

std::optional<HoverResult> literal_hover(const std::string& language, const std::string& text, std::size_t line, std::size_t column) {
    const bool walnut = language == "walnut";
    if (!walnut && language != "c" && language != "cpp") return std::nullopt;
    std::size_t offset = 0;
    for (std::size_t l = 0; l < line; ++l) {
        const std::size_t nl = text.find('\n', offset);
        if (nl == std::string::npos) return std::nullopt;
        offset = nl + 1;
    }
    offset += column;
    if (offset >= text.size()) return std::nullopt;
    const std::optional<Span> span = literal_at(text, walnut, offset);
    if (!span) return std::nullopt;
    const std::string tok = text.substr(span->begin, span->end - span->begin);
    std::size_t start_line = 0;
    std::size_t line_start = 0;
    for (std::size_t k = 0; k < span->begin; ++k) {
        if (text[k] == '\n') {
            ++start_line;
            line_start = k + 1;
        }
    }
    const std::size_t nl = tok.find('\n');
    HoverResult result;
    result.line = start_line;
    result.column = span->begin - line_start;
    result.length = nl == std::string::npos ? tok.size() : nl;
    if (start_line != line) {
        result.line = line;
        result.column = 0;
        std::size_t this_start = offset - column;
        const std::size_t this_end = text.find('\n', this_start);
        result.length = std::min(span->end, this_end == std::string::npos ? text.size() : this_end) - this_start;
    }

    if (span->kind == 'n') {
        std::optional<HoverResult> h = number_hover(tok, walnut);
        if (!h) return std::nullopt;
        result.markdown = h->markdown;
        return result;
    }

    std::string prefix;
    std::size_t q = 0;
    while (q < tok.size() && tok[q] != '"' && tok[q] != '\'' && tok[q] != '`') ++q;
    prefix = tok.substr(0, q);
    const char quote = q < tok.size() ? tok[q] : '"';
    std::size_t close = tok.rfind(quote);
    if (close == std::string::npos || close <= q) close = tok.size();
    std::string suffix = close < tok.size() ? tok.substr(close + 1) : std::string();
    const bool raw = !walnut && !prefix.empty() && prefix.back() == 'R';
    std::string body;
    if (raw) {
        const std::size_t open = tok.find('(', q);
        const std::size_t end = tok.rfind(')');
        if (open != std::string::npos && end != std::string::npos && end > open) body = tok.substr(open + 1, end - open - 1);
    } else {
        body = tok.substr(q + 1, close > q ? close - q - 1 : 0);
    }
    const bool unclosed = close == tok.size();
    const Decoded d = decode_body(body, walnut, raw, span->kind == 't');
    std::string md;
    if (span->kind == 'c') {
        std::string type = walnut ? "char" : language == "c" ? "int" : "char";
        const std::string p = raw ? prefix.substr(0, prefix.size() - 1) : prefix;
        if (p == "u8") type = language == "c" ? "unsigned char" : "char8_t";
        else if (p == "u") type = language == "c" ? "char16_t (unsigned short)" : "char16_t";
        else if (p == "U") type = language == "c" ? "char32_t (unsigned int)" : "char32_t";
        else if (p == "L") type = "wchar_t";
        md = "**Character literal** " + code(type) + "\n";
        if (!d.error.empty()) md += "This character literal " + d.error + ".\n";
        else if (d.points.empty()) md += "It's empty; a character literal holds one character.\n";
        else if (d.points.size() > 1) {
            if (walnut) md += "It holds " + std::to_string(d.points.size()) + " characters, but a `char` holds exactly one; use a string (`\"...\"`) instead.\n";
            else {
                std::uint64_t value = 0;
                for (std::uint32_t cp : d.points) value = (value << 8) | (cp & 0xFF);
                md += "A multi-character literal: an `int` whose value the compiler chooses (usually " + code(std::to_string(static_cast<std::int32_t>(value))) + "). It's almost always a mistake for a string.\n";
            }
        } else {
            const std::uint32_t cp = d.points.front();
            md += "**Character:** " + code(visible(cp));
            if (const char* name = control_name(cp)) md += " " + std::string(name);
            md += "\n**Code:** " + code_point_text(cp) + "\n";
            if (cp >= 0x80) md += "**UTF-8:** " + code(utf8_bytes_text(cp)) + "\n";
            if (!walnut && prefix.empty() && cp >= 0x80) md += "Doesn't fit a plain `char`; the compiler makes it a multi-byte `int` (or rejects it). Use `U'...'` for one Unicode character.\n";
            if (!walnut && p == "u" && cp >= 0x10000) md += "Doesn't fit in one `char16_t`.\n";
            md += escapes_text(d);
        }
        if (unclosed) md += "It's never closed with `'`.\n";
        result.markdown = md;
        return result;
    }

    std::string type;
    std::size_t units = 0;
    std::string unit_name;
    if (walnut) {
        type = span->kind == 't' ? "text" : "string";
    } else {
        const std::string p = raw ? prefix.substr(0, prefix.size() - 1) : prefix;
        const std::size_t bytes = utf8_size(d.points);
        if (p.empty()) {
            type = "const char[" + std::to_string(bytes + 1) + "]";
        } else if (p == "u8") {
            type = (language == "c" ? "const char[" : "const char8_t[") + std::to_string(bytes + 1) + "]";
        } else if (p == "u") {
            units = utf16_size(d.points);
            unit_name = "UTF-16 units";
            type = "const char16_t[" + std::to_string(units + 1) + "]";
        } else if (p == "U") {
            units = d.points.size();
            unit_name = "UTF-32 units";
            type = "const char32_t[" + std::to_string(units + 1) + "]";
        } else if (p == "L") {
#if defined(_WIN32)
            units = utf16_size(d.points);
#else
            units = d.points.size();
#endif
            unit_name = "wchar_t units";
            type = "const wchar_t[" + std::to_string(units + 1) + "]";
        }
        if (language == "cpp" && suffix == "s") type = "std::string";
        else if (language == "cpp" && suffix == "sv") type = "std::string_view";
    }
    md = std::string(span->kind == 't' ? "**Text literal** " : raw ? "**Raw string literal** " : "**String literal** ") + code(type) + "\n";
    if (!d.error.empty()) {
        md += "This literal " + d.error + ".\n";
        result.markdown = md;
        return result;
    }
    const std::size_t bytes = utf8_size(d.points);
    if (d.interpolated) {
        md += "**Length:** depends on the `${...}` values put in when it runs\n";
    } else {
        md += "**Length:** " + plural(d.points.size(), "character", "characters");
        if (bytes != d.points.size() || !walnut) md += ", " + plural(bytes, "byte", "bytes");
        if (!unit_name.empty()) md += ", " + std::to_string(units) + " " + unit_name;
        if (!walnut && suffix.empty()) md += " (plus the ending `\\0`)";
        md += "\n";
    }
    if (!suffix.empty() && walnut == false && type.rfind("std::", 0) != 0) md += "**Suffix:** " + code(suffix) + " (a user-defined literal)\n";
    if (unclosed) md += "It's never closed with `" + std::string(1, quote) + "`.\n";
    md += escapes_text(d);
    md += "---\n" + content_block(d.points);
    result.markdown = md;
    return result;
}

} // namespace canopy
