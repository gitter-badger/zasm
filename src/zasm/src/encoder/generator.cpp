#include "generator.hpp"

#include <Zydis/Zydis.h>

namespace zasm
{
    // FIXME: Duplicate code, move this somewhere.
    static bool isImmediateControlFlow(ZydisMnemonic mnemonic) noexcept
    {
        switch (mnemonic)
        {
            case ZYDIS_MNEMONIC_CALL:
            case ZYDIS_MNEMONIC_JB:
            case ZYDIS_MNEMONIC_JBE:
            case ZYDIS_MNEMONIC_JCXZ:
            case ZYDIS_MNEMONIC_JECXZ:
            case ZYDIS_MNEMONIC_JKNZD:
            case ZYDIS_MNEMONIC_JKZD:
            case ZYDIS_MNEMONIC_JL:
            case ZYDIS_MNEMONIC_JLE:
            case ZYDIS_MNEMONIC_JMP:
            case ZYDIS_MNEMONIC_JNB:
            case ZYDIS_MNEMONIC_JNBE:
            case ZYDIS_MNEMONIC_JNL:
            case ZYDIS_MNEMONIC_JNLE:
            case ZYDIS_MNEMONIC_JNO:
            case ZYDIS_MNEMONIC_JNP:
            case ZYDIS_MNEMONIC_JNS:
            case ZYDIS_MNEMONIC_JNZ:
            case ZYDIS_MNEMONIC_JO:
            case ZYDIS_MNEMONIC_JP:
            case ZYDIS_MNEMONIC_JRCXZ:
            case ZYDIS_MNEMONIC_JS:
            case ZYDIS_MNEMONIC_JZ:
            case ZYDIS_MNEMONIC_LOOP:
            case ZYDIS_MNEMONIC_LOOPE:
            case ZYDIS_MNEMONIC_LOOPNE:
                return true;
        }
        return false;
    }

    InstrGenerator::InstrGenerator(ZydisMachineMode mode) noexcept
        : _decoder(mode)
        , _mode(mode)
    {
    }

    InstrGenerator::Result InstrGenerator::generate(
        Instruction::Attribs attribs, ZydisMnemonic mnemonic, size_t numOps, EncoderOperands&& operands) noexcept
    {
        EncoderResult buf{};

        auto encodeResult = encodeEstimated(buf, _mode, attribs, mnemonic, numOps, operands);
        if (encodeResult != Error::None)
        {
            return zasm::makeUnexpected(encodeResult);
        }

        auto decodeResult = _decoder.decode(buf.data.data(), buf.length, 0);
        if (!decodeResult)
        {
            return zasm::makeUnexpected(decodeResult.error());
        }

        // Exchange back certain operands.
        const auto& decodedInstr = *decodeResult;

        auto newOps = decodedInstr.getOperands();
        const auto opCount = decodedInstr.getOperandCount();
        const auto& vis = decodedInstr.getOperandsVisibility();
        for (size_t i = 0; i < opCount; i++)
        {
            if (vis[i] == Operand::Visibility::Hidden)
                continue;

            const auto& opSrc = operands[i];
            if (opSrc.is<operands::Label>())
            {
                newOps[i] = opSrc;
            }
            else if (const auto* opMem = opSrc.tryAs<operands::Mem>(); opMem != nullptr)
            {
                // FIXME: Handle labels in memory operands.
                auto& decodedMemOp = newOps[i].as<operands::Mem>();

                if (opMem->hasLabel())
                {
                    newOps[i] = operands::Mem(
                        opMem->getBitSize(), decodedMemOp.getSegment(), opMem->getLabel(), opMem->getBase(), opMem->getIndex(),
                        opMem->getScale(), opMem->getDisplacement());
                }
            }
            if (opSrc.is<operands::Imm>())
            {
                if (i == 0 && isImmediateControlFlow(decodedInstr.getId()))
                {
                    newOps[i] = opSrc;
                }
            }
        }

        return Instruction(
            decodedInstr.getAttribs(), decodedInstr.getId(), opCount, newOps, decodedInstr.getAccess(), vis,
            decodedInstr.getOperandsEncoding(), decodedInstr.getFlags(), decodedInstr.getEncoding(), decodedInstr.getCategory(),
            decodedInstr.getLength());
    }

} // namespace zasm