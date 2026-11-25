#include <canopy/code_intel.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace canopy {

namespace {

struct FloatShape {
    int mantissa;
    int exponent;
    const char* max;
    const char* min;
    const char* epsilon;
    const char* subnormal;
    const char* exact_int;
};

const FloatShape k_shapes[] = {
    { 23, 8, "3.402823466385288598117042e38", "1.175494350822287507968737e-38", "1.192092895507812500000000e-7", "1.401298464324817070923730e-45", "1.677721600000000000000000e7" },
    { 52, 11, "1.797693134862315708145274e308", "2.225073858507201383090233e-308", "2.220446049250313080847263e-16", "4.940656458412465441765688e-324", "9.007199254740992000000000e15" },
    { 24, 8, "3.402823567797336616375394e38", "1.175494350822287507968737e-38", "5.960464477539062500000000e-8", "7.006492321624085354618648e-46", "3.355443200000000000000000e7" },
    { 53, 11, "1.797693134862315807937290e308", "2.225073858507201383090233e-308", "1.110223024625156540423632e-16", "2.470328229206232720882844e-324", "1.801439850948198400000000e16" },
    { 112, 16, "1.415461031044954789001553e9864", "2.825934386231054577110961e-9864", "1.925929944387235853055978e-34", "5.442551655315952425488660e-9898", "1.038459371706965525706099e34" },
    { 238, 18, "4.014132182036063039166061e39456", "9.964793929558904407357803e-39457", "2.263919769706678091877280e-72", "2.255949397818149869788955e-39528", "8.834235323891921647916488e71" },
    { 492, 20, "2.596370567831000776126596e157826", "1.540612133552872047061498e-157826", "7.820637090558987986053067e-149", "1.204856839382880828109701e-157974", "2.557336412418860835947804e148" },
    { 1002, 22, "4.544297019161366309996160e631305", "8.802241541725161332971599e-631306", "2.333159046258047197475224e-302", "2.053702948042444037117428e-631607", "8.572068857490138567587400e301" },
    { 2024, 24, "4.264487423559527872432729e2525222", "9.379790822929048089018710e-2525223", "5.191451201568375036463854e-610", "4.869472633815502399052856e-2525832", "3.852487334169269478406295e609" },
};

struct NumberType {
    bool is_float = false;
    bool is_unsigned = false;
    bool hardware = false;
    int bits = 0;
    std::string spelling;
    std::size_t begin = 0;
    std::size_t end = 0;
};

bool word_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

bool number_word(const std::string& w) {
    static const char* const words[] = { "unsigned", "short", "long", "int", "integer", "float", "dec", "double", "decimal", "floater" };
    for (const char* k : words) if (w == k) return true;
    return false;
}

std::vector<std::pair<std::size_t, std::size_t>> words_of(const std::string& line) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (!word_char(line[i])) {
            ++i;
            continue;
        }
        const std::size_t b = i;
        while (i < line.size() && word_char(line[i])) ++i;
        out.emplace_back(b, i);
    }
    return out;
}

bool in_code(const std::string& line, std::size_t column) {
    char quote = 0;
    for (std::size_t i = 0; i < line.size() && i < column; ++i) {
        const char c = line[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') quote = c;
        else if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') return false;
    }
    return quote == 0;
}

std::optional<NumberType> hardware_type(const std::string& line, std::size_t b, std::size_t e) {
    const std::string w = line.substr(b, e - b);
    const std::string prefix = "hardware_";
    if (w.rfind(prefix, 0) != 0) return std::nullopt;
    const std::string rest = w.substr(prefix.size());
    NumberType t;
    t.hardware = true;
    if (rest == "float") {
        t.is_float = true;
        t.bits = 32;
    } else if (rest == "double") {
        t.is_float = true;
        t.bits = 64;
    } else {
        std::string digits = rest;
        if (digits.rfind("uint", 0) == 0) {
            t.is_unsigned = true;
            digits = digits.substr(4);
        } else if (digits.rfind("int", 0) == 0) {
            digits = digits.substr(3);
        } else {
            return std::nullopt;
        }
        if (digits.empty()) t.bits = 32;
        else if (digits == "8" || digits == "16" || digits == "32" || digits == "64") t.bits = std::stoi(digits);
        else return std::nullopt;
    }
    std::size_t begin = b;
    if (begin >= 5 && line.compare(begin - 5, 5, "std::") == 0) begin -= 5;
    t.begin = begin;
    t.end = e;
    t.spelling = line.substr(begin, e - begin);
    return t;
}

std::optional<NumberType> keyword_type(const std::string& line, const std::vector<std::pair<std::size_t, std::size_t>>& words, std::size_t at) {
    std::size_t first = at;
    std::size_t last = at;
    auto gap_ok = [&](std::size_t from, std::size_t to) {
        for (std::size_t i = from; i < to; ++i) if (line[i] != ' ' && line[i] != '\t') return false;
        return true;
    };
    while (first > 0 && number_word(line.substr(words[first - 1].first, words[first - 1].second - words[first - 1].first)) && gap_ok(words[first - 1].second, words[first].first)) --first;
    while (last + 1 < words.size() && number_word(line.substr(words[last + 1].first, words[last + 1].second - words[last + 1].first)) && gap_ok(words[last].second, words[last + 1].first)) ++last;
    int steps = 0;
    int shorts = 0;
    int longs = 0;
    bool is_unsigned = false;
    std::string base;
    std::size_t used_last = first;
    for (std::size_t k = first; k <= last; ++k) {
        const std::string w = line.substr(words[k].first, words[k].second - words[k].first);
        if (!base.empty()) break;
        if (w == "unsigned") {
            if (is_unsigned || steps != 0) break;
            is_unsigned = true;
        } else if (w == "short") {
            --steps;
            ++shorts;
        } else if (w == "long") {
            ++steps;
            ++longs;
        } else {
            base = w;
        }
        used_last = k;
    }
    if (at > used_last || shorts > 2 || longs > 2 || (shorts && longs)) return std::nullopt;
    NumberType t;
    t.is_unsigned = is_unsigned;
    t.is_float = base == "float" || base == "dec" || base == "double" || base == "decimal" || base == "floater";
    const bool wide = base == "integer" || base == "double" || base == "decimal" || base == "floater";
    double bits = t.is_float ? (wide ? 512.0 : 128.0) : (wide ? 512.0 : 32.0);
    for (int i = 0; i < steps; ++i) bits *= 2;
    for (int i = 0; i > steps; --i) bits /= 2;
    t.bits = static_cast<int>(bits);
    if (t.is_float ? (t.bits < 32 || t.bits > 2048) : (t.bits < 8 || t.bits > 2048)) return std::nullopt;
    t.begin = words[first].first;
    t.end = words[used_last].second;
    t.spelling = line.substr(t.begin, t.end - t.begin);
    return t;
}

std::string add_one(std::string digits) {
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (digits[i] == '9') {
            digits[i] = '0';
        } else {
            ++digits[i];
            return digits;
        }
    }
    return "1" + digits;
}

std::string minus_one(std::string digits) {
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (digits[i] == '0') {
            digits[i] = '9';
        } else {
            --digits[i];
            break;
        }
    }
    const std::size_t nz = digits.find_first_not_of('0');
    return nz == std::string::npos ? "0" : digits.substr(nz);
}

std::string power_of_two(int n) {
    std::string digits = "1";
    for (int k = 0; k < n; ++k) {
        int carry = 0;
        for (std::size_t i = digits.size(); i-- > 0;) {
            const int v = (digits[i] - '0') * 2 + carry;
            digits[i] = static_cast<char>('0' + v % 10);
            carry = v / 10;
        }
        if (carry) digits.insert(digits.begin(), static_cast<char>('0' + carry));
    }
    return digits;
}

std::string grouped(const std::string& digits) {
    std::string out;
    const std::size_t n = digits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += '_';
        out += digits[i];
    }
    return out;
}

std::string approx(const std::string& digits, int significant) {
    if (digits.size() <= 1) return digits;
    std::string mantissa = digits.substr(0, static_cast<std::size_t>(std::min<int>(significant, static_cast<int>(digits.size()))));
    if (static_cast<int>(digits.size()) > significant && digits[static_cast<std::size_t>(significant)] >= '5') {
        mantissa = add_one(mantissa);
        if (mantissa.size() > static_cast<std::size_t>(significant)) return "1e" + std::to_string(digits.size());
    }
    while (mantissa.size() > 1 && mantissa.back() == '0') mantissa.pop_back();
    std::string out(1, mantissa[0]);
    if (mantissa.size() > 1) out += "." + mantissa.substr(1);
    return out + "e" + std::to_string(digits.size() - 1);
}

std::string code(const std::string& s) { return "`" + s + "`"; }

std::string int_value(const std::string& digits, bool negative, const NumberHoverOptions& o) {
    const std::string sign = negative && digits != "0" ? "-" : "";
    if (digits.size() <= 39 || o.int_all_digits) return code(sign + grouped(digits));
    return code(sign + approx(digits, 20)) + " (" + std::to_string(digits.size()) + " digits)";
}

std::string hex_text(int bits, bool all_ones, bool top_only) {
    std::string out;
    const int nibbles = bits / 4;
    for (int i = 0; i < nibbles; ++i) {
        if (top_only) out += i == 0 ? '8' : '0';
        else out += all_ones ? 'F' : (i == 0 ? '7' : 'F');
    }
    std::string grouped_hex;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (i > 0 && (out.size() - i) % 4 == 0) grouped_hex += '_';
        grouped_hex += out[i];
    }
    return "0x" + grouped_hex;
}

std::string int_markdown(const NumberType& t, const NumberHoverOptions& o) {
    std::string md;
    const std::string bytes = t.bits >= 8 ? std::to_string(t.bits / 8) : "1";
    if (o.int_size) md += "**Size:** " + std::to_string(t.bits) + " bits (" + bytes + (t.bits == 8 ? " byte" : " bytes") + ")\n";
    const std::string full = power_of_two(t.bits);
    std::string low;
    std::string high;
    bool low_negative = false;
    if (t.is_unsigned) {
        low = "0";
        high = minus_one(full);
    } else if (t.hardware) {
        const std::string half = power_of_two(t.bits - 1);
        low = half;
        low_negative = true;
        high = minus_one(half);
    } else {
        low = minus_one(full);
        low_negative = true;
        high = low;
    }
    if (o.int_range) {
        md += "**Smallest:** " + int_value(low, low_negative, o) + "\n";
        md += "**Largest:** " + int_value(high, false, o) + "\n";
    }
    if (o.int_hex) {
        if (t.is_unsigned) md += "**Largest in hex:** " + code(hex_text(t.bits, true, false)) + "\n";
        else if (t.hardware) md += "**In hex:** largest " + code(hex_text(t.bits, false, false)) + ", smallest " + code(hex_text(t.bits, false, true)) + " as bits\n";
        else md += "**Largest in hex:** " + code(hex_text(t.bits, true, false)) + " (the sign is kept apart, so negatives mirror it)\n";
    }
    if (o.int_count) {
        std::string count;
        std::string form;
        if (t.is_unsigned || t.hardware) {
            count = full;
            form = "2^" + std::to_string(t.bits);
        } else {
            count = minus_one(power_of_two(t.bits + 1));
            form = "2^" + std::to_string(t.bits + 1) + " - 1";
        }
        md += "**Number of values:** " + code(form) + " = " + int_value(count, false, o) + (t.hardware ? "" : ", plus the special values") + "\n";
    }
    if (o.int_behavior) {
        if (t.hardware) md += "**Overflow:** wraps around like C++'s " + code(std::string(t.is_unsigned ? "uint" : "int") + std::to_string(t.bits) + "_t") + " and can't hold special values.\n";
        else if (t.is_unsigned) md += "**Overflow:** too big gives " + code("inf") + ", below 0 gives " + code("0") + ". It can also hold " + code("nan") + ", " + code("undefined") + " and " + code("indeterminate") + ".\n";
        else md += "**Overflow:** gives " + code("inf") + " or " + code("-inf") + ". It can also hold " + code("nan") + ", " + code("undefined") + " and " + code("indeterminate") + ". The sign is kept apart from the bits, so the range is the same both ways.\n";
    }
    return md;
}

std::string trimmed_sci(const char* value, int significant) {
    const std::string s = value;
    const std::size_t e = s.find('e');
    std::string mantissa = s.substr(0, e);
    const std::string exponent = s.substr(e + 1);
    std::string digits;
    for (char c : mantissa) if (c != '.') digits += c;
    std::string rounded = digits.substr(0, static_cast<std::size_t>(significant));
    int exp = std::stoi(exponent);
    if (static_cast<int>(digits.size()) > significant && digits[static_cast<std::size_t>(significant)] >= '5') {
        rounded = add_one(rounded);
        if (rounded.size() > static_cast<std::size_t>(significant)) {
            rounded.pop_back();
            ++exp;
        }
    }
    while (rounded.size() > 1 && rounded.back() == '0') rounded.pop_back();
    std::string out(1, rounded[0]);
    if (rounded.size() > 1) out += "." + rounded.substr(1);
    if (exp == 0) return out;
    return out + "e" + std::to_string(exp);
}

std::string float_markdown(const NumberType& t, const NumberHoverOptions& o) {
    const int mantissa = t.hardware ? (t.bits == 32 ? 23 : 52) : 0;
    const FloatShape* shape = nullptr;
    for (const FloatShape& s : k_shapes) {
        const bool hardware_shape = (&s - k_shapes) < 2;
        if (hardware_shape != t.hardware) continue;
        const int total = s.mantissa + s.exponent + (hardware_shape ? 1 : 0);
        if (t.hardware ? s.mantissa == mantissa : total == t.bits) {
            shape = &s;
            break;
        }
    }
    if (!shape) return std::string();
    const int mb = shape->mantissa;
    const long long bias = (1LL << (shape->exponent - 1)) - 1;
    const int digits10 = static_cast<int>(static_cast<long long>(mb) * 30103 / 100000);
    const int max_digits10 = 2 + static_cast<int>(static_cast<long long>(mb + 1) * 30103 / 100000);
    const int shown = std::min(max_digits10, 21);
    auto value = [&](const char* v) { return code(trimmed_sci(v, shown)); };
    std::string md;
    if (o.float_size) md += "**Size:** " + std::to_string(t.bits) + " bits (" + std::to_string(t.bits / 8) + " bytes)" + (t.hardware ? ", IEEE 754" : "") + "\n";
    if (o.float_mantissa) md += "**Mantissa:** " + std::to_string(mb) + " bits stored, " + std::to_string(mb + 1) + " bits of precision with the hidden leading 1\n";
    if (o.float_exponent) {
        md += "**Exponent:** " + std::to_string(shape->exponent) + " bits, bias " + std::to_string(bias) + "; powers of 2 from " + code(std::to_string(1 - bias)) + " to " + code(std::to_string(bias)) + ", powers of 10 from about " + code(std::to_string((1 - bias) * 30103 / 100000)) + " to " + code(std::to_string(bias * 30103 / 100000)) + "\n";
    }
    if (o.float_digits) md += "**Decimal digits:** " + std::to_string(digits10) + " always survive a round trip (digits10); " + std::to_string(max_digits10) + " are enough to write any value exactly (max_digits10)\n";
    if (o.float_epsilon) md += "**Epsilon:** " + value(shape->epsilon) + (o.float_exact_form ? " = " + code("2^-" + std::to_string(mb)) : std::string()) + ", the gap between 1 and the next value\n";
    if (o.float_max) md += "**Largest:** " + value(shape->max) + (o.float_exact_form ? " = " + code("(2 - 2^-" + std::to_string(mb) + ") * 2^" + std::to_string(bias)) : std::string()) + "\n";
    if (o.float_lowest) {
        if (t.is_unsigned) md += "**Lowest:** " + code("0") + "; it can't hold a negative value\n";
        else md += "**Lowest:** " + code("-" + trimmed_sci(shape->max, shown)) + "\n";
    }
    if (o.float_min) md += "**Smallest normal:** " + value(shape->min) + (o.float_exact_form ? " = " + code("2^" + std::to_string(1 - bias)) : std::string()) + ", the smallest positive value with full precision\n";
    if (o.float_subnormal) md += "**Smallest subnormal:** " + value(shape->subnormal) + (o.float_exact_form ? " = " + code("2^" + std::to_string(1 - bias - mb)) : std::string()) + ", the smallest positive value at all\n";
    if (o.float_exact_int) md += "**Exact integers:** every whole number up to " + value(shape->exact_int) + (o.float_exact_form ? " = " + code("2^" + std::to_string(mb + 1)) : std::string()) + " is held exactly\n";
    if (o.float_behavior) {
        if (t.hardware) md += "**Special values:** " + code("inf") + ", " + code("-inf") + ", " + code("nan") + " and " + code("-0.0") + ", as in IEEE 754.\n";
        else if (t.is_unsigned) md += "**Special values:** " + code("inf") + ", " + code("nan") + ", " + code("undefined") + " and " + code("indeterminate") + ". A result below 0 becomes 0.\n";
        else md += "**Special values:** " + code("inf") + ", " + code("-inf") + ", " + code("nan") + ", " + code("undefined") + " and " + code("indeterminate") + ". Fizmo's layout isn't IEEE 754.\n";
    }
    return md;
}

} // namespace

std::optional<HoverResult> walnut_number_hover(const std::string& text, std::size_t line, std::size_t column, const NumberHoverOptions& options) {
    if (!options.enabled) return std::nullopt;
    const std::string l = mask_c_like_line(text, line);
    if (column >= l.size() || !in_code(l, column)) return std::nullopt;
    const auto words = words_of(l);
    std::size_t at = words.size();
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (words[i].first <= column && column < words[i].second) {
            at = i;
            break;
        }
    }
    if (at == words.size()) return std::nullopt;
    std::optional<NumberType> type = hardware_type(l, words[at].first, words[at].second);
    if (!type) {
        const std::string w = l.substr(words[at].first, words[at].second - words[at].first);
        if (!number_word(w)) return std::nullopt;
        if (words[at].first >= 2 && l.compare(words[at].first - 2, 2, "::") == 0) return std::nullopt;
        type = keyword_type(l, words, at);
    }
    if (!type) return std::nullopt;
    const std::string body = type->is_float ? float_markdown(*type, options) : int_markdown(*type, options);
    if (body.empty()) return std::nullopt;
    std::string title = (type->hardware ? std::string("Hardware ") : std::string()) + (type->is_unsigned ? "unsigned " : "") + (type->is_float ? "float" : "integer");
    title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(title[0])));
    return HoverResult{ line, type->begin, type->end - type->begin, "**" + title + "** " + code(type->spelling) + "\n" + body };
}

} // namespace canopy
