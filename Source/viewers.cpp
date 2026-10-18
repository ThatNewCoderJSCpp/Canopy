#include <canopy/viewers.hpp>

#include <canopy/toolchain.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string extension_of(const std::string& path) {
    return lower(fs::path(path).extension().string());
}

bool has_extension(const std::string& path, std::initializer_list<const char*> extensions) {
    const std::string ext = extension_of(path);
    for (const char* e : extensions) {
        if (ext == e) return true;
    }
    return false;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::uint64_t read_uint(std::string_view bytes, std::uint64_t offset, unsigned size, bool little) {
    if (offset > bytes.size() || bytes.size() - offset < size) return 0;
    std::uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i) {
        const std::uint64_t b = static_cast<unsigned char>(bytes[static_cast<std::size_t>(offset) + (little ? size - 1 - i : i)]);
        value = (value << 8) | b;
    }
    return value;
}

std::uint64_t u8(std::string_view b, std::uint64_t o) { return read_uint(b, o, 1, true); }
std::uint64_t le16(std::string_view b, std::uint64_t o) { return read_uint(b, o, 2, true); }
std::uint64_t le32(std::string_view b, std::uint64_t o) { return read_uint(b, o, 4, true); }
std::uint64_t le64(std::string_view b, std::uint64_t o) { return read_uint(b, o, 8, true); }
std::uint64_t be16(std::string_view b, std::uint64_t o) { return read_uint(b, o, 2, false); }
std::uint64_t be32(std::string_view b, std::uint64_t o) { return read_uint(b, o, 4, false); }

bool fits(std::string_view bytes, std::uint64_t offset, std::uint64_t size) {
    return offset <= bytes.size() && bytes.size() - offset >= size;
}

std::string c_string(std::string_view bytes, std::uint64_t offset, std::size_t limit = 512) {
    std::string out;
    if (offset >= bytes.size()) return out;
    for (std::size_t i = static_cast<std::size_t>(offset); i < bytes.size() && out.size() < limit; ++i) {
        if (bytes[i] == '\0') break;
        out += bytes[i];
    }
    return out;
}

std::string fixed_string(std::string_view bytes, std::uint64_t offset, std::size_t size) {
    std::string out;
    for (std::size_t i = 0; i < size && fits(bytes, offset + i, 1); ++i) {
        const char c = bytes[static_cast<std::size_t>(offset) + i];
        if (c == '\0') break;
        out += c;
    }
    return out;
}

std::string hex(std::uint64_t value, int digits = 0) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%0*llx", digits, static_cast<unsigned long long>(value));
    return std::string("0x") + buffer;
}

std::string with_commas(std::uint64_t value) {
    std::string digits = std::to_string(value);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

std::string utc_date(std::uint64_t seconds) {
    std::int64_t days = static_cast<std::int64_t>(seconds / 86400);
    const std::uint64_t rest = seconds % 86400;
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const std::int64_t doe = days - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t year = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
    if (month <= 2) ++year;
    char buffer[128];
    std::snprintf(buffer, sizeof buffer, "%04lld-%02lld-%02lld %02llu:%02llu UTC", static_cast<long long>(year), static_cast<long long>(month), static_cast<long long>(day),
                  static_cast<unsigned long long>(rest / 3600), static_cast<unsigned long long>(rest % 3600 / 60));
    return buffer;
}

std::string size_fact(std::uint64_t size) {
    if (size < 1024) return human_size(size);
    return human_size(size) + " (" + with_commas(size) + " bytes)";
}

const char* pe_machine(std::uint64_t machine) {
    switch (machine) {
    case 0x14c: return "x86";
    case 0x8664: return "x86-64";
    case 0xaa64: return "ARM64";
    case 0xa641: return "ARM64EC";
    case 0x1c0: return "ARM";
    case 0x1c4: return "ARMv7 (Thumb-2)";
    case 0x200: return "Itanium";
    case 0x5064: return "RISC-V 64";
    case 0xebc: return "EFI byte code";
    }
    return nullptr;
}

std::string pe_subsystem(std::uint64_t subsystem) {
    switch (subsystem) {
    case 1: return "Native (driver)";
    case 2: return "Windows GUI";
    case 3: return "Console";
    case 7: return "POSIX console";
    case 9: return "Windows CE GUI";
    case 10: return "EFI application";
    case 11: return "EFI boot service driver";
    case 12: return "EFI runtime driver";
    case 13: return "EFI ROM";
    case 14: return "Xbox";
    case 16: return "Windows boot application";
    }
    return "Unknown (" + std::to_string(subsystem) + ")";
}

bool describe_pe(std::string_view b, const std::string& path, BinaryInfo& info) {
    if (!starts_with(b, "MZ")) return false;
    const std::uint64_t pe = le32(b, 0x3C);
    if (!fits(b, pe, 24) || b.substr(static_cast<std::size_t>(pe), 4) != std::string_view("PE\0\0", 4)) {
        info.kind = "DOS executable";
        info.summary = "DOS (MZ) executable";
        info.facts.push_back({ "Format", "MS-DOS MZ executable" });
        return true;
    }
    const std::uint64_t coff = pe + 4;
    const std::uint64_t machine = le16(b, coff);
    const std::uint64_t section_count = le16(b, coff + 2);
    const std::uint64_t timestamp = le32(b, coff + 4);
    const std::uint64_t optional_size = le16(b, coff + 16);
    const std::uint64_t characteristics = le16(b, coff + 18);
    const std::uint64_t opt = coff + 20;
    const std::uint64_t magic = le16(b, opt);
    const bool plus = magic == 0x20b;
    const bool dll = (characteristics & 0x2000) != 0;
    const std::uint64_t subsystem = optional_size ? le16(b, opt + 68) : 0;
    const std::uint64_t dll_flags = optional_size ? le16(b, opt + 70) : 0;
    const std::uint64_t dir_count = optional_size ? le32(b, opt + (plus ? 108 : 92)) : 0;
    const std::uint64_t dirs = opt + (plus ? 112 : 96);
    auto directory = [&](std::uint64_t index) -> std::pair<std::uint64_t, std::uint64_t> {
        if (index >= dir_count) return { 0, 0 };
        return { le32(b, dirs + index * 8), le32(b, dirs + index * 8 + 4) };
    };

    const char* arch = pe_machine(machine);
    const std::string arch_name = arch ? arch : hex(machine, 4);
    const bool efi = subsystem >= 10 && subsystem <= 13;
    const bool managed = directory(14).first != 0;
    info.kind = dll ? "Windows DLL" : efi ? "EFI application" : "Windows executable";
    if (has_extension(path, { ".sys" })) info.kind = "Windows driver";
    std::string format = plus ? "PE32+ (64-bit)" : magic == 0x10b ? "PE32 (32-bit)" : "PE";
    info.summary = info.kind + "  \xC2\xB7  " + (plus ? "PE32+" : "PE32") + "  \xC2\xB7  " + arch_name;
    if (subsystem) info.summary += "  \xC2\xB7  " + pe_subsystem(subsystem);
    if (managed) info.summary += "  \xC2\xB7  .NET";
    info.facts.push_back({ "Format", format });
    info.facts.push_back({ "Machine", arch_name });
    if (subsystem) info.facts.push_back({ "Subsystem", pe_subsystem(subsystem) });
    if (optional_size) {
        info.facts.push_back({ "Entry point", hex(le32(b, opt + 16)) + " (RVA)" });
        info.facts.push_back({ "Image base", hex(plus ? le64(b, opt + 24) : le32(b, opt + 28)) });
        info.facts.push_back({ "Linker", std::to_string(u8(b, opt + 2)) + "." + std::to_string(u8(b, opt + 3)) });
    }
    if (timestamp > 946684800ull && timestamp < 4102444800ull) info.facts.push_back({ "Built", utc_date(timestamp) });
    else if (timestamp) info.facts.push_back({ "Timestamp", hex(timestamp, 8) + " (a reproducible-build hash, not a date)" });
    if (managed) info.facts.push_back({ ".NET", "Managed assembly (has a CLR header)" });
    std::vector<std::string> flags;
    if (dll) flags.push_back("DLL");
    if (characteristics & 0x20) flags.push_back("large-address-aware");
    if (dll_flags & 0x40) flags.push_back("ASLR");
    if (dll_flags & 0x20) flags.push_back("high-entropy ASLR");
    if (dll_flags & 0x100) flags.push_back("DEP/NX");
    if (dll_flags & 0x4000) flags.push_back("Control Flow Guard");
    if (characteristics & 0x0200) flags.push_back("debug info stripped");
    if (!flags.empty()) {
        std::string text;
        for (const std::string& f : flags) text += (text.empty() ? "" : ", ") + f;
        info.facts.push_back({ "Flags", text });
    }

    const std::uint64_t table = opt + optional_size;
    for (std::uint64_t i = 0; i < section_count && i < 96; ++i) {
        const std::uint64_t s = table + i * 40;
        if (!fits(b, s, 40)) break;
        BinarySection section;
        section.name = fixed_string(b, s, 8);
        const std::uint64_t virtual_size = le32(b, s + 8);
        section.address = le32(b, s + 12);
        section.size = le32(b, s + 16);
        section.offset = le32(b, s + 20);
        if (section.size == 0) section.size = virtual_size;
        const std::uint64_t c = le32(b, s + 36);
        section.flags += (c & 0x40000000) ? 'r' : '-';
        section.flags += (c & 0x80000000) ? 'w' : '-';
        section.flags += (c & 0x20000000) ? 'x' : '-';
        if (c & 0x20) section.flags += " code";
        else if (c & 0x40) section.flags += " data";
        else if (c & 0x80) section.flags += " bss";
        info.sections.push_back(section);
    }
    auto rva_offset = [&](std::uint64_t rva) -> std::optional<std::uint64_t> {
        for (std::uint64_t i = 0; i < section_count && i < 96; ++i) {
            const std::uint64_t s = table + i * 40;
            if (!fits(b, s, 40)) break;
            const std::uint64_t va = le32(b, s + 12);
            const std::uint64_t span = std::max(le32(b, s + 8), le32(b, s + 16));
            if (rva >= va && rva < va + span) return rva - va + le32(b, s + 20);
        }
        return std::nullopt;
    };
    const auto imports = directory(1);
    if (imports.first) {
        if (const auto start = rva_offset(imports.first)) {
            for (std::uint64_t d = *start; fits(b, d, 20) && info.libraries.size() < 256; d += 20) {
                const std::uint64_t name_rva = le32(b, d + 12);
                if (name_rva == 0 && le32(b, d) == 0 && le32(b, d + 16) == 0) break;
                if (const auto name = rva_offset(name_rva)) {
                    const std::string lib = c_string(b, *name, 260);
                    if (!lib.empty()) info.libraries.push_back(lib);
                }
            }
        }
    }
    info.facts.push_back({ "Sections", std::to_string(section_count) });
    return true;
}

const char* elf_machine(std::uint64_t machine) {
    switch (machine) {
    case 2: return "SPARC";
    case 3: return "x86";
    case 8: return "MIPS";
    case 20: return "PowerPC";
    case 21: return "PowerPC 64";
    case 22: return "IBM S/390";
    case 40: return "ARM";
    case 43: return "SPARC V9";
    case 62: return "x86-64";
    case 183: return "AArch64 (ARM64)";
    case 190: return "NVIDIA CUDA";
    case 243: return "RISC-V";
    case 247: return "BPF";
    case 258: return "LoongArch";
    }
    return nullptr;
}

const char* elf_abi(std::uint64_t abi) {
    switch (abi) {
    case 0: return "System V";
    case 1: return "HP-UX";
    case 2: return "NetBSD";
    case 3: return "Linux (GNU)";
    case 6: return "Solaris";
    case 9: return "FreeBSD";
    case 12: return "OpenBSD";
    case 97: return "ARM";
    case 255: return "Standalone";
    }
    return "Unknown";
}

bool describe_elf(std::string_view b, BinaryInfo& info) {
    if (!starts_with(b, "\x7F" "ELF") || b.size() < 52) return false;
    const bool is64 = u8(b, 4) == 2;
    const bool little = u8(b, 5) != 2;
    auto get = [&](std::uint64_t offset, unsigned size) { return read_uint(b, offset, size, little); };
    auto word = [&](std::uint64_t offset) { return get(offset, is64 ? 8 : 4); };
    const std::uint64_t type = get(16, 2);
    const std::uint64_t machine = get(18, 2);
    const std::uint64_t entry = word(24);
    const std::uint64_t phoff = is64 ? get(32, 8) : get(28, 4);
    const std::uint64_t shoff = is64 ? get(40, 8) : get(32, 4);
    const std::uint64_t phentsize = get(is64 ? 54 : 42, 2);
    const std::uint64_t phnum = get(is64 ? 56 : 44, 2);
    const std::uint64_t shentsize = get(is64 ? 58 : 46, 2);
    const std::uint64_t shnum = get(is64 ? 60 : 48, 2);
    const std::uint64_t shstrndx = get(is64 ? 62 : 50, 2);

    std::string interpreter;
    bool has_dynamic_segment = false;
    for (std::uint64_t i = 0; i < phnum && i < 512 && phentsize; ++i) {
        const std::uint64_t p = phoff + i * phentsize;
        if (!fits(b, p, phentsize)) break;
        const std::uint64_t ptype = get(p, 4);
        if (ptype == 2) has_dynamic_segment = true;
        if (ptype == 3) {
            const std::uint64_t off = is64 ? get(p + 8, 8) : get(p + 4, 4);
            interpreter = c_string(b, off, 256);
        }
    }

    struct Raw {
        std::uint64_t name, type, flags, addr, offset, size, link;
    };
    std::vector<Raw> raw;
    for (std::uint64_t i = 0; i < shnum && i < 4096 && shentsize; ++i) {
        const std::uint64_t s = shoff + i * shentsize;
        if (!fits(b, s, shentsize)) break;
        Raw r{};
        r.name = get(s, 4);
        r.type = get(s + 4, 4);
        r.flags = word(s + 8);
        r.addr = is64 ? get(s + 16, 8) : get(s + 12, 4);
        r.offset = is64 ? get(s + 24, 8) : get(s + 16, 4);
        r.size = is64 ? get(s + 32, 8) : get(s + 20, 4);
        r.link = is64 ? get(s + 40, 4) : get(s + 24, 4);
        raw.push_back(r);
    }
    const std::uint64_t names = shstrndx < raw.size() ? raw[static_cast<std::size_t>(shstrndx)].offset : 0;
    bool symtab = false;
    bool debug = false;
    std::string soname;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const Raw& r = raw[i];
        BinarySection section;
        section.name = names ? c_string(b, names + r.name, 128) : std::string();
        if (i == 0 && section.name.empty() && r.type == 0) continue;
        section.address = r.addr;
        section.offset = r.offset;
        section.size = r.size;
        section.flags += (r.flags & 2) ? 'r' : '-';
        section.flags += (r.flags & 1) ? 'w' : '-';
        section.flags += (r.flags & 4) ? 'x' : '-';
        if (r.type == 8) section.flags += " bss";
        if (r.type == 2) symtab = true;
        if (section.name.rfind(".debug_info", 0) == 0 || section.name == ".zdebug_info") debug = true;
        info.sections.push_back(section);
        if (r.type == 6 && r.link < raw.size()) {
            const std::uint64_t strings = raw[static_cast<std::size_t>(r.link)].offset;
            const std::uint64_t step = is64 ? 16 : 8;
            for (std::uint64_t d = r.offset; d + step <= r.offset + r.size && fits(b, d, step); d += step) {
                const std::uint64_t tag = word(d);
                const std::uint64_t value = word(d + (is64 ? 8 : 4));
                if (tag == 0) break;
                if (tag == 1 && info.libraries.size() < 256) info.libraries.push_back(c_string(b, strings + value, 256));
                if (tag == 14) soname = c_string(b, strings + value, 256);
            }
        }
    }

    std::string role;
    switch (type) {
    case 1: role = "object file"; break;
    case 2: role = "executable"; break;
    case 3: role = interpreter.empty() || !soname.empty() ? "shared library" : "PIE executable"; break;
    case 4: role = "core dump"; break;
    default: role = "file"; break;
    }
    const char* arch = elf_machine(machine);
    const std::string arch_name = arch ? arch : "machine " + std::to_string(machine);
    info.kind = "ELF " + role;
    info.summary = std::string("ELF ") + (is64 ? "64-bit" : "32-bit") + "  \xC2\xB7  " + arch_name + "  \xC2\xB7  " + role;
    if (type == 2 || type == 3) info.summary += interpreter.empty() && !has_dynamic_segment ? "  \xC2\xB7  static" : "  \xC2\xB7  dynamic";
    info.facts.push_back({ "Format", std::string(is64 ? "ELF64" : "ELF32") + ", " + (little ? "little-endian" : "big-endian") });
    info.facts.push_back({ "Type", role });
    info.facts.push_back({ "Machine", arch_name });
    info.facts.push_back({ "OS ABI", elf_abi(u8(b, 7)) });
    if (type == 2 || type == 3) info.facts.push_back({ "Entry point", hex(entry) });
    if (!interpreter.empty()) info.facts.push_back({ "Interpreter", interpreter });
    if (!soname.empty()) info.facts.push_back({ "Library name", soname });
    if (type == 2 || type == 3) info.facts.push_back({ "Linking", interpreter.empty() && !has_dynamic_segment ? "static" : "dynamic" });
    if (!raw.empty()) {
        info.facts.push_back({ "Symbols", symtab ? "present" : "stripped" });
        info.facts.push_back({ "Debug info", debug ? "present" : "none" });
    }
    info.facts.push_back({ "Sections", std::to_string(info.sections.size()) });
    return true;
}

const char* macho_cpu(std::uint64_t cpu) {
    switch (cpu) {
    case 7: return "x86";
    case 0x01000007: return "x86-64";
    case 12: return "ARM";
    case 0x0100000c: return "ARM64";
    case 0x0200000c: return "ARM64_32";
    case 18: return "PowerPC";
    case 0x01000012: return "PowerPC 64";
    }
    return nullptr;
}

std::string macho_cpu_name(std::uint64_t cpu) {
    const char* name = macho_cpu(cpu);
    return name ? name : hex(cpu);
}

bool describe_macho(std::string_view b, BinaryInfo& info) {
    const std::uint64_t magic_le = le32(b, 0);
    const std::uint64_t magic_be = be32(b, 0);
    bool little = true;
    bool is64 = false;
    if (magic_le == 0xfeedface) is64 = false;
    else if (magic_le == 0xfeedfacf) is64 = true;
    else if (magic_be == 0xfeedface) little = false;
    else if (magic_be == 0xfeedfacf) {
        little = false;
        is64 = true;
    } else {
        return false;
    }
    auto get = [&](std::uint64_t offset, unsigned size) { return read_uint(b, offset, size, little); };
    const std::uint64_t cpu = get(4, 4);
    const std::uint64_t filetype = get(12, 4);
    const std::uint64_t ncmds = get(16, 4);
    std::string role;
    switch (filetype) {
    case 1: role = "object file"; break;
    case 2: role = "executable"; break;
    case 6: role = "dynamic library"; break;
    case 7: role = "dynamic linker"; break;
    case 8: role = "bundle"; break;
    case 9: role = "dynamic library stub"; break;
    case 10: role = "debug symbols (dSYM)"; break;
    case 11: role = "kernel extension"; break;
    default: role = "file"; break;
    }
    std::uint64_t at = is64 ? 32 : 28;
    std::string install_name;
    std::optional<std::uint64_t> entry;
    for (std::uint64_t i = 0; i < ncmds && i < 4096; ++i) {
        const std::uint64_t cmd = get(at, 4);
        const std::uint64_t size = get(at + 4, 4);
        if (size < 8 || !fits(b, at, size)) break;
        if (cmd == 0x19 || cmd == 0x1) {
            BinarySection s;
            s.name = fixed_string(b, at + 8, 16);
            const bool wide = cmd == 0x19;
            s.address = wide ? get(at + 24, 8) : get(at + 24, 4);
            s.offset = wide ? get(at + 40, 8) : get(at + 32, 4);
            s.size = wide ? get(at + 48, 8) : get(at + 36, 4);
            const std::uint64_t prot = get(at + (wide ? 60 : 44), 4);
            s.flags += (prot & 1) ? 'r' : '-';
            s.flags += (prot & 2) ? 'w' : '-';
            s.flags += (prot & 4) ? 'x' : '-';
            info.sections.push_back(s);
        } else if (cmd == 0xc || cmd == 0x80000018 || cmd == 0x8000001f || cmd == 0x23) {
            const std::string name = c_string(b, at + get(at + 8, 4), 512);
            if (!name.empty() && info.libraries.size() < 256) info.libraries.push_back(name);
        } else if (cmd == 0xd) {
            install_name = c_string(b, at + get(at + 8, 4), 512);
        } else if (cmd == 0x80000028) {
            entry = get(at + 8, 8);
        }
        at += size;
    }
    info.kind = "Mach-O " + role;
    info.summary = std::string("Mach-O ") + (is64 ? "64-bit" : "32-bit") + "  \xC2\xB7  " + macho_cpu_name(cpu) + "  \xC2\xB7  " + role;
    info.facts.push_back({ "Format", std::string(is64 ? "Mach-O 64-bit" : "Mach-O 32-bit") + ", " + (little ? "little-endian" : "big-endian") });
    info.facts.push_back({ "Type", role });
    info.facts.push_back({ "CPU", macho_cpu_name(cpu) });
    if (entry) info.facts.push_back({ "Entry point", hex(*entry) + " (file offset)" });
    if (!install_name.empty()) info.facts.push_back({ "Install name", install_name });
    info.facts.push_back({ "Load commands", std::to_string(ncmds) });
    return true;
}

bool describe_cafebabe(std::string_view b, BinaryInfo& info) {
    if (be32(b, 0) != 0xcafebabe) return false;
    const std::uint64_t count = be32(b, 4);
    if (count > 0 && count < 30) {
        info.kind = "macOS universal binary";
        std::string archs;
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t a = 8 + i * 20;
            if (!fits(b, a, 20)) break;
            const std::string cpu = macho_cpu_name(be32(b, a));
            archs += (archs.empty() ? "" : ", ") + cpu;
            BinarySection s;
            s.name = cpu;
            s.offset = be32(b, a + 8);
            s.size = be32(b, a + 12);
            s.flags = "slice";
            info.sections.push_back(s);
        }
        info.summary = "macOS universal (fat) binary  \xC2\xB7  " + archs;
        info.facts.push_back({ "Format", "Mach-O universal binary" });
        info.facts.push_back({ "Architectures", archs });
        return true;
    }
    const std::uint64_t major = be16(b, 6);
    const std::uint64_t minor = be16(b, 4);
    std::string java;
    if (major >= 49) java = "Java " + std::to_string(major - 44);
    else if (major >= 45) java = "Java 1." + std::to_string(major - 44);
    info.kind = "Java class file";
    info.summary = "Java class file" + (java.empty() ? std::string() : "  \xC2\xB7  " + java);
    info.facts.push_back({ "Format", "Java class file" });
    info.facts.push_back({ "Class version", std::to_string(major) + "." + std::to_string(minor) + (java.empty() ? "" : " (" + java + ")") });
    return true;
}

void describe_zip(std::string_view b, const std::string& path, BinaryInfo& info) {
    std::string kind = "ZIP archive";
    const std::string ext = extension_of(path);
    if (ext == ".jar" || ext == ".war" || ext == ".ear") kind = "Java archive (ZIP)";
    else if (ext == ".apk" || ext == ".aab") kind = "Android package (ZIP)";
    else if (ext == ".docx" || ext == ".xlsx" || ext == ".pptx") kind = "Office document (ZIP)";
    else if (ext == ".odt" || ext == ".ods" || ext == ".odp") kind = "OpenDocument (ZIP)";
    else if (ext == ".epub") kind = "EPUB book (ZIP)";
    else if (ext == ".whl") kind = "Python wheel (ZIP)";
    else if (ext == ".nupkg") kind = "NuGet package (ZIP)";
    else if (ext == ".vsix") kind = "VS Code extension (ZIP)";
    info.kind = kind;
    info.summary = kind;
    info.facts.push_back({ "Format", "ZIP" });
    const std::size_t floor = b.size() > 65557 ? b.size() - 65557 : 0;
    for (std::size_t at = b.size() >= 22 ? b.size() - 22 : 0; at + 22 <= b.size(); --at) {
        if (le32(b, at) == 0x06054b50) {
            const std::uint64_t entries = le16(b, at + 10);
            const std::uint64_t cd = le32(b, at + 16);
            info.summary += "  \xC2\xB7  " + std::to_string(entries) + (entries == 1 ? " entry" : " entries");
            info.facts.push_back({ "Entries", std::to_string(entries) });
            std::uint64_t p = cd;
            for (std::uint64_t i = 0; i < entries && i < 2000 && fits(b, p, 46) && le32(b, p) == 0x02014b50; ++i) {
                const std::uint64_t name_len = le16(b, p + 28);
                const std::uint64_t extra = le16(b, p + 30);
                const std::uint64_t comment = le16(b, p + 32);
                const std::uint64_t size = le32(b, p + 24);
                std::string name(b.substr(static_cast<std::size_t>(std::min<std::uint64_t>(p + 46, b.size())), static_cast<std::size_t>(name_len)));
                if (!name.empty() && name.back() != '/') name += "  (" + human_size(size) + ")";
                info.libraries.push_back(name);
                p += 46 + name_len + extra + comment;
            }
            break;
        }
        if (at == floor) break;
    }
}

struct Magic {
    std::string_view bytes;
    std::size_t offset;
    const char* kind;
};

bool describe_simple(std::string_view b, const std::string& path, BinaryInfo& info) {
    static const Magic magics[] = {
        { std::string_view("\x1F\x8B", 2), 0, "gzip archive" },
        { std::string_view("7z\xBC\xAF\x27\x1C", 6), 0, "7-Zip archive" },
        { std::string_view("\xFD" "7zXZ\0", 6), 0, "xz archive" },
        { std::string_view("BZh", 3), 0, "bzip2 archive" },
        { std::string_view("\x28\xB5\x2F\xFD", 4), 0, "Zstandard archive" },
        { std::string_view("Rar!\x1A\x07", 6), 0, "RAR archive" },
        { std::string_view("ustar", 5), 257, "tar archive" },
        { std::string_view("!<arch>\n", 8), 0, "Static library (ar archive)" },
        { std::string_view("\0asm", 4), 0, "WebAssembly module" },
        { std::string_view("SQLite format 3\0", 16), 0, "SQLite database" },
        { std::string_view("OTTO", 4), 0, "OpenType font" },
        { std::string_view("\0\1\0\0", 4), 0, "TrueType font" },
        { std::string_view("true", 4), 0, "TrueType font" },
        { std::string_view("ttcf", 4), 0, "TrueType font collection" },
        { std::string_view("wOFF", 4), 0, "WOFF web font" },
        { std::string_view("wOF2", 4), 0, "WOFF2 web font" },
        { std::string_view("OggS", 4), 0, "Ogg media" },
        { std::string_view("fLaC", 4), 0, "FLAC audio" },
        { std::string_view("ID3", 3), 0, "MP3 audio" },
        { std::string_view("\x1A\x45\xDF\xA3", 4), 0, "Matroska / WebM video" },
        { std::string_view("BC\xC0\xDE", 4), 0, "LLVM bitcode" },
        { std::string_view("dex\n", 4), 0, "Android DEX bytecode" },
        { std::string_view("%PDF-", 5), 0, "PDF document" },
        { std::string_view("MSCF", 4), 0, "Windows cabinet archive" },
        { std::string_view("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8), 0, "Microsoft compound file (old Office / MSI)" },
        { std::string_view("\x4C\0\0\0\x01\x14\x02\0", 8), 0, "Windows shortcut (.lnk)" },
    };
    for (const Magic& m : magics) {
        if (!fits(b, m.offset, m.bytes.size()) || b.substr(m.offset, m.bytes.size()) != m.bytes) continue;
        info.kind = m.kind;
        info.summary = m.kind;
        if (m.bytes == std::string_view("\0\1\0\0", 4) && !has_extension(path, { ".ttf", ".otf" })) {
            info.kind.clear();
            info.summary.clear();
            continue;
        }
        if (m.bytes == std::string_view("true", 4) && !has_extension(path, { ".ttf" })) {
            info.kind.clear();
            info.summary.clear();
            continue;
        }
        if (m.bytes == std::string_view("\x1F\x8B", 2) && (u8(b, 3) & 8)) {
            const std::string name = c_string(b, 10, 256);
            if (!name.empty()) info.facts.push_back({ "Original name", name });
        }
        if (m.bytes == std::string_view("\0asm", 4)) info.facts.push_back({ "Version", std::to_string(le32(b, 4)) });
        if (m.bytes == std::string_view("SQLite format 3\0", 16)) {
            std::uint64_t page = be16(b, 16);
            if (page == 1) page = 65536;
            info.facts.push_back({ "Page size", std::to_string(page) + " bytes" });
        }
        if (m.bytes == std::string_view("%PDF-", 5)) {
            info.facts.push_back({ "Version", fixed_string(b, 5, 3) });
            if (const auto pages = pdf_page_count(b)) info.facts.push_back({ "Pages", std::to_string(*pages) });
        }
        return true;
    }
    if (fits(b, 0, 12) && starts_with(b, "RIFF")) {
        const std::string form(b.substr(8, 4));
        if (form == "WAVE") {
            info.kind = "WAV audio";
            const std::uint64_t channels = le16(b, 22);
            const std::uint64_t rate = le32(b, 24);
            const std::uint64_t bits = le16(b, 34);
            info.summary = "WAV audio  \xC2\xB7  " + std::to_string(rate) + " Hz  \xC2\xB7  " + std::to_string(bits) + "-bit  \xC2\xB7  " + (channels == 1 ? std::string("mono") : channels == 2 ? std::string("stereo") : std::to_string(channels) + " channels");
            info.facts.push_back({ "Sample rate", std::to_string(rate) + " Hz" });
            info.facts.push_back({ "Bits per sample", std::to_string(bits) });
            info.facts.push_back({ "Channels", std::to_string(channels) });
            return true;
        }
        if (form == "AVI ") {
            info.kind = info.summary = "AVI video";
            return true;
        }
    }
    if (fits(b, 4, 8) && b.substr(4, 4) == "ftyp") {
        const std::string brand(b.substr(8, 4));
        if (brand == "qt  ") info.kind = "QuickTime video";
        else if (brand == "heic" || brand == "heix" || brand == "mif1" || brand == "msf1") info.kind = "HEIF image";
        else if (brand == "avif") info.kind = "AVIF image";
        else if (brand == "M4A " || brand == "M4B ") info.kind = "MPEG-4 audio";
        else info.kind = "MPEG-4 video";
        info.summary = info.kind + "  \xC2\xB7  brand " + brand;
        info.facts.push_back({ "Brand", brand });
        return true;
    }
    if (fits(b, 0, 2) && static_cast<unsigned char>(b[0]) == 0xFF && (static_cast<unsigned char>(b[1]) & 0xE0) == 0xE0 && has_extension(path, { ".mp3", ".mp2", ".aac" })) {
        info.kind = info.summary = "MPEG audio";
        return true;
    }
    if (has_extension(path, { ".obj", ".o" }) && pe_machine(le16(b, 0)) && le16(b, 2) < 4096) {
        info.kind = "COFF object file";
        info.summary = std::string("COFF object file  \xC2\xB7  ") + pe_machine(le16(b, 0));
        info.facts.push_back({ "Machine", pe_machine(le16(b, 0)) });
        info.facts.push_back({ "Sections", std::to_string(le16(b, 2)) });
        return true;
    }
    return false;
}

std::uint64_t parse_number(std::string_view text) {
    std::uint64_t n = 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) break;
        n = n * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return n;
}

std::string svg_attribute(std::string_view tag, std::string_view name) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        const bool boundary = at == 0 || std::isspace(static_cast<unsigned char>(tag[at - 1]));
        std::size_t p = at + name.size();
        while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
        if (boundary && p < tag.size() && tag[p] == '=') {
            ++p;
            while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
            if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
                const char quote = tag[p];
                const std::size_t end = tag.find(quote, p + 1);
                if (end != std::string_view::npos) return std::string(tag.substr(p + 1, end - p - 1));
            }
        }
        at += name.size();
    }
    return {};
}

unsigned svg_length(const std::string& value) {
    if (value.empty() || value.find('%') != std::string::npos) return 0;
    return static_cast<unsigned>(std::max(0.0, std::atof(value.c_str())) + 0.5);
}

void pnm_size(std::string_view b, ImageInfo& out) {
    std::size_t p = 2;
    std::vector<std::uint64_t> numbers;
    while (p < b.size() && numbers.size() < 2) {
        const char c = b[p];
        if (c == '#') {
            while (p < b.size() && b[p] != '\n') ++p;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t start = p;
            while (p < b.size() && std::isdigit(static_cast<unsigned char>(b[p]))) ++p;
            numbers.push_back(parse_number(b.substr(start, p - start)));
            continue;
        }
        ++p;
    }
    if (numbers.size() == 2) {
        out.width = static_cast<unsigned>(numbers[0]);
        out.height = static_cast<unsigned>(numbers[1]);
    }
}

void tiff_size(std::string_view b, ImageInfo& out) {
    const bool little = b[0] == 'I';
    auto get = [&](std::uint64_t offset, unsigned size) { return read_uint(b, offset, size, little); };
    const std::uint64_t ifd = get(4, 4);
    const std::uint64_t count = get(ifd, 2);
    for (std::uint64_t i = 0; i < count && i < 512; ++i) {
        const std::uint64_t e = ifd + 2 + i * 12;
        if (!fits(b, e, 12)) break;
        const std::uint64_t tag = get(e, 2);
        const std::uint64_t type = get(e + 2, 2);
        const std::uint64_t value = type == 3 ? get(e + 8, 2) : get(e + 8, 4);
        if (tag == 256) out.width = static_cast<unsigned>(value);
        if (tag == 257) out.height = static_cast<unsigned>(value);
    }
}

grove::CodeTokenKind byte_kind(unsigned char c) {
    if (c == 0) return grove::CodeTokenKind::Comment;
    if (c == '\t' || c == '\n' || c == '\r') return grove::CodeTokenKind::Escape;
    if (c >= 0x20 && c < 0x7f) return grove::CodeTokenKind::String;
    if (c == 0xff) return grove::CodeTokenKind::Keyword;
    return grove::CodeTokenKind::Number;
}

struct SpanBuilder {
    std::vector<grove::CodeSpan>& spans;
    std::size_t line = 0;
    bool open = false;
    grove::CodeSpan current;

    void add(std::size_t column, std::size_t length, grove::CodeTokenKind kind) {
        if (open && current.line == line && current.kind == kind && current.column + current.length <= column && column - (current.column + current.length) <= 2) {
            current.length = column + length - current.column;
            return;
        }
        flush();
        current = grove::CodeSpan{ line, column, length, kind };
        open = true;
    }

    void flush() {
        if (open) spans.push_back(current);
        open = false;
    }
};

ByteDump row_dump(std::string_view bytes, std::uint64_t base, std::uint64_t file_size, std::size_t per_line, bool bits) {
    ByteDump out;
    const std::size_t cell = bits ? 9 : 3;
    SpanBuilder spans{ out.spans, 0, false, grove::CodeSpan{} };
    for (std::size_t start = 0, line = 0; start < bytes.size(); start += per_line, ++line) {
        spans.line = line;
        std::string row = hex_offset(base + start, std::max<std::uint64_t>(file_size, base + bytes.size()));
        spans.add(0, row.size(), grove::CodeTokenKind::Comment);
        row += "  ";
        const std::size_t count = std::min(per_line, bytes.size() - start);
        for (std::size_t i = 0; i < per_line; ++i) {
            if (!bits && i == 8) row += ' ';
            const std::size_t column = row.size();
            if (i < count) {
                const unsigned char c = static_cast<unsigned char>(bytes[start + i]);
                char buffer[16];
                if (bits) {
                    for (int bit = 7; bit >= 0; --bit) buffer[7 - bit] = ((c >> bit) & 1) ? '1' : '0';
                    buffer[8] = '\0';
                } else {
                    std::snprintf(buffer, sizeof buffer, "%02x", c);
                }
                row += buffer;
                row += ' ';
                spans.add(column, cell - 1, byte_kind(c));
            } else {
                row.append(cell, ' ');
            }
        }
        row += ' ';
        spans.add(row.size(), 1, grove::CodeTokenKind::Operator);
        row += '|';
        for (std::size_t i = 0; i < count; ++i) {
            const unsigned char c = static_cast<unsigned char>(bytes[start + i]);
            const bool shown = c >= 0x20 && c < 0x7f;
            spans.add(row.size(), 1, shown ? grove::CodeTokenKind::String : grove::CodeTokenKind::Comment);
            row += shown ? static_cast<char>(c) : '.';
        }
        spans.add(row.size(), 1, grove::CodeTokenKind::Operator);
        row += '|';
        spans.flush();
        out.text += row;
        out.text += '\n';
    }
    spans.flush();
    return out;
}

std::vector<std::string> list_dirs_desc(const fs::path& folder) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return out;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_directory(ec)) out.push_back(entry.path().string());
    }
    std::sort(out.begin(), out.end(), std::greater<std::string>());
    return out;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::optional<std::string> find_in_folder(const std::string& folder, const std::string& program) {
    std::error_code ec;
    const fs::path candidate = fs::path(folder) / with_exe_suffix(program);
    if (is_executable_file(candidate.string())) return fs::absolute(candidate, ec).string();
    return std::nullopt;
}

std::size_t trailing_number(const std::string& name) {
    std::size_t end = name.size();
    while (end > 0 && !std::isdigit(static_cast<unsigned char>(name[end - 1]))) --end;
    std::size_t start = end;
    while (start > 0 && std::isdigit(static_cast<unsigned char>(name[start - 1]))) --start;
    return start == end ? 0 : static_cast<std::size_t>(parse_number(std::string_view(name).substr(start, end - start)));
}

std::string trim(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    return text.substr(start);
}

std::string strip_comment(const std::string& line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '%' && (i == 0 || line[i - 1] != '\\')) return line.substr(0, i);
    }
    return line;
}

std::string resolve_against(const std::string& base_dir, std::string file) {
    if (file.rfind("./", 0) == 0) file = file.substr(2);
    fs::path p(file);
    if (p.is_relative()) p = fs::path(base_dir) / p;
    std::error_code ec;
    const fs::path normal = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal().string() : normal.string();
}

std::optional<std::size_t> input_line_number(const std::string& line) {
    const std::size_t at = line.find("input line ");
    if (at == std::string::npos) return std::nullopt;
    const std::size_t n = static_cast<std::size_t>(parse_number(std::string_view(line).substr(at + 11)));
    return n > 0 ? std::optional<std::size_t>(n) : std::nullopt;
}

} // namespace

const char* to_string(ViewerKind kind) noexcept {
    switch (kind) {
    case ViewerKind::Text: return "text";
    case ViewerKind::Image: return "image";
    case ViewerKind::Pdf: return "pdf";
    case ViewerKind::Latex: return "latex";
    case ViewerKind::Binary: return "binary";
    }
    return "text";
}

std::optional<ViewerKind> viewer_kind_from_name(const std::string& name) {
    const std::string n = lower(name);
    if (n == "text") return ViewerKind::Text;
    if (n == "image") return ViewerKind::Image;
    if (n == "pdf") return ViewerKind::Pdf;
    if (n == "latex" || n == "tex") return ViewerKind::Latex;
    if (n == "binary" || n == "hex") return ViewerKind::Binary;
    return std::nullopt;
}

bool is_image_path(const std::string& path) {
    return has_extension(path, { ".png", ".apng", ".jpg", ".jpeg", ".jpe", ".jfif", ".bmp", ".dib", ".gif", ".webp", ".tga", ".qoi", ".pbm", ".pgm", ".ppm", ".pnm", ".pam", ".ico",
                                 ".cur", ".tif", ".tiff", ".svg" });
}

bool is_pdf_path(const std::string& path) {
    return has_extension(path, { ".pdf" });
}

bool is_latex_path(const std::string& path) {
    return has_extension(path, { ".tex", ".latex", ".ltx" });
}

bool is_binary_path(const std::string& path) {
    return has_extension(path, { ".exe", ".dll", ".sys", ".efi", ".com", ".scr", ".ocx", ".so", ".dylib", ".a", ".lib", ".o", ".obj", ".ko", ".elf", ".bin", ".class",
                                 ".jar", ".war", ".apk", ".aab", ".dex", ".wasm", ".pyc", ".pyd", ".zip", ".gz", ".tgz", ".xz", ".bz2", ".zst", ".7z", ".rar", ".tar", ".cab", ".msi",
                                 ".iso", ".img", ".dmg", ".deb", ".rpm", ".ttf", ".otf", ".woff", ".woff2", ".ttc", ".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aac", ".mp4", ".m4v",
                                 ".mov", ".mkv", ".webm", ".avi", ".db", ".sqlite", ".sqlite3", ".pch", ".gch", ".pdb", ".ilk", ".exp", ".bc", ".spv", ".nupkg", ".vsix", ".whl",
                                 ".docx", ".xlsx", ".pptx", ".doc", ".xls", ".ppt", ".odt", ".ods", ".odp", ".epub", ".lnk", ".heic", ".avif" });
}

bool looks_binary(std::string_view bytes) {
    if (bytes.empty()) return false;
    if (starts_with(bytes, "\xEF\xBB\xBF")) return false;
    std::size_t control = 0;
    std::size_t invalid = 0;
    for (std::size_t i = 0; i < bytes.size();) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c == 0) return true;
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\f' && c != '\v' && c != 0x1b) ++control;
        if (c < 0x80) {
            ++i;
            continue;
        }
        const std::size_t need = c >= 0xF0 && c <= 0xF4 ? 3 : c >= 0xE0 && c < 0xF0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : 0;
        if (need == 0) {
            ++invalid;
            ++i;
            continue;
        }
        bool ok = true;
        std::size_t k = 1;
        for (; k <= need && i + k < bytes.size(); ++k) {
            if ((static_cast<unsigned char>(bytes[i + k]) & 0xC0) != 0x80) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            ++invalid;
            ++i;
            continue;
        }
        i += k;
    }
    return control * 10 > bytes.size() || invalid * 20 > bytes.size();
}

ViewerKind viewer_kind_for(const std::string& path, std::string_view head) {
    if (is_pdf_path(path)) return ViewerKind::Pdf;
    if (is_latex_path(path)) return ViewerKind::Latex;
    if (is_image_path(path)) return ViewerKind::Image;
    if (is_binary_path(path)) return ViewerKind::Binary;
    if (starts_with(head, "%PDF-")) return ViewerKind::Pdf;
    const ImageInfo image = image_info(head, path);
    if (!image.format.empty() && image.format != "SVG" && image.format != "PNM") return ViewerKind::Image;
    return looks_binary(head) ? ViewerKind::Binary : ViewerKind::Text;
}

ViewerKind viewer_kind_for(const std::string& path) {
    const std::optional<std::string> head = read_file_range(path, 0, 8192);
    return viewer_kind_for(path, head ? std::string_view(*head) : std::string_view());
}

std::optional<std::uint64_t> file_size(const std::string& path) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec) return std::nullopt;
    return static_cast<std::uint64_t>(size);
}

std::optional<std::string> read_file_range(const std::string& path, std::uint64_t offset, std::size_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    in.seekg(static_cast<std::streamoff>(offset));
    if (!in) return std::string();
    std::string out(size, '\0');
    in.read(out.data(), static_cast<std::streamsize>(size));
    out.resize(static_cast<std::size_t>(in.gcount()));
    return out;
}

std::string human_size(std::uint64_t bytes) {
    if (bytes == 1) return "1 byte";
    if (bytes < 1024) return std::to_string(bytes) + " bytes";
    const char* units[] = { "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, value < 10.0 ? "%.1f %s" : "%.0f %s", value, units[unit]);
    return buffer;
}

std::string hex_offset(std::uint64_t offset, std::uint64_t file_size) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, file_size > 0xFFFFFFFFull ? "%012llx" : "%08llx", static_cast<unsigned long long>(offset));
    return buffer;
}

ImageInfo image_info(std::string_view b, const std::string& path) {
    ImageInfo out;
    if (starts_with(b, "\x89PNG\r\n\x1A\n")) {
        out.format = "PNG";
        out.width = static_cast<unsigned>(be32(b, 16));
        out.height = static_cast<unsigned>(be32(b, 20));
        const std::uint64_t depth = u8(b, 24);
        const std::uint64_t color = u8(b, 25);
        const char* names[] = { "grayscale", "", "RGB", "indexed", "grayscale + alpha", "", "RGBA" };
        if (color <= 6 && *names[color]) out.detail = std::to_string(depth) + "-bit " + names[color];
        for (std::uint64_t at = 8; fits(b, at, 8);) {
            const std::uint64_t length = be32(b, at);
            const std::string type(b.substr(static_cast<std::size_t>(at + 4), 4));
            if (type == "acTL") {
                out.detail += out.detail.empty() ? "animated" : ", animated";
                break;
            }
            if (type == "IDAT" || type == "IEND") break;
            at += 12 + length;
        }
        if (has_extension(path, { ".apng" }) && out.detail.find("animated") == std::string::npos) out.detail += out.detail.empty() ? "APNG" : ", APNG";
        return out;
    }
    if (starts_with(b, "\xFF\xD8\xFF")) {
        out.format = "JPEG";
        for (std::uint64_t at = 2; fits(b, at, 4);) {
            if (u8(b, at) != 0xFF) {
                ++at;
                continue;
            }
            const std::uint64_t marker = u8(b, at + 1);
            if (marker == 0xFF) {
                ++at;
                continue;
            }
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
                at += 2;
                continue;
            }
            const std::uint64_t length = be16(b, at + 2);
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                out.height = static_cast<unsigned>(be16(b, at + 5));
                out.width = static_cast<unsigned>(be16(b, at + 7));
                const std::uint64_t components = u8(b, at + 9);
                out.detail = components == 1 ? "grayscale" : components == 4 ? "CMYK" : "color";
                if (marker == 0xC2) out.detail += ", progressive";
                break;
            }
            if (marker == 0xDA || length < 2) break;
            at += 2 + length;
        }
        return out;
    }
    if (starts_with(b, "GIF87a") || starts_with(b, "GIF89a")) {
        out.format = "GIF";
        out.width = static_cast<unsigned>(le16(b, 6));
        out.height = static_cast<unsigned>(le16(b, 8));
        std::size_t frames = 0;
        for (std::size_t at = b.find("\x21\xF9\x04"); at != std::string_view::npos && frames < 2; at = b.find("\x21\xF9\x04", at + 1)) ++frames;
        if (frames > 1) out.detail = "animated";
        return out;
    }
    if (starts_with(b, "BM") && fits(b, 0, 30)) {
        out.format = "BMP";
        out.width = static_cast<unsigned>(le32(b, 18));
        const std::int32_t h = static_cast<std::int32_t>(static_cast<std::uint32_t>(le32(b, 22)));
        out.height = static_cast<unsigned>(h < 0 ? -static_cast<std::int64_t>(h) : h);
        out.detail = std::to_string(le16(b, 28)) + "-bit";
        return out;
    }
    if (starts_with(b, "RIFF") && fits(b, 8, 4) && b.substr(8, 4) == "WEBP") {
        out.format = "WebP";
        const std::string chunk(b.substr(12, std::min<std::size_t>(4, b.size() - 12)));
        if (chunk == "VP8 ") {
            out.width = static_cast<unsigned>(le16(b, 26) & 0x3FFF);
            out.height = static_cast<unsigned>(le16(b, 28) & 0x3FFF);
            out.detail = "lossy";
        } else if (chunk == "VP8L") {
            const std::uint64_t b0 = u8(b, 21), b1 = u8(b, 22), b2 = u8(b, 23), b3 = u8(b, 24);
            out.width = static_cast<unsigned>(1 + (b0 | ((b1 & 0x3F) << 8)));
            out.height = static_cast<unsigned>(1 + ((b1 >> 6) | (b2 << 2) | ((b3 & 0x0F) << 10)));
            out.detail = "lossless";
        } else if (chunk == "VP8X") {
            out.width = static_cast<unsigned>(1 + read_uint(b, 24, 3, true));
            out.height = static_cast<unsigned>(1 + read_uint(b, 27, 3, true));
            const std::uint64_t flags = u8(b, 20);
            if (flags & 0x02) out.detail = "animated";
            if (flags & 0x10) out.detail += out.detail.empty() ? "with alpha" : ", with alpha";
        }
        return out;
    }
    if (starts_with(b, "qoif")) {
        out.format = "QOI";
        out.width = static_cast<unsigned>(be32(b, 4));
        out.height = static_cast<unsigned>(be32(b, 8));
        out.detail = u8(b, 12) == 4 ? "RGBA" : "RGB";
        return out;
    }
    if (fits(b, 0, 6) && le16(b, 0) == 0 && (le16(b, 2) == 1 || le16(b, 2) == 2) && le16(b, 4) > 0 && le16(b, 4) < 256 && (has_extension(path, { ".ico", ".cur" }) || path.empty())) {
        out.format = le16(b, 2) == 1 ? "ICO" : "CUR";
        const std::uint64_t count = le16(b, 4);
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t e = 6 + i * 16;
            if (!fits(b, e, 16)) break;
            const unsigned w = u8(b, e) ? static_cast<unsigned>(u8(b, e)) : 256u;
            const unsigned h = u8(b, e + 1) ? static_cast<unsigned>(u8(b, e + 1)) : 256u;
            if (w * h > out.width * out.height) {
                out.width = w;
                out.height = h;
            }
        }
        out.detail = std::to_string(count) + (count == 1 ? " size" : " sizes");
        return out;
    }
    if (starts_with(b, std::string_view("II*\0", 4)) || starts_with(b, std::string_view("MM\0*", 4))) {
        out.format = "TIFF";
        tiff_size(b, out);
        return out;
    }
    if (fits(b, 0, 2) && b[0] == 'P' && b[1] >= '1' && b[1] <= '7' && (has_extension(path, { ".pbm", ".pgm", ".ppm", ".pnm", ".pam" }) || (fits(b, 2, 1) && std::isspace(static_cast<unsigned char>(b[2]))))) {
        out.format = "PNM";
        if (b[1] != '7') pnm_size(b, out);
        else {
            const std::size_t w = b.find("WIDTH ");
            const std::size_t h = b.find("HEIGHT ");
            if (w != std::string_view::npos) out.width = static_cast<unsigned>(parse_number(b.substr(w + 6)));
            if (h != std::string_view::npos) out.height = static_cast<unsigned>(parse_number(b.substr(h + 7)));
        }
        return out;
    }
    if (has_extension(path, { ".tga" }) && fits(b, 0, 18)) {
        out.format = "TGA";
        out.width = static_cast<unsigned>(le16(b, 12));
        out.height = static_cast<unsigned>(le16(b, 14));
        out.detail = std::to_string(u8(b, 16)) + "-bit";
        return out;
    }
    const std::size_t svg = b.find("<svg");
    if (svg != std::string_view::npos && (has_extension(path, { ".svg" }) || svg < 1024)) {
        out.format = "SVG";
        const std::size_t end = b.find('>', svg);
        const std::string_view tag = b.substr(svg, end == std::string_view::npos ? std::string_view::npos : end - svg);
        out.width = svg_length(svg_attribute(tag, "width"));
        out.height = svg_length(svg_attribute(tag, "height"));
        if (!out.known_size()) {
            std::istringstream box(svg_attribute(tag, "viewBox"));
            double x = 0, y = 0, w = 0, h = 0;
            if (box >> x >> y >> w >> h) {
                out.width = static_cast<unsigned>(w + 0.5);
                out.height = static_cast<unsigned>(h + 0.5);
            }
        }
        out.detail = "vector";
        return out;
    }
    return out;
}

BinaryInfo describe_binary(std::string_view bytes, const std::string& path) {
    BinaryInfo info;
    const std::uint64_t size = bytes.size();
    if (!describe_pe(bytes, path, info) && !describe_elf(bytes, info) && !describe_macho(bytes, info) && !describe_cafebabe(bytes, info)) {
        if (starts_with(bytes, "PK\x03\x04") || starts_with(bytes, "PK\x05\x06")) {
            describe_zip(bytes, path, info);
        } else if (!describe_simple(bytes, path, info)) {
            const ImageInfo image = image_info(bytes, path);
            if (!image.format.empty()) {
                info.kind = image.format + " image";
                info.summary = info.kind;
                if (image.known_size()) {
                    info.summary += "  \xC2\xB7  " + std::to_string(image.width) + " \xC3\x97 " + std::to_string(image.height);
                    info.facts.push_back({ "Dimensions", std::to_string(image.width) + " \xC3\x97 " + std::to_string(image.height) + " pixels" });
                }
                if (!image.detail.empty()) info.facts.push_back({ "Detail", image.detail });
            } else {
                info.kind = looks_binary(bytes.substr(0, 8192)) ? "Binary data" : "Text";
                info.summary = info.kind;
            }
        }
    }
    info.facts.insert(info.facts.begin(), FileFact{ "Size", size_fact(size) });
    info.summary += "  \xC2\xB7  " + human_size(size);
    if (!info.kind.empty()) info.facts.insert(info.facts.begin(), FileFact{ "Kind", info.kind });
    return info;
}

std::size_t byte_view_page_size(ByteView view) {
    switch (view) {
    case ByteView::Hex: return 64 * 1024;
    case ByteView::Bits: return 16 * 1024;
    case ByteView::Strings: return 1024 * 1024;
    }
    return 64 * 1024;
}

ByteDump hex_dump(std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size) {
    return row_dump(bytes, base_offset, file_size, 16, false);
}

ByteDump bits_dump(std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size) {
    return row_dump(bytes, base_offset, file_size, 8, true);
}

ByteDump strings_dump(std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size, std::size_t min_length) {
    struct Found {
        std::size_t at;
        std::string text;
        bool wide;
    };
    std::vector<Found> found;
    auto printable = [](unsigned char c) { return (c >= 0x20 && c < 0x7f) || c == '\t'; };
    for (std::size_t i = 0; i < bytes.size();) {
        std::size_t j = i;
        while (j < bytes.size() && printable(static_cast<unsigned char>(bytes[j]))) ++j;
        if (j - i >= min_length) found.push_back({ i, std::string(bytes.substr(i, j - i)), false });
        i = j == i ? i + 1 : j;
    }
    for (std::size_t i = 0; i + 1 < bytes.size();) {
        std::size_t j = i;
        std::string text;
        while (j + 1 < bytes.size() && printable(static_cast<unsigned char>(bytes[j])) && bytes[j + 1] == '\0') {
            text += bytes[j];
            j += 2;
        }
        if (text.size() >= min_length) {
            found.push_back({ i, text, true });
            i = j;
        } else {
            i += 1;
        }
    }
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.at < b.at; });
    ByteDump out;
    std::size_t line = 0;
    for (const Found& f : found) {
        std::string row = hex_offset(base_offset + f.at, file_size);
        out.spans.push_back({ line, 0, row.size(), grove::CodeTokenKind::Comment });
        row += "  ";
        std::string text = f.text;
        for (char& c : text) {
            if (c == '\t') c = ' ';
        }
        if (text.size() > 2000) text = text.substr(0, 2000) + "\xE2\x80\xA6";
        out.spans.push_back({ line, row.size(), text.size(), grove::CodeTokenKind::String });
        row += text;
        if (f.wide) {
            row += "  ";
            out.spans.push_back({ line, row.size(), 6, grove::CodeTokenKind::Keyword });
            row += "UTF-16";
        }
        out.text += row + "\n";
        ++line;
    }
    return out;
}

ByteDump byte_dump(ByteView view, std::string_view bytes, std::uint64_t base_offset, std::uint64_t file_size) {
    switch (view) {
    case ByteView::Hex: return hex_dump(bytes, base_offset, file_size);
    case ByteView::Bits: return bits_dump(bytes, base_offset, file_size);
    case ByteView::Strings: return strings_dump(bytes, base_offset, file_size);
    }
    return hex_dump(bytes, base_offset, file_size);
}

std::string sections_table(const std::vector<BinarySection>& sections) {
    if (sections.empty()) return {};
    std::size_t name_width = 4;
    for (const BinarySection& s : sections) name_width = std::max(name_width, s.name.size());
    name_width = std::min<std::size_t>(name_width, 32);
    auto pad = [](std::string text, std::size_t width) {
        if (text.size() > width) text = text.substr(0, width);
        text.append(width - text.size(), ' ');
        return text;
    };
    std::string out = pad("Name", name_width) + "  " + pad("Address", 18) + "  " + pad("Offset", 12) + "  " + pad("Size", 10) + "  Flags\n";
    for (const BinarySection& s : sections) {
        out += pad(s.name.empty() ? "(unnamed)" : s.name, name_width) + "  " + pad(hex(s.address), 18) + "  " + pad(hex(s.offset), 12) + "  " + pad(human_size(s.size), 10) + "  " + s.flags + "\n";
    }
    return out;
}

std::string viewer_tool_name(const std::string& path) {
    std::string name = lower(fs::path(path).filename().string());
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
    return name;
}

std::vector<std::string> pdf_renderer_candidates() {
    return { "pdftoppm", "mutool", "pdftocairo", "gs", "gswin64c", "gswin32c" };
}

std::vector<std::string> latex_engine_candidates() {
    return { "latexmk", "tectonic", "pdflatex", "xelatex", "lualatex" };
}

std::vector<std::string> viewer_tool_folders(const std::string& key) {
    std::vector<std::string> folders = known_tool_folders();
    const std::string home = env("HOME");
#if defined(_WIN32)
    const std::string local = env("LOCALAPPDATA");
    const std::string profile = env("USERPROFILE");
    if (!profile.empty()) folders.push_back((fs::path(profile) / "scoop" / "shims").string());
    if (key == "latex") {
        folders.push_back("C:/Program Files/MiKTeX/miktex/bin/x64");
        if (!local.empty()) folders.push_back((fs::path(local) / "Programs" / "MiKTeX" / "miktex" / "bin" / "x64").string());
        for (const std::string& year : list_dirs_desc("C:/texlive")) {
            for (const char* arch : { "windows", "win64", "win32" }) folders.push_back((fs::path(year) / "bin" / arch).string());
        }
    } else {
        for (const char* root : { "C:/Program Files/gs", "C:/Program Files (x86)/gs" }) {
            for (const std::string& version : list_dirs_desc(root)) folders.push_back((fs::path(version) / "bin").string());
        }
        folders.push_back("C:/Program Files/MuPDF");
        if (!local.empty()) folders.push_back((fs::path(local) / "Programs" / "MuPDF").string());
    }
#elif defined(__APPLE__)
    if (key == "latex") {
        folders.push_back("/Library/TeX/texbin");
        for (const std::string& year : list_dirs_desc("/usr/local/texlive")) folders.push_back((fs::path(year) / "bin" / "universal-darwin").string());
        if (!home.empty()) folders.push_back((fs::path(home) / "Library" / "TinyTeX" / "bin" / "universal-darwin").string());
    }
#else
    if (key == "latex") {
        for (const std::string& year : list_dirs_desc("/usr/local/texlive")) {
            for (const char* arch : { "x86_64-linux", "aarch64-linux" }) folders.push_back((fs::path(year) / "bin" / arch).string());
        }
        if (!home.empty()) {
            for (const char* arch : { "x86_64-linux", "aarch64-linux" }) folders.push_back((fs::path(home) / ".TinyTeX" / "bin" / arch).string());
        }
    }
#endif
    if (!home.empty()) folders.push_back((fs::path(home) / ".cargo" / "bin").string());
    return folders;
}

std::optional<ViewerTool> find_viewer_tool(const Settings& settings, const std::string& key) {
    const std::string chosen = settings.get_or("viewer", key, "auto");
    if (!chosen.empty() && chosen != "auto") {
        if (is_executable_file(chosen)) return ViewerTool{ key, chosen, viewer_tool_name(chosen) };
        if (fs::path(chosen).filename() == fs::path(chosen)) {
            if (const auto found = find_on_path(chosen)) return ViewerTool{ key, *found, viewer_tool_name(*found) };
            for (const std::string& folder : viewer_tool_folders(key)) {
                if (const auto found = find_in_folder(folder, chosen)) return ViewerTool{ key, *found, viewer_tool_name(*found) };
            }
        }
        return std::nullopt;
    }
    const std::vector<std::string> names = key == "latex" ? latex_engine_candidates() : pdf_renderer_candidates();
    for (const std::string& name : names) {
        if (const auto found = find_on_path(name)) return ViewerTool{ key, *found, viewer_tool_name(*found) };
    }
    const std::vector<std::string> folders = viewer_tool_folders(key);
    for (const std::string& name : names) {
        for (const std::string& folder : folders) {
            if (const auto found = find_in_folder(folder, name)) return ViewerTool{ key, *found, viewer_tool_name(*found) };
        }
    }
    return std::nullopt;
}

std::string viewer_install_hint(const std::string& key) {
    if (key == "latex") {
#if defined(_WIN32)
        return "Install MiKTeX (https://miktex.org) or TeX Live (https://tug.org/texlive), or the single-program Tectonic (winget install TectonicTypesetting.Tectonic), then reopen the file.";
#elif defined(__APPLE__)
        return "Install MacTeX (https://tug.org/mactex), or the single-program Tectonic: brew install tectonic. Then reopen the file.";
#else
        return "Install TeX: Fedora: sudo dnf install texlive-scheme-basic latexmk   Ubuntu/Debian: sudo apt install texlive-latex-recommended latexmk   or the single-program Tectonic (https://tectonic-typesetting.github.io). Then reopen the file.";
#endif
    }
#if defined(_WIN32)
    return "Canopy draws PDF pages with Poppler, MuPDF or Ghostscript. In MSYS2: pacman -S mingw-w64-ucrt-x86_64-poppler, or install MuPDF or Ghostscript. Then reopen the file.";
#elif defined(__APPLE__)
    return "Canopy draws PDF pages with Poppler, MuPDF or Ghostscript: brew install poppler. Then reopen the file.";
#else
    return "Canopy draws PDF pages with Poppler, MuPDF or Ghostscript. Fedora: sudo dnf install poppler-utils   Ubuntu/Debian: sudo apt install poppler-utils. Then reopen the file.";
#endif
}

int pdf_resolution(const Settings& settings) {
    const std::string value = settings.get_or("viewer", "pdf-resolution", "144");
    const int dpi = std::atoi(value.c_str());
    if (dpi <= 0) return 144;
    return std::clamp(dpi, 36, 600);
}

std::vector<std::string> pdf_render_command(const ViewerTool& tool, const std::string& pdf, const std::string& out_dir, int dpi) {
    const std::string resolution = std::to_string(dpi);
    const std::string name = tool.name;
    if (name == "mutool") return { tool.path, "draw", "-q", "-r", resolution, "-o", (fs::path(out_dir) / "page-%d.png").string(), pdf };
    if (name.rfind("gs", 0) == 0) {
        return { tool.path, "-q", "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=png16m", "-dTextAlphaBits=4", "-dGraphicsAlphaBits=4", "-r" + resolution,
                 "-sOutputFile=" + (fs::path(out_dir) / "page-%d.png").string(), pdf };
    }
    return { tool.path, "-png", "-r", resolution, pdf, (fs::path(out_dir) / "page").string() };
}

std::vector<std::string> rendered_pages(const std::string& out_dir) {
    std::vector<std::string> pages;
    std::error_code ec;
    if (!fs::is_directory(out_dir, ec)) return pages;
    for (const auto& entry : fs::directory_iterator(out_dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (lower(entry.path().extension().string()) == ".png" && name.rfind("page", 0) == 0) pages.push_back(entry.path().string());
    }
    std::sort(pages.begin(), pages.end(), [](const std::string& a, const std::string& b) {
        const std::size_t na = trailing_number(fs::path(a).stem().string());
        const std::size_t nb = trailing_number(fs::path(b).stem().string());
        return na != nb ? na < nb : a < b;
    });
    return pages;
}

std::optional<std::size_t> pdf_page_count(std::string_view bytes) {
    std::size_t best = 0;
    for (std::size_t at = bytes.find("/Count"); at != std::string_view::npos; at = bytes.find("/Count", at + 6)) {
        std::size_t p = at + 6;
        while (p < bytes.size() && (bytes[p] == ' ' || bytes[p] == '\r' || bytes[p] == '\n')) ++p;
        best = std::max(best, static_cast<std::size_t>(parse_number(bytes.substr(p, 12))));
    }
    if (best > 0) return best;
    std::size_t pages = 0;
    for (std::size_t at = bytes.find("/Type"); at != std::string_view::npos; at = bytes.find("/Type", at + 5)) {
        std::size_t p = at + 5;
        while (p < bytes.size() && (bytes[p] == ' ' || bytes[p] == '\r' || bytes[p] == '\n')) ++p;
        if (bytes.substr(p, 5) == "/Page" && (p + 5 >= bytes.size() || bytes[p + 5] != 's')) ++pages;
    }
    return pages > 0 ? std::optional<std::size_t>(pages) : std::nullopt;
}

std::string latex_root_file(const std::string& path, const std::string& text) {
    std::istringstream in(text);
    std::string line;
    for (int n = 0; n < 30 && std::getline(in, line); ++n) {
        const std::string l = lower(line);
        const std::size_t percent = l.find('%');
        if (percent == std::string::npos) continue;
        std::string rest = l.substr(percent + 1);
        const std::size_t bang = rest.find('!');
        if (bang == std::string::npos) continue;
        rest = trim(rest.substr(bang + 1));
        if (rest.rfind("tex", 0) != 0) continue;
        rest = trim(rest.substr(3));
        if (rest.rfind("root", 0) != 0) continue;
        const std::size_t equals = line.find('=', percent);
        if (equals == std::string::npos) continue;
        const std::string target = trim(line.substr(equals + 1));
        if (target.empty()) continue;
        return resolve_against(fs::path(path).parent_path().string(), target);
    }
    return path;
}

bool latex_is_document(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string code = strip_comment(line);
        if (code.find("\\documentclass") != std::string::npos || code.find("\\documentstyle") != std::string::npos) return true;
    }
    return false;
}

std::vector<std::string> latex_build_command(const ViewerTool& tool, const std::string& tex, const std::string& out_dir, const std::string& flags) {
    const std::string file = fs::path(tex).filename().string();
    std::vector<std::string> command;
    if (tool.name == "latexmk") {
        command = { tool.path, "-pdf", "-interaction=nonstopmode", "-file-line-error", "-outdir=" + out_dir };
    } else if (tool.name == "tectonic") {
        command = { tool.path, "--outdir", out_dir };
    } else {
        command = { tool.path, "-interaction=nonstopmode", "-file-line-error", "-output-directory=" + out_dir };
    }
    for (const std::string& f : split_flags(flags)) command.push_back(f);
    command.push_back(file);
    return command;
}

std::string latex_pdf_path(const std::string& tex, const std::string& out_dir) {
    return (fs::path(out_dir) / (fs::path(tex).stem().string() + ".pdf")).string();
}

std::vector<CompilerMessage> parse_latex_log(const std::string& output, const std::string& tex) {
    std::vector<CompilerMessage> out;
    std::set<std::string> seen;
    const std::string base = fs::path(tex).parent_path().string();
    std::vector<std::string> lines;
    {
        std::istringstream in(output);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    auto add = [&](CompilerMessage m) {
        m.message = trim(m.message);
        if (m.message.empty()) return;
        const std::string key = m.file + ":" + std::to_string(m.line) + ":" + m.message;
        if (seen.insert(key).second) out.push_back(std::move(m));
    };
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line = lines[i];
        std::string severity = "error";
        if (line.rfind("error: ", 0) == 0) line = line.substr(7);
        else if (line.rfind("warning: ", 0) == 0) {
            line = line.substr(9);
            severity = "warning";
        }
        bool matched = false;
        for (const char* ext : { ".tex:", ".sty:", ".cls:", ".ltx:", ".latex:", ".bib:" }) {
            const std::size_t at = line.find(ext);
            if (at == std::string::npos) continue;
            const std::size_t colon = at + std::string(ext).size() - 1;
            std::size_t p = colon + 1;
            while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) ++p;
            if (p == colon + 1 || p >= line.size() || line[p] != ':') continue;
            std::string file = line.substr(0, colon);
            const std::size_t space = file.rfind(' ');
            if (space != std::string::npos && file.find('/') == std::string::npos && file.find('\\') == std::string::npos) file = file.substr(space + 1);
            CompilerMessage m;
            m.file = resolve_against(base, file);
            m.line = static_cast<std::size_t>(parse_number(std::string_view(line).substr(colon + 1)));
            m.severity = severity;
            m.message = line.substr(p + 1);
            if (line.size() == 79 && i + 1 < lines.size() && !lines[i + 1].empty() && lines[i + 1][0] != '!' && lines[i + 1][0] != 'l') m.message += lines[i + 1];
            add(m);
            matched = true;
            break;
        }
        if (matched) continue;
        if (line.rfind("! ", 0) == 0) {
            CompilerMessage m;
            m.file = tex;
            m.severity = "error";
            m.message = line.substr(2);
            for (std::size_t k = i + 1; k < lines.size() && k < i + 12; ++k) {
                if (lines[k].rfind("l.", 0) == 0) {
                    m.line = static_cast<std::size_t>(parse_number(std::string_view(lines[k]).substr(2)));
                    break;
                }
            }
            if (m.message.rfind("Emergency stop", 0) == 0 || m.message.rfind("==> Fatal error", 0) == 0) continue;
            add(m);
            continue;
        }
        const std::size_t warning = line.find("Warning: ");
        if (warning != std::string::npos && (line.rfind("LaTeX ", 0) == 0 || line.rfind("Package ", 0) == 0 || line.rfind("Class ", 0) == 0 || line.rfind("LaTeX Font", 0) == 0)) {
            std::string message = line.substr(warning + 9);
            std::size_t k = i + 1;
            while (k < lines.size() && !lines[k].empty() && lines[k][0] == '(' && lines[k].find(')') != std::string::npos && k < i + 6) {
                const std::size_t close = lines[k].find(')');
                message += " " + trim(lines[k].substr(close + 1));
                ++k;
            }
            CompilerMessage m;
            m.file = tex;
            m.severity = "warning";
            const std::optional<std::size_t> n = input_line_number(message);
            m.line = n ? *n : 0;
            const std::size_t on = message.find(" on input line");
            if (on != std::string::npos) message = message.substr(0, on);
            if (!message.empty() && message.back() == '.') message.pop_back();
            m.message = message;
            add(m);
        }
    }
    return out;
}

std::string viewer_cache_dir(const std::string& path) {
    std::error_code ec;
    const fs::path normal = fs::weakly_canonical(fs::absolute(path, ec), ec);
    const std::size_t h = std::hash<std::string>()(normal.string());
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(h));
    fs::path base = fs::temp_directory_path(ec);
    if (ec) base = fs::path(".");
    return (base / "canopy-view" / (fs::path(path).stem().string() + "-" + buffer)).string();
}

} // namespace canopy
