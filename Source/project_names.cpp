#include <canopy/special_names.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

enum class EscapeError { None, Bad, OutOfRange, Surrogate, Utf8 };

const char* escape_error_text(EscapeError e) {
    switch (e) {
        case EscapeError::OutOfRange: return "code point exceeds U+10FFFF";
        case EscapeError::Surrogate: return "U+D800..U+DFFF are surrogates, not characters";
        case EscapeError::Utf8: return "malformed UTF-8";
        default: return "unknown or incomplete escape";
    }
}

bool is_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

bool valid_scalar(std::uint32_t cp, EscapeError& err) {
    if (cp > 0x10FFFFu) { err = EscapeError::OutOfRange; return false; }
    if (cp >= 0xD800u && cp <= 0xDFFFu) { err = EscapeError::Surrogate; return false; }
    return true;
}

void utf8_encode(std::uint32_t cp, std::string& out) {
    if (cp < 0x80u) out += static_cast<char>(cp);
    else if (cp < 0x800u) {
        out += static_cast<char>(0xC0u | (cp >> 6));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        out += static_cast<char>(0xE0u | (cp >> 12));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    } else {
        out += static_cast<char>(0xF0u | (cp >> 18));
        out += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
        out += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        out += static_cast<char>(0x80u | (cp & 0x3Fu));
    }
}

bool utf8_decode(std::string_view s, std::size_t& i, std::uint32_t& cp, EscapeError& err) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    std::size_t extra = 0;
    if (c0 < 0x80u) cp = c0;
    else if ((c0 & 0xE0u) == 0xC0u) { cp = c0 & 0x1Fu; extra = 1; }
    else if ((c0 & 0xF0u) == 0xE0u) { cp = c0 & 0x0Fu; extra = 2; }
    else if ((c0 & 0xF8u) == 0xF0u) { cp = c0 & 0x07u; extra = 3; }
    else { err = EscapeError::Utf8; return false; }
    if (i + extra >= s.size() && extra > 0) { err = EscapeError::Utf8; return false; }
    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char cn = static_cast<unsigned char>(s[i + k]);
        if ((cn & 0xC0u) != 0x80u) { err = EscapeError::Utf8; return false; }
        cp = (cp << 6) | (cn & 0x3Fu);
    }
    if (!valid_scalar(cp, err)) return false;
    i += extra + 1;
    return true;
}

bool decode_escape(std::string_view s, std::size_t& i, std::uint32_t& cp, EscapeError& err) {
    err = EscapeError::Bad;
    if (i + 1 >= s.size()) return false;
    const char e = s[i + 1];
    i += 2;
    switch (e) {
        case 'n': cp = 0x0A; return true;
        case 't': cp = 0x09; return true;
        case 'r': cp = 0x0D; return true;
        case 'a': cp = 0x07; return true;
        case 'b': cp = 0x08; return true;
        case 'f': cp = 0x0C; return true;
        case 'v': cp = 0x0B; return true;
        case 'e': cp = 0x1B; return true;
        case '0': cp = 0x00; return true;
        case '\\': case '\'': case '"': case '?': case '`': case '$': cp = static_cast<unsigned char>(e); return true;
        case 'x': {
            if (i >= s.size() || !is_hex(s[i])) return false;
            std::uint32_t v = 0;
            while (i < s.size() && is_hex(s[i])) {
                v = v * 16u + static_cast<std::uint32_t>(hex_val(s[i]));
                if (v > 0x10FFFFu) { err = EscapeError::OutOfRange; return false; }
                ++i;
            }
            cp = v;
            return valid_scalar(cp, err);
        }
        case 'u':
            if (i < s.size() && s[i] == '{') {
                ++i;
                std::uint32_t v = 0;
                std::size_t digits = 0;
                while (i < s.size() && is_hex(s[i])) {
                    v = v * 16u + static_cast<std::uint32_t>(hex_val(s[i]));
                    if (v > 0x10FFFFu) { err = EscapeError::OutOfRange; return false; }
                    ++i;
                    ++digits;
                }
                if (digits == 0 || i >= s.size() || s[i] != '}') return false;
                ++i;
                cp = v;
                return valid_scalar(cp, err);
            }
            [[fallthrough]];
        case 'U': {
            const std::size_t want = e == 'u' ? 4 : 8;
            if (i + want > s.size()) return false;
            std::uint32_t v = 0;
            for (std::size_t k = 0; k < want; ++k) {
                if (!is_hex(s[i + k])) return false;
                v = v * 16u + static_cast<std::uint32_t>(hex_val(s[i + k]));
            }
            i += want;
            cp = v;
            return valid_scalar(cp, err);
        }
        case 's': {
            if (i >= s.size() || s[i] != '{') return false;
            const std::size_t open = ++i;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
            if (i == open || i >= s.size() || s[i] != '}') return false;
            const std::string_view name = s.substr(open, i - open);
            ++i;
            return builtin_special_character(name, cp);
        }
        default:
            return false;
    }
}

bool decode_literal(std::string_view text, std::string& out, EscapeError& err) {
    out.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        std::uint32_t cp = 0;
        if (text[i] == '\\') { if (!decode_escape(text, i, cp, err)) return false; }
        else if (!utf8_decode(text, i, cp, err)) return false;
        utf8_encode(cp, out);
    }
    return true;
}


std::vector<ProjectName>& active() {
    static std::vector<ProjectName> names;
    return names;
}

bool name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool valid_name(std::string_view name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), name_char);
}

std::string show_code(std::uint32_t code) {
    static const char* hex = "0123456789ABCDEF";
    std::string digits;
    for (std::uint32_t v = code; v != 0 || digits.size() < 4; v >>= 4) digits.insert(digits.begin(), hex[v & 15]);
    std::string glyph;
    if (code > 0x20 && code != 0x7F && !(code >= 0x80 && code < 0xA0)) utf8_encode(code, glyph);
    return glyph.empty() ? "U+" + digits : glyph + " (U+" + digits + ")";
}

class Reader {
public:
    Reader(std::string_view text) : m_text(text) {}

    std::size_t line() const { return m_line; }
    std::size_t offset() { skip(); return m_at; }
    std::size_t position() const { return m_at; }
    bool done() { skip(); return m_at >= m_text.size(); }
    char peek() { skip(); return m_at < m_text.size() ? m_text[m_at] : '\0'; }
    void take() { if (m_at < m_text.size()) ++m_at; }

    std::string word() {
        skip();
        const std::size_t start = m_at;
        while (m_at < m_text.size() && (name_char(m_text[m_at]) || m_text[m_at] == '-' || m_text[m_at] == '.' || m_text[m_at] == '+')) ++m_at;
        return std::string(m_text.substr(start, m_at - start));
    }

    bool quoted(std::string& raw) {
        skip();
        if (m_at >= m_text.size() || (m_text[m_at] != '"' && m_text[m_at] != '\'')) return false;
        const char quote = m_text[m_at++];
        const std::size_t start = m_at;
        while (m_at < m_text.size() && m_text[m_at] != quote && m_text[m_at] != '\n') {
            if (m_text[m_at] == '\\' && m_at + 1 < m_text.size()) ++m_at;
            ++m_at;
        }
        raw = std::string(m_text.substr(start, m_at - start));
        if (m_at < m_text.size() && m_text[m_at] == quote) ++m_at;
        return true;
    }

    void skip_to(char stop) {
        while (m_at < m_text.size() && m_text[m_at] != stop && m_text[m_at] != '}') {
            if (m_text[m_at] == '\n') ++m_line;
            ++m_at;
        }
        if (m_at < m_text.size() && m_text[m_at] == stop) ++m_at;
    }

    void skip_block() {
        int depth = 0;
        while (!done()) {
            const char c = m_text[m_at];
            if (c == '"' || c == '\'') { std::string ignored; quoted(ignored); continue; }
            ++m_at;
            if (c == '{') ++depth;
            if (c == '}' && --depth <= 0) return;
        }
    }

private:
    void skip() {
        while (m_at < m_text.size()) {
            const char c = m_text[m_at];
            if (c == '\n') { ++m_line; ++m_at; continue; }
            if (c == ' ' || c == '\t' || c == '\r') { ++m_at; continue; }
            if (c == '/' && m_at + 1 < m_text.size() && m_text[m_at + 1] == '/') {
                while (m_at < m_text.size() && m_text[m_at] != '\n') ++m_at;
                continue;
            }
            if (c == '/' && m_at + 1 < m_text.size() && m_text[m_at + 1] == '*') {
                m_at += 2;
                while (m_at + 1 < m_text.size() && !(m_text[m_at] == '*' && m_text[m_at + 1] == '/')) {
                    if (m_text[m_at] == '\n') ++m_line;
                    ++m_at;
                }
                m_at = std::min(m_text.size(), m_at + 2);
                continue;
            }
            break;
        }
    }

    std::string_view m_text;
    std::size_t m_at = 0;
    std::size_t m_line = 1;
};

struct Pending {
    std::string name;
    std::string value;
    bool quoted = false;
    std::size_t line = 0;
};

bool parse_number(const std::string& text, std::uint32_t& code) {
    std::string digits;
    if (text.size() > 2 && (text[0] == 'U' || text[0] == 'u') && text[1] == '+') digits = text.substr(2);
    else if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) digits = text.substr(2);
    else return false;
    if (digits.empty() || digits.size() > 6) return false;
    std::uint32_t v = 0;
    for (char c : digits) {
        if (!is_hex(c)) return false;
        v = v * 16 + static_cast<std::uint32_t>(hex_val(c));
    }
    code = v;
    return true;
}

} // namespace

const char* project_file_name() {
    return "walnut.bark";
}

std::string find_project_file(const std::string& from) {
    if (from.empty()) return std::string();
    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(from), ec);
    if (ec) return std::string();
    if (!fs::is_directory(dir, ec)) dir = dir.parent_path();
    while (!dir.empty()) {
        const fs::path candidate = dir / project_file_name();
        if (fs::is_regular_file(candidate, ec)) return candidate.string();
        const fs::path up = dir.parent_path();
        if (up == dir) break;
        dir = up;
    }
    return std::string();
}

ProjectNames parse_project_names(std::string_view text, const std::string& path) {
    ProjectNames out;
    out.path = path;
    Reader in(text);
    std::vector<Pending> pending;
    while (!in.done()) {
        const std::size_t at = in.line();
        const std::string section = in.word();
        if (section.empty()) {
            out.problems.push_back({ at, std::string("expected a section like 'names { ... }'") });
            in.skip_to('\n');
            continue;
        }
        if (in.peek() != '{') {
            out.problems.push_back({ at, "expected '{' after '" + section + "'" });
            in.skip_to(';');
            continue;
        }
        if (section != "names") {
            in.skip_block();
            continue;
        }
        in.take();
        while (!in.done() && in.peek() != '}') {
            Pending p;
            p.line = in.line();
            p.name = in.word();
            if (p.name.empty()) {
                out.problems.push_back({ p.line, "expected a name, as in 'deg: \"\\u{B0}\";'" });
                in.skip_to(';');
                continue;
            }
            if (in.peek() != ':') {
                out.problems.push_back({ p.line, "expected ':' after '" + p.name + "'" });
                in.skip_to(';');
                continue;
            }
            in.take();
            p.quoted = in.quoted(p.value);
            if (!p.quoted) p.value = in.word();
            if (in.peek() == ';') in.take();
            else out.problems.push_back({ p.line, "expected ';' after the value of '" + p.name + "'" });
            pending.push_back(std::move(p));
        }
        if (in.peek() == '}') in.take();
        else out.problems.push_back({ in.line(), std::string("'names' needs a closing '}'") });
    }

    std::map<std::string, const Pending*> by_name;
    for (const Pending& p : pending) by_name.emplace(p.name, &p);

    std::function<int(const Pending&, std::vector<std::string>&, std::uint32_t&, std::string&)> resolve =
        [&](const Pending& p, std::vector<std::string>& chain, std::uint32_t& code, std::string& why) -> int {
        if (p.quoted) {
            std::string decoded;
            EscapeError err = EscapeError::None;
            if (!decode_literal(p.value, decoded, err)) {
                why = "the value of '" + p.name + "' has a bad escape: " + escape_error_text(err);
                return 0;
            }
            std::size_t i = 0;
            if (decoded.empty() || !utf8_decode(decoded, i, code, err) || i != decoded.size()) {
                why = "the value of '" + p.name + "' must be exactly one character, like \"\\u{B0}\" or \"\xC2\xB0\"";
                return 0;
            }
            return 1;
        }
        if (parse_number(p.value, code)) {
            EscapeError err = EscapeError::None;
            if (valid_scalar(code, err)) return 1;
            why = "the value of '" + p.name + "' isn't a Unicode character: " + escape_error_text(err);
            return 0;
        }
        if (builtin_special_character(p.value, code)) return 1;
        const auto other = by_name.find(p.value);
        if (other != by_name.end()) {
            if (std::find(chain.begin(), chain.end(), p.value) != chain.end()) {
                why = "'" + chain.front() + "' leads back to itself (";
                for (const std::string& step : chain) why += step + " \xE2\x86\x92 ";
                why += p.value + "), so it never reaches a character";
                return 0;
            }
            chain.push_back(p.value);
            return resolve(*other->second, chain, code, why);
        }
        why = "'" + p.value + "' isn't a character name";
        const std::string guess = special_character_suggestion(p.value);
        if (!guess.empty()) why += "; did you mean '" + guess + "'?";
        else why += "; give a built-in name like alpha, a character in quotes like \"\xC2\xB0\", or a code like U+00B0";
        return 0;
    };

    std::map<std::string, std::size_t> seen;
    for (const Pending& p : pending) {
        if (!valid_name(p.name)) {
            out.problems.push_back({ p.line, "'" + p.name + "' can't be used in \\s{...}; names use only letters, digits and _" });
            continue;
        }
        std::uint32_t builtin = 0;
        if (builtin_special_character(p.name, builtin)) {
            out.problems.push_back({ p.line, "'" + p.name + "' is already a built-in name for " + show_code(builtin) + "; your names are added alongside the built-in ones, so pick another" });
            continue;
        }
        const auto again = seen.find(p.name);
        if (again != seen.end()) {
            out.problems.push_back({ p.line, "'" + p.name + "' is given more than once" });
            continue;
        }
        seen[p.name] = p.line;
        std::vector<std::string> chain{ p.name };
        std::uint32_t code = 0;
        std::string why;
        if (!resolve(p, chain, code, why)) {
            out.problems.push_back({ p.line, why });
            continue;
        }
        out.names.push_back({ p.name, code, p.line });
    }
    std::map<std::string, std::vector<std::size_t>> lines_of;
    for (const Pending& p : pending) {
        if (valid_name(p.name)) lines_of[p.name].push_back(p.line);
    }
    for (const auto& [name, lines] : lines_of) {
        if (lines.size() < 2) continue;
        std::string message = "'" + name + "' is given " + std::to_string(lines.size()) + " times, on lines ";
        for (std::size_t i = 0; i < lines.size(); ++i) message += (i == 0 ? "" : i + 1 == lines.size() ? " and " : ", ") + std::to_string(lines[i]);
        message += "; Walnut uses the first, so remove the others";
        out.problems.erase(std::remove_if(out.problems.begin(), out.problems.end(), [&](const ProjectNameProblem& q) { return q.message == "'" + name + "' is given more than once"; }), out.problems.end());
        for (std::size_t line : lines) out.problems.push_back({ line, message });
    }
    std::sort(out.problems.begin(), out.problems.end(), [](const ProjectNameProblem& a, const ProjectNameProblem& b) { return a.line < b.line; });
    for (const Pending& p : pending) {
        ProjectNameEntry entry;
        entry.name = p.name;
        entry.value = p.value;
        entry.quoted = p.quoted;
        entry.line = p.line;
        for (const ProjectName& n : out.names) {
            if (n.line == p.line && n.name == p.name) {
                entry.ok = true;
                entry.code = n.code;
            }
        }
        {
            for (const ProjectNameProblem& problem : out.problems) {
                if (problem.line == p.line) {
                    entry.problem = problem.message;
                    break;
                }
            }
        }
        out.entries.push_back(std::move(entry));
    }
    return out;
}

ProjectNames read_project_names(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ProjectNames out;
        out.path = path;
        out.problems.push_back({ 0, std::string("can't be read") });
        return out;
    }
    std::ostringstream all;
    all << in.rdbuf();
    return parse_project_names(all.str(), path);
}

void use_project_names(const ProjectNames& names) {
    std::vector<ProjectName> sorted = names.names;
    std::sort(sorted.begin(), sorted.end(), [](const ProjectName& a, const ProjectName& b) { return a.name < b.name; });
    active() = std::move(sorted);
}

void clear_project_names() {
    active().clear();
}

const std::vector<ProjectName>& project_names() {
    return active();
}

bool special_character(std::string_view name, std::uint32_t& code) {
    if (builtin_special_character(name, code)) return true;
    const std::vector<ProjectName>& names = active();
    const auto it = std::lower_bound(names.begin(), names.end(), name, [](const ProjectName& entry, std::string_view key) { return entry.name < key; });
    if (it == names.end() || it->name != name) return false;
    code = it->code;
    return true;
}

std::vector<std::string_view> special_character_names(std::string_view prefix) {
    std::vector<std::string_view> found = builtin_special_character_names(prefix);
    for (const ProjectName& entry : active()) {
        if (std::string_view(entry.name).substr(0, prefix.size()) == prefix) found.push_back(entry.name);
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::string special_character_suggestion(std::string_view name) {
    if (name.empty()) return std::string();
    std::vector<std::string_view> all = special_character_names(std::string_view());
    std::string_view best;
    std::size_t best_score = (std::max<std::size_t>(2, name.size() / 3) * 2 + 1) * 2;
    bool best_same_letters = false;
    for (const std::string_view entry : all) {
        bool same_letters = entry.size() == name.size();
        for (std::size_t k = 0; same_letters && k < name.size(); ++k) same_letters = special_names_alike(entry[k], name[k]);
        if (best_same_letters && !same_letters) continue;
        const std::size_t larger = std::max(entry.size(), name.size());
        const std::size_t smaller = std::min(entry.size(), name.size());
        if (!same_letters && (larger - smaller) * 4 >= best_score) continue;
        const std::size_t first = special_names_alike(entry[0], name[0]) ? 0 : 1;
        const std::size_t score = special_name_distance(name, entry) * 2 + first;
        if ((same_letters && !best_same_letters) || score < best_score) {
            best = entry;
            best_score = score;
            best_same_letters = same_letters;
        }
    }
    return std::string(best);
}

std::string special_character_text(std::uint32_t code) {
    std::string out;
    if (code > 0x20 && code != 0x7F && !(code >= 0x80 && code < 0xA0)) utf8_encode(code, out);
    return out;
}

const std::vector<std::string>& special_character_categories() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out = {
            "Greek", "Arrows", "Math operators and relations", "Math letters and number sets", "Fractions, superscripts and subscripts",
            "Currency", "Punctuation and spaces", "Shapes, boxes and symbols", "Latin letters", "Cyrillic", "Control and invisible characters",
        };
        for (const std::string_view name : builtin_special_character_names(std::string_view())) {
            std::uint32_t code = 0;
            if (!builtin_special_character(name, code)) continue;
            const char* listed = builtin_special_category(code);
            if (listed && std::find(out.begin(), out.end(), listed) == out.end()) out.push_back(listed);
        }
        out.push_back("Other");
        return out;
    }();
    return names;
}

std::string special_character_category(std::uint32_t c) {
    if (const char* listed = builtin_special_category(c)) return listed;
    const auto in = [c](std::uint32_t a, std::uint32_t b) { return c >= a && c <= b; };
    if (c < 0x20 || c == 0x7F || c == 0xAD || in(0x200B, 0x200F) || in(0x2060, 0x2064) || c == 0xFEFF) return "Control and invisible characters";
    if (in(0x370, 0x3FF) || in(0x1F00, 0x1FFF)) return "Greek";
    if (in(0x2190, 0x21FF) || in(0x27F0, 0x27FF) || in(0x2900, 0x297F) || in(0x2B00, 0x2B2F) || in(0x2B45, 0x2B4F)) return "Arrows";
    if (in(0x2200, 0x22FF) || in(0x2A00, 0x2AFF) || in(0x27C0, 0x27EF) || in(0x2980, 0x29FF) || c == 0xB1 || c == 0xD7 || c == 0xF7 || c == 0xAC || in(0x2032, 0x2037) || c == 0x2057) return "Math operators and relations";
    if (in(0x2100, 0x214F) || in(0x1D400, 0x1D7FF)) return "Math letters and number sets";
    if (in(0x2070, 0x209F) || c == 0xB2 || c == 0xB3 || c == 0xB9 || in(0xBC, 0xBE) || in(0x2150, 0x218F)) return "Fractions, superscripts and subscripts";
    if (in(0x20A0, 0x20CF) || in(0xA2, 0xA5)) return "Currency";
    if (in(0x2000, 0x206F) || in(0xA0, 0xBF) || (c < 0x80) || in(0x3000, 0x303F) || in(0x2E00, 0x2E7F)) return "Punctuation and spaces";
    if (in(0x2300, 0x23FF) || in(0x2500, 0x27BF) || in(0x2B00, 0x2BFF) || in(0x1F300, 0x1FAFF)) return "Shapes, boxes and symbols";
    if (in(0xC0, 0x24F) || in(0x1E00, 0x1EFF) || in(0x2C60, 0x2C7F) || in(0xA720, 0xA7FF) || in(0xFB00, 0xFB06)) return "Latin letters";
    if (in(0x400, 0x4FF)) return "Cyrillic";
    return "Other";
}

namespace {

const std::map<std::uint32_t, std::vector<std::string_view>>& builtin_by_code() {
    static const std::map<std::uint32_t, std::vector<std::string_view>> table = [] {
        std::map<std::uint32_t, std::vector<std::string_view>> out;
        for (const std::string_view name : builtin_special_character_names(std::string_view())) {
            std::uint32_t code = 0;
            if (builtin_special_character(name, code)) out[code].push_back(name);
        }
        for (auto& [code, names] : out) {
            std::stable_sort(names.begin(), names.end(), [](std::string_view a, std::string_view b) { return a.size() < b.size() || (a.size() == b.size() && a < b); });
        }
        return out;
    }();
    return table;
}

} // namespace

std::vector<std::uint32_t> builtin_special_codes() {
    std::vector<std::uint32_t> out;
    for (const auto& [code, names] : builtin_by_code()) out.push_back(code);
    return out;
}

std::vector<std::string_view> builtin_names_for(std::uint32_t code) {
    const auto& table = builtin_by_code();
    const auto it = table.find(code);
    return it == table.end() ? std::vector<std::string_view>{} : it->second;
}

std::string project_name_value_text(const std::string& typed) {
    std::string value = typed;
    const std::size_t a = value.find_first_not_of(" \t");
    const std::size_t b = value.find_last_not_of(" \t");
    value = a == std::string::npos ? std::string() : value.substr(a, b - a + 1);
    if (value.empty()) return "\"\"";
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) return value;
    std::uint32_t code = 0;
    if (parse_number(value, code)) return value;
    if (valid_name(value) && value.size() > 1) return value;
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string project_names_block(const std::vector<ProjectNameEntry>& entries) {
    std::string out = "names {\n";
    for (const ProjectNameEntry& e : entries) {
        if (e.name.empty()) continue;
        out += "    " + e.name + ": " + (e.quoted ? "\"" + e.value + "\"" : e.value) + ";\n";
    }
    return out + "}\n";
}

std::string replace_project_names(const std::string& file_text, const std::vector<ProjectNameEntry>& entries) {
    Reader in(file_text);
    while (!in.done()) {
        const std::size_t start = in.offset();
        const std::string section = in.word();
        if (section.empty()) {
            in.skip_to('\n');
            continue;
        }
        if (in.peek() != '{') {
            in.skip_to(';');
            continue;
        }
        in.skip_block();
        if (section != "names") continue;
        std::size_t end = in.position();
        if (end < file_text.size() && file_text[end] == '\r') ++end;
        if (end < file_text.size() && file_text[end] == '\n') ++end;
        return file_text.substr(0, start) + project_names_block(entries) + file_text.substr(end);
    }
    std::string out = file_text;
    if (!out.empty() && out.back() != '\n') out += '\n';
    if (!out.empty()) out += '\n';
    return out + project_names_block(entries);
}

bool write_project_names(const std::string& path, const std::vector<ProjectNameEntry>& entries) {
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream all;
            all << in.rdbuf();
            text = all.str();
        } else {
            text = "// Your own names for \\s{...} in Walnut literals, added alongside the built-in ones.\n"
                   "// A value is a built-in name (alpha), a character in quotes (\"\xC2\xB0\" or \"\\u{B0}\") or a code (U+00B0).\n\n";
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << replace_project_names(text, entries);
    return static_cast<bool>(out);
}

} // namespace canopy
