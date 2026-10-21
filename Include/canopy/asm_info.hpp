#ifndef CANOPY_ASM_INFO_HPP
#define CANOPY_ASM_INFO_HPP

#include "toolchain.hpp"

#include <string>
#include <vector>

namespace canopy {

enum class AsmDialect {
    Nasm,
    Gas,
    GasIntel,
    Masm,
    Fasm
};

enum class AsmArch {
    X86_64,
    X86,
    Arm64,
    Arm,
    RiscV64,
    Mips
};

struct AsmInfo {
    AsmDialect dialect = AsmDialect::Nasm;
    AsmArch arch = AsmArch::X86_64;
    std::string entry;
    bool dialect_chosen = false;
    bool arch_chosen = false;
};

struct AsmBuilder {
    std::string name;
    std::string key;
    bool found = false;
    bool runs_here = false;
    std::string note;
};

const char* to_string(AsmDialect dialect) noexcept;
const char* to_string(AsmArch arch) noexcept;
AsmArch host_arch() noexcept;
AsmInfo detect_assembly(const std::string& text, const std::string& path);
AsmInfo detect_assembly(const std::string& text, const std::string& path, const std::string& dialect_setting, const std::string& arch_setting);
std::vector<AsmBuilder> assembly_builders(const AsmInfo& info, const Toolchain& toolchain);
std::string dialect_name(const AsmInfo& info);
std::string assembly_summary(const AsmInfo& info);
std::string assembly_report(const AsmInfo& info, const Toolchain& toolchain);

} // namespace canopy

#endif // CANOPY_ASM_INFO_HPP
