#include <canopy/asm_info.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace canopy {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '%' || c == '#';
}

std::vector<std::string> words_of(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        if (!word_char(line[i])) {
            ++i;
            continue;
        }
        std::size_t e = i;
        while (e < line.size() && word_char(line[e])) ++e;
        out.push_back(line.substr(i, e - i));
        i = e;
    }
    return out;
}

bool numbered(const std::string& w, const std::string& prefix, int most) {
    if (w.size() <= prefix.size() || w.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string rest = w.substr(prefix.size());
    if (rest.empty() || rest.size() > 2 || rest.find_first_not_of("0123456789") != std::string::npos) return false;
    return std::stoi(rest) <= most;
}

std::string strip_comment(const std::string& line) {
    std::string out;
    char quote = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quote) {
            out += c;
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            out += c;
            continue;
        }
        if (c == ';') break;
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;
        if ((c == '#' || c == '@') && (i + 1 >= line.size() || line[i + 1] == ' ' || line[i + 1] == '\t')) break;
        out += c;
    }
    return out;
}

} // namespace

const char* to_string(AsmDialect dialect) noexcept {
    switch (dialect) {
    case AsmDialect::Nasm: return "NASM";
    case AsmDialect::Gas: return "GAS (AT&T)";
    case AsmDialect::GasIntel: return "GAS (Intel)";
    case AsmDialect::Masm: return "MASM";
    case AsmDialect::Fasm: return "FASM";
    }
    return "?";
}

const char* to_string(AsmArch arch) noexcept {
    switch (arch) {
    case AsmArch::X86_64: return "x86-64";
    case AsmArch::X86: return "x86 (32-bit)";
    case AsmArch::Arm64: return "ARM64";
    case AsmArch::Arm: return "ARM (32-bit)";
    case AsmArch::RiscV64: return "RISC-V 64";
    case AsmArch::Mips: return "MIPS";
    }
    return "?";
}

AsmArch host_arch() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return AsmArch::Arm64;
#elif defined(__arm__) || defined(_M_ARM)
    return AsmArch::Arm;
#elif defined(__riscv)
    return AsmArch::RiscV64;
#elif defined(__mips__)
    return AsmArch::Mips;
#elif defined(__i386__) || defined(_M_IX86)
    return AsmArch::X86;
#else
    return AsmArch::X86_64;
#endif
}

AsmInfo detect_assembly(const std::string& text, const std::string& path) {
    return detect_assembly(text, path, "auto", "auto");
}

AsmInfo detect_assembly(const std::string& text, const std::string& path, const std::string& dialect_setting, const std::string& arch_setting) {
    AsmInfo info;
    const std::string ext = lower(fs::path(path).extension().string());
    int nasm = ext == ".asm" || ext == ".nasm" ? 2 : 0;
    int gas = ext == ".s" ? 2 : 0;
    int intel = 0;
    int masm = ext == ".masm" ? 4 : 0;
    int fasm = ext == ".fasm" ? 4 : 0;
    int x64 = 0;
    int x86 = 0;
    int arm64 = 0;
    int arm = 0;
    int riscv = 0;
    int mips = 0;
    std::set<std::string> labels;
    std::set<std::string> globals;
    std::size_t start = 0;
    std::size_t count = 0;
    while (start < text.size() && count < 4000) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string raw = text.substr(start, end - start);
        start = end + 1;
        ++count;
        const std::string line = lower(strip_comment(raw));
        const std::size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const std::string body = line.substr(first);
        std::vector<std::string> w = words_of(body);
        if (w.empty()) continue;
        const std::string& head = w[0];
        if (body.find(':') != std::string::npos && body.find(':') == head.size()) {
            labels.insert(head);
            w.erase(w.begin());
            if (w.empty()) continue;
        }
        const std::string& op = w[0];
        if (op == "section" || op == "segment" || op == "bits" || op == "global" || op == "extern" || op == "%define" || op == "%macro" || op == "%include" ||
            op == "default" || op == "resb" || op == "resq" || op == "times") {
            nasm += 3;
        }
        if (w.size() > 1 && (w[1] == "db" || w[1] == "dw" || w[1] == "dd" || w[1] == "dq" || w[1] == "resb" || w[1] == "resq" || w[1] == "equ")) nasm += 2;
        if (body.find("[rel ") != std::string::npos) nasm += 3;
        if (op == ".globl" || op == ".global" || op == ".section" || op == ".text" || op == ".data" || op == ".type" || op == ".size" || op == ".asciz" ||
            op == ".string" || op == ".quad" || op == ".long" || op == ".byte" || op == ".ascii" || op == ".align" || op == ".p2align" || op == ".bss") {
            gas += 3;
        }
        if (op == ".intel_syntax") intel += 20;
        if (op == ".model" || op == ".code" || op == ".data?" || op == "includelib" || op == "invoke" || op == "option" || op == ".stack" || op == ".386" || op == ".686") masm += 4;
        if (w.size() > 1 && (w[1] == "proc" || w[1] == "endp" || w[1] == "ends" || w[1] == "segment")) masm += 4;
        if (op == "format" || op == "entry" || (op == "segment" && body.find("readable") != std::string::npos)) fasm += 5;
        if ((op == "global" || op == ".globl" || op == ".global" || op == "public") && w.size() > 1) globals.insert(w[1]);
        for (const std::string& x : w) {
            std::string r = x;
            while (!r.empty() && (r[0] == '%' || r[0] == '#')) r.erase(0, 1);
            if (x[0] == '%' && (numbered(r, "r", 15) || r == "rax" || r == "eax" || r == "rsp" || r == "rbp" || r == "esp")) gas += 2;
            if (r == "rax" || r == "rbx" || r == "rcx" || r == "rdx" || r == "rsi" || r == "rdi" || r == "rsp" || r == "rbp" || r == "rip" || r == "qword" ||
                numbered(r, "r", 15) || r == "syscall" || r == "movq" || r == "pushq" || r == "popq") {
                x64 += numbered(r, "r", 7) ? 0 : 2;
            }
            if (r == "eax" || r == "ebx" || r == "ecx" || r == "edx" || r == "esi" || r == "edi" || r == "esp" || r == "ebp" || r == "dword") x86 += 1;
            if (numbered(r, "x", 30) || numbered(r, "w", 30) || r == "stp" || r == "ldp" || r == "adrp" || r == "xzr" || r == "wzr" || r == "cbz" || r == "cbnz") arm64 += 2;
            if (numbered(r, "r", 12) && x64 == 0) arm += 1;
            if (r == "bx" || r == "swi" || r == "ldr" || r == "str" || r == "ldmfd" || r == "stmfd" || r == ".arm" || r == ".thumb") arm += 2;
            if (r == "ecall" || r == "ebreak" || numbered(r, "a", 7) || numbered(r, "t", 6) || r == "addi" || r == "sd" || r == "jalr" || r == "auipc" || r == "li" || r == "la") riscv += r == "ecall" ? 4 : 1;
            if (x[0] == '$' && (numbered(r.substr(1), "t", 9) || numbered(r.substr(1), "v", 1) || numbered(r.substr(1), "a", 3) || r == "$sp" || r == "$ra" || r == "$zero")) mips += 3;
        }
        if (body.find("int 0x80") != std::string::npos || body.find("bits 32") != std::string::npos) x86 += 5;
        if (body.find("bits 64") != std::string::npos) x64 += 5;
        if (body.find("svc #0") != std::string::npos || body.find("svc 0") != std::string::npos) {
            arm64 += 1;
            arm += 1;
        }
    }
    if (intel > 0) gas += intel;
    struct Pick {
        AsmDialect dialect;
        int score;
    };
    std::vector<Pick> dialects = { { AsmDialect::Nasm, nasm }, { AsmDialect::Gas, gas }, { AsmDialect::Masm, masm }, { AsmDialect::Fasm, fasm } };
    std::stable_sort(dialects.begin(), dialects.end(), [](const Pick& a, const Pick& b) { return a.score > b.score; });
    info.dialect = dialects.front().dialect;
    if (info.dialect == AsmDialect::Gas && intel > 0) info.dialect = AsmDialect::GasIntel;
    struct ArchPick {
        AsmArch arch;
        int score;
    };
    std::vector<ArchPick> arches = { { host_arch(), 0 }, { AsmArch::X86_64, x64 }, { AsmArch::Arm64, arm64 }, { AsmArch::RiscV64, riscv }, { AsmArch::Mips, mips },
                                     { AsmArch::Arm, arm64 > 0 ? 0 : arm }, { AsmArch::X86, x64 > 0 ? 0 : x86 } };
    std::stable_sort(arches.begin(), arches.end(), [](const ArchPick& a, const ArchPick& b) { return a.score > b.score; });
    info.arch = arches.front().score > 0 ? arches.front().arch : (info.dialect == AsmDialect::Gas || info.dialect == AsmDialect::GasIntel ? host_arch() : AsmArch::X86_64);
    if (info.dialect == AsmDialect::Nasm || info.dialect == AsmDialect::Masm || info.dialect == AsmDialect::Fasm) {
        if (info.arch != AsmArch::X86) info.arch = AsmArch::X86_64;
    }
    const std::string ds = lower(dialect_setting);
    if (ds == "nasm") info.dialect = AsmDialect::Nasm;
    else if (ds == "gas" || ds == "att") info.dialect = AsmDialect::Gas;
    else if (ds == "gas-intel" || ds == "intel") info.dialect = AsmDialect::GasIntel;
    else if (ds == "masm") info.dialect = AsmDialect::Masm;
    else if (ds == "fasm") info.dialect = AsmDialect::Fasm;
    info.dialect_chosen = ds != "auto" && !ds.empty();
    const std::string as = lower(arch_setting);
    if (as == "x86-64" || as == "x86_64" || as == "x64" || as == "amd64") info.arch = AsmArch::X86_64;
    else if (as == "x86" || as == "i386" || as == "x86-32") info.arch = AsmArch::X86;
    else if (as == "arm64" || as == "aarch64") info.arch = AsmArch::Arm64;
    else if (as == "arm" || as == "arm32") info.arch = AsmArch::Arm;
    else if (as == "riscv64" || as == "riscv" || as == "risc-v") info.arch = AsmArch::RiscV64;
    else if (as == "mips") info.arch = AsmArch::Mips;
    info.arch_chosen = as != "auto" && !as.empty();
    for (const char* name : { "main", "_main", "_start", "start", "winmain" }) {
        if (labels.count(name) || globals.count(name)) {
            info.entry = std::string(name) == "_main" ? "main" : name;
            break;
        }
    }
    return info;
}

std::vector<AsmBuilder> assembly_builders(const AsmInfo& info, const Toolchain& toolchain) {
    std::vector<AsmBuilder> out;
    const bool native = info.arch == host_arch() || (host_arch() == AsmArch::X86_64 && info.arch == AsmArch::X86);
    auto add = [&](const std::string& name, const std::string& key, bool runs, const std::string& note) {
        AsmBuilder b;
        b.name = name;
        b.key = key;
        b.found = toolchain.get(key) != nullptr;
        b.runs_here = runs;
        b.note = note;
        out.push_back(b);
    };
    std::string qemu_key;
    std::string cross_key;
    switch (info.arch) {
    case AsmArch::Arm64:
        qemu_key = "qemu-aarch64";
        cross_key = "gcc-aarch64";
        break;
    case AsmArch::Arm:
        qemu_key = "qemu-arm";
        cross_key = "gcc-arm";
        break;
    case AsmArch::RiscV64:
        qemu_key = "qemu-riscv64";
        cross_key = "gcc-riscv64";
        break;
    case AsmArch::Mips:
        qemu_key = "qemu-mips";
        cross_key = "gcc-mips";
        break;
    case AsmArch::X86_64:
    case AsmArch::X86:
        qemu_key = "qemu-x86_64";
        cross_key = "gcc-x86_64";
        break;
    }
    const bool emulated = !native && toolchain.get(qemu_key) != nullptr;
    switch (info.dialect) {
    case AsmDialect::Nasm:
        add("nasm", "nasm", native, "assembles it; the C compiler or ld links it");
        add("yasm", "yasm", native, "a NASM-compatible assembler");
        break;
    case AsmDialect::Gas:
    case AsmDialect::GasIntel:
        if (native) add(toolchain.get("c") ? fs::path(toolchain.get("c")->path).filename().string() : std::string("gcc or clang"), "c", true, "assembles and links .s files");
        else add(toolchain.get(cross_key) ? fs::path(toolchain.get(cross_key)->path).filename().string() : cross_key, cross_key, emulated, emulated ? "runs it through " + qemu_key : "builds it; running it needs " + qemu_key);
        break;
    case AsmDialect::Masm:
#if defined(_WIN32)
        add("ml64 / ml", "masm", native, "Visual Studio's assembler");
#else
        add("ml64 / ml", "masm", false, "only on Windows with Visual Studio");
#endif
        break;
    case AsmDialect::Fasm:
        add("fasm", "fasm", native, "flat assembler; the file's format line picks the output");
        break;
    }
    return out;
}

std::string dialect_name(const AsmInfo& info) {
    const bool x86 = info.arch == AsmArch::X86_64 || info.arch == AsmArch::X86;
    if (!x86 && (info.dialect == AsmDialect::Gas || info.dialect == AsmDialect::GasIntel)) return "GAS";
    return to_string(info.dialect);
}

std::string assembly_summary(const AsmInfo& info) {
    return dialect_name(info) + " \xC2\xB7 " + to_string(info.arch);
}

std::string assembly_report(const AsmInfo& info, const Toolchain& toolchain) {
    std::string out = "This looks like " + dialect_name(info) + " syntax for " + to_string(info.arch);
    out += info.entry.empty() ? std::string(".") : ", starting at " + info.entry + ".";
    if (info.dialect_chosen || info.arch_chosen) out += " (Set in language.asm.)";
    const std::vector<AsmBuilder> builders = assembly_builders(info, toolchain);
    bool any = false;
    std::string missing;
    for (const AsmBuilder& b : builders) {
        if (b.found) {
            out += (any ? "; " : " You can build it with ") + b.name + " (" + b.note + ")";
            if (!b.runs_here) out += ", but it can't run on this " + std::string(to_string(host_arch())) + " computer";
            any = true;
        } else if (missing.empty()) {
            missing = b.name;
        }
    }
    if (any) {
        out += ".";
    } else {
        const std::string key = builders.empty() ? std::string() : builders.front().key;
        out += " Nothing here can build it yet: " + (missing.empty() ? std::string("no assembler for it") : missing + " isn't installed") + ".";
        const std::string hint = tool_install_hint(key);
        if (!hint.empty()) out += " To get it, " + hint + ".";
    }
    std::vector<std::string> others;
    for (const char* key : { "nasm", "yasm", "fasm", "masm", "c", "gcc-aarch64", "gcc-arm", "gcc-riscv64", "gcc-mips" }) {
        const Tool* t = toolchain.get(key);
        if (!t) continue;
        std::string what = fs::path(t->path).filename().string();
        if (std::string(key) == "c") what += " (GAS syntax for " + std::string(to_string(host_arch())) + ")";
        else if (std::string(key).rfind("gcc-", 0) == 0) what += " (GAS syntax)";
        else if (std::string(key) == "nasm" || std::string(key) == "yasm") what += " (NASM syntax)";
        others.push_back(what);
    }
    if (!others.empty()) {
        out += " Assemblers on this computer: ";
        for (std::size_t i = 0; i < others.size(); ++i) out += (i ? ", " : "") + others[i];
        out += ".";
    }
    return out;
}

} // namespace canopy
