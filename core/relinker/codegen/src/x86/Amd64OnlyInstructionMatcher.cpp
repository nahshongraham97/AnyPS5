#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/DecodedInstruction.hpp>
#include <memory>

namespace Codegen {

namespace {

class Amd64OnlyInstructionMatcher : public IAmd64OnlyInstructionMatcher {
public:
    [[nodiscard]] std::optional<Amd64OnlyMatch> Match(
        const std::uint8_t* data,
        std::size_t length
    ) const override;
};

std::optional<Amd64OnlyMatch> Amd64OnlyInstructionMatcher::Match(
    const std::uint8_t* data,
    std::size_t length
) const {
    const DecodedInstruction instr{data, length};

    if (instr.IsMonitorx()) {
        return Amd64OnlyMatch{"MONITORX", length, {}};
    }

    if (instr.IsMwaitx()) {
        return Amd64OnlyMatch{"MWAITX", length, {}};
    }

    if (instr.IsClzero()) {
        return Amd64OnlyMatch{"CLZERO", length, {}};
    }

    if (instr.IsRdpru()) {
        return Amd64OnlyMatch{"RDPRU", length, {}};
    }

    if (instr.IsMcommit()) {
        return Amd64OnlyMatch{"MCOMMIT", length, {}};
    }

    if (instr.IsExtrq()) return Amd64OnlyMatch{"EXTRQ", length, {}};
    if (instr.IsInsertq()) return Amd64OnlyMatch{"INSERTQ", length, {}};
    if (instr.IsMovntss()) return Amd64OnlyMatch{"MOVNTSS", length, {}};
    if (instr.IsMovntsd()) return Amd64OnlyMatch{"MOVNTSD", length, {}};

    return std::nullopt;
}

}

std::unique_ptr<IAmd64OnlyInstructionMatcher> MakeAmd64OnlyInstructionMatcher() {
    return std::make_unique<Amd64OnlyInstructionMatcher>();
}

}
