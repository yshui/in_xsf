// [AsmJit]
// Complete JIT Assembler for C++ Language.
//
// [License]
// Zlib - See COPYING file in this package.

#define ASMJIT_EXPORTS

// [Dependencies - AsmJit]
#include "../core/assembler.h"
#include "../core/context.h"
#include "../core/cpuinfo.h"
#include "../core/defs.h"
#include "../core/intutil.h"
#include "../core/logger.h"
#include "../core/memorymanager.h"
#include "../core/memorymarker.h"
#include "../core/stringutil.h"

#include "../x86/x86assembler.h"
#include "../x86/x86cpuinfo.h"
#include "../x86/x86defs.h"
#include "../x86/x86operand.h"
#include "../x86/x86util.h"

// [Api-Begin]
#include "../core/apibegin.h"

namespace AsmJit
{

// ============================================================================
// [Constants]
// ============================================================================

enum { kMaxCommentLength = 80 };

// ============================================================================
// [AsmJit::X64TrampolineWriter]
// ============================================================================

#ifdef ASMJIT_X64
//! @brief Class used to determine size of trampoline and as trampoline writer.
struct X64TrampolineWriter
{
	// Size of trampoline
	enum
	{
		kSizeJmp = 6,
		kSizeAddr = 8,
		kSizeTotal = kSizeJmp + kSizeAddr
	};

	// Write trampoline into code at address @a code that will jump to @a target.
	static void writeTrampoline(uint8_t *code, uint64_t target)
	{
		code[0] = 0xFF; // Jmp OpCode.
		code[1] = 0x25; // ModM (RIP addressing).
		reinterpret_cast<uint32_t *>(code + 2)[0] = 0; // Offset (zero).
		reinterpret_cast<uint64_t *>(code + kSizeJmp)[0] = target; // Absolute address.
	}
};
#endif // ASMJIT_X64

// ============================================================================
// [AsmJit::X86Assembler - Construction / Destruction]
// ============================================================================

X86Assembler::X86Assembler(Context *context) : Assembler(context)
{
	this->_properties = IntUtil::maskFromIndex(kX86PropertyOptimizedAlign);
}

X86Assembler::~X86Assembler()
{
}

// ============================================================================
// [AsmJit::X86Assembler - Buffer - Setters (X86-Extensions)]
// ============================================================================

void X86Assembler::setVarAt(size_t pos, sysint_t i, uint8_t isUnsigned, uint32_t size)
{
	if (size == 1 && !isUnsigned)
		this->setByteAt(pos, static_cast<int8_t>(i));
	else if (size == 1 && isUnsigned)
		this->setByteAt(pos, static_cast<uint8_t>(i));
	else if (size == 2 && !isUnsigned)
		this->setWordAt(pos, static_cast<int16_t>(i));
	else if (size == 2 && isUnsigned)
		this->setWordAt(pos, static_cast<uint16_t>(i));
	else if (size == 4 && !isUnsigned)
		this->setDWordAt(pos, static_cast<int32_t>(i));
	else if (size == 4 && isUnsigned)
		this->setDWordAt(pos, static_cast<uint32_t>(i));
#ifdef ASMJIT_X64
	else if (size == 8 && !isUnsigned)
		this->setQWordAt(pos, static_cast<int64_t>(i));
	else if (size == 8 && isUnsigned)
		this->setQWordAt(pos, static_cast<uint64_t>(i));
#endif // ASMJIT_X64
	else
		ASMJIT_ASSERT(0);
}

// ============================================================================
// [AsmJit::X86Assembler - Emit]
// ============================================================================

void X86Assembler::_emitModM(uint8_t opReg, const Mem &mem, sysint_t immSize)
{
	ASMJIT_ASSERT(mem.getType() == kOperandMem);

	uint8_t baseReg = mem.getBase() & 0x7;
	uint8_t indexReg = mem.getIndex() & 0x7;
	sysint_t disp = mem.getDisplacement();
	uint32_t shift = mem.getShift();

	if (mem.getMemType() == kOperandMemNative)
	{
		// [base + displacemnt]
		if (!mem.hasIndex())
		{
			// ESP/RSP/R12 == 4
			if (baseReg == 4)
			{
				uint8_t mod = 0;

				if (disp)
					mod = IntUtil::isInt8(disp) ? 1 : 2;

				this->_emitMod(mod, opReg, 4);
				this->_emitSib(0, 4, 4);

				if (disp)
				{
					if (IntUtil::isInt8(disp))
						this->_emitByte(static_cast<int8_t>(disp));
					else
						this->_emitInt32(static_cast<int32_t>(disp));
				}
			}
			// EBP/RBP/R13 == 5
			else if (baseReg != 5 && !disp)
				this->_emitMod(0, opReg, baseReg);
			else if (IntUtil::isInt8(disp))
			{
				this->_emitMod(1, opReg, baseReg);
				this->_emitByte(static_cast<int8_t>(disp));
			}
			else
			{
				this->_emitMod(2, opReg, baseReg);
				this->_emitInt32(static_cast<int32_t>(disp));
			}
		}
		// [base + index * scale + displacemnt]
		else
		{
			//ASMJIT_ASSERT(indexReg != RID_ESP);

			// EBP/RBP/R13 == 5
			if (baseReg != 5 && !disp)
			{
				this->_emitMod(0, opReg, 4);
				this->_emitSib(shift, indexReg, baseReg);
			}
			else if (IntUtil::isInt8(disp))
			{
				this->_emitMod(1, opReg, 4);
				this->_emitSib(shift, indexReg, baseReg);
				this->_emitByte(static_cast<int8_t>(disp));
			}
			else
			{
				this->_emitMod(2, opReg, 4);
				this->_emitSib(shift, indexReg, baseReg);
				this->_emitInt32(static_cast<int32_t>(disp));
			}
		}
	}
	// Address                       | 32-bit mode | 64-bit mode
	// ------------------------------+-------------+---------------
	// [displacement]                |   ABSOLUTE  | RELATIVE (RIP)
	// [index * scale + displacemnt] |   ABSOLUTE  | ABSOLUTE (ZERO EXTENDED)
	else
	{
		// - In 32-bit mode the absolute addressing model is used.
		// - In 64-bit mode the relative addressing model is used together with
		//   the absolute addressing. Main problem is that if instruction
		//   contains SIB then relative addressing (RIP) is not possible.

#ifdef ASMJIT_X86
		if (mem.hasIndex())
		{
			// ASMJIT_ASSERT(mem.getMemIndex() != 4); // ESP/RSP == 4
			this->_emitMod(0, opReg, 4);
			this->_emitSib(shift, indexReg, 5);
		}
		else
			this->_emitMod(0, opReg, 5);

		// X86 uses absolute addressing model, all relative addresses will be
		// relocated to absolute ones.
		if (mem.getMemType() == kOperandMemLabel)
		{
			LabelData &l_data = this->_labels[mem._mem.base & kOperandIdValueMask];
			RelocData r_data;
			uint32_t relocId = this->_relocData.size();

			// Relative addressing will be relocated to absolute address.
			r_data.type = kRelocRelToAbs;
			r_data.size = 4;
			r_data.offset = this->getOffset();
			r_data.destination = disp;

			if (l_data.offset != -1)
			{
				// Bound label.
				r_data.destination += l_data.offset;

				// Add a dummy DWORD.
				this->_emitInt32(0);
			}
			else
				// Non-bound label.
				this->_emitDisplacement(l_data, -4 - immSize, 4)->relocId = relocId;

			this->_relocData.push_back(r_data);
		}
		else
			// Absolute address
			this->_emitInt32((int32_t)((uint8_t*)mem._mem.target + disp));
#else
		// X64 uses relative addressing model
		if (mem.getMemType() == kOperandMemLabel)
		{
			LabelData &l_data = this->_labels[mem._mem.base & kOperandIdValueMask];

			if (mem.hasIndex())
			{
				// Indexing is not possible.
				this->setError(kErrorIllegalAddressing);
				return;
			}

			// Relative address (RIP +/- displacement).
			this->_emitMod(0, opReg, 5);

			disp -= 4 + immSize;

			if (l_data.offset != -1)
			{
				// Bound label.
				disp += getOffset() - l_data.offset;

				// Displacement is known.
				this->_emitInt32(static_cast<int32_t>(disp));
			}
			else
				// Non-bound label.
				this->_emitDisplacement(l_data, disp, 4);
		}
		else
		{
			// Absolute address (truncated to 32-bits), this kind of address requires
			// SIB byte (4).
			this->_emitMod(0, opReg, 4);

			if (mem.hasIndex())
				//ASMJIT_ASSERT(mem.getMemIndex() != 4); // ESP/RSP == 4
				this->_emitSib(shift, indexReg, 5);
			else
				this->_emitSib(0, 4, 5);

			// Truncate to 32-bits.
			sysuint_t target = (sysuint_t)((uint8_t*)mem._mem.target + disp);

			if (target > static_cast<sysuint_t>(0xFFFFFFFF))
			{
				if (this->_logger)
					this->_logger->logString("*** ASSEMBER WARNING - Absolute address truncated to 32-bits.\n");
				target &= 0xFFFFFFFF;
			}

			this->_emitInt32(static_cast<int32_t>(static_cast<uint32_t>(target)));
		}
#endif // ASMJIT_X64
	}
}

void X86Assembler::_emitModRM(uint8_t opReg, const Operand &op, sysint_t immSize)
{
	ASMJIT_ASSERT(op.getType() == kOperandReg || op.getType() == kOperandMem);

	if (op.getType() == kOperandReg)
		this->_emitModR(opReg, reinterpret_cast<const Reg &>(op).getRegCode());
	else
		this->_emitModM(opReg, reinterpret_cast<const Mem &>(op), immSize);
}

void X86Assembler::_emitSegmentPrefix(const Operand &rm)
{
	static const uint8_t segmentCode[] =
	{
		0x26, // ES
		0x2E, // SS
		0x36, // SS
		0x3E, // DS
		0x64, // FS
		0x65 // GS
	};

	if (!rm.isMem())
		return;

	uint32_t seg = reinterpret_cast<const Mem &>(rm).getSegment();
	if (seg >= kX86RegNumSeg)
		return;

	this->_emitByte(segmentCode[seg]);
}

void X86Assembler::_emitX86Inl(uint32_t opCode, uint8_t i16bit, uint8_t rexw, uint8_t reg, bool forceRexPrefix)
{
	// 16-bit prefix.
	if (i16bit)
		this->_emitByte(0x66);

	// Instruction prefix.
	if (opCode & 0xFF000000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0xFF000000) >> 24));

	// REX prefix.
#ifdef ASMJIT_X64
	this->_emitRexR(rexw, 0, reg, forceRexPrefix);
#endif // ASMJIT_X64

	// Instruction opcodes.
	if (opCode & 0x00FF0000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x00FF0000) >> 16));
	if (opCode & 0x0000FF00)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x0000FF00) >> 8));
	this->_emitByte(static_cast<uint8_t>(opCode & 0x000000FF) + (reg & 0x7));
}

void X86Assembler::_emitX86RM(uint32_t opCode, uint8_t i16bit, uint8_t rexw, uint8_t o, const Operand &op, sysint_t immSize, bool forceRexPrefix)
{
	// 16-bit prefix.
	if (i16bit)
		this->_emitByte(0x66);

	// Segment prefix.
	this->_emitSegmentPrefix(op);

	// Instruction prefix.
	if (opCode & 0xFF000000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0xFF000000) >> 24));

	// REX prefix.
#ifdef ASMJIT_X64
	this->_emitRexRM(rexw, o, op, forceRexPrefix);
#endif // ASMJIT_X64

	// Instruction opcodes.
	if (opCode & 0x00FF0000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x00FF0000) >> 16));
	if (opCode & 0x0000FF00)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x0000FF00) >> 8));
	this->_emitByte(static_cast<uint8_t>(opCode & 0x000000FF));

	// Mod R/M.
	this->_emitModRM(o, op, immSize);
}

void X86Assembler::_emitFpu(uint32_t opCode)
{
	this->_emitOpCode(opCode);
}

void X86Assembler::_emitFpuSTI(uint32_t opCode, uint32_t sti)
{
	// Illegal stack offset.
	ASMJIT_ASSERT(0 <= sti && sti < 8);
	this->_emitOpCode(opCode + sti);
}

void X86Assembler::_emitFpuMEM(uint32_t opCode, uint8_t opReg, const Mem& mem)
{
	// Segment prefix.
	this->_emitSegmentPrefix(mem);

	// Instruction prefix.
	if (opCode & 0xFF000000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0xFF000000) >> 24));

	// REX prefix.
#ifdef ASMJIT_X64
	this->_emitRexRM(0, opReg, mem, false);
#endif // ASMJIT_X64

	// Instruction opcodes.
	if (opCode & 0x00FF0000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x00FF0000) >> 16));
	if (opCode & 0x0000FF00)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x0000FF00) >> 8));
	this->_emitByte(static_cast<uint8_t>(opCode & 0x000000FF));
	this->_emitModM(opReg, mem, 0);
}

void X86Assembler::_emitMmu(uint32_t opCode, uint8_t rexw, uint8_t opReg, const Operand &src, sysint_t immSize)
{
	// Segment prefix.
	this->_emitSegmentPrefix(src);

	// Instruction prefix.
	if (opCode & 0xFF000000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0xFF000000) >> 24));

	// REX prefix.
#ifdef ASMJIT_X64
	this->_emitRexRM(rexw, opReg, src, false);
#endif // ASMJIT_X64

	// Instruction opcodes.
	if (opCode & 0x00FF0000)
		this->_emitByte(static_cast<uint8_t>((opCode & 0x00FF0000) >> 16));

	// No checking, MMX/SSE instructions have always two opcodes or more.
	this->_emitByte(static_cast<uint8_t>((opCode & 0x0000FF00) >> 8));
	this->_emitByte(static_cast<uint8_t>(opCode & 0x000000FF));

	if (src.isReg())
		this->_emitModR(opReg, reinterpret_cast<const Reg &>(src).getRegCode());
	else
		this->_emitModM(opReg, reinterpret_cast<const Mem &>(src), immSize);
}

X86Assembler::LabelLink *X86Assembler::_emitDisplacement(LabelData &l_data, sysint_t inlinedDisplacement, int size)
{
	ASMJIT_ASSERT(l_data.offset == -1);
	ASMJIT_ASSERT(size == 1 || size == 4);

	// Chain with label.
	LabelLink *link = this->_newLabelLink();
	link->prev = l_data.links;
	link->offset = this->getOffset();
	link->displacement = inlinedDisplacement;

	l_data.links = link;

	// Emit label size as dummy data.
	if (size == 1)
		this->_emitByte(0x01);
	else // if (size == 4)
		this->_emitDWord(0x04040404);

	return link;
}

void X86Assembler::_emitJmpOrCallReloc(uint32_t instruction, void *target)
{
	RelocData rd;

	rd.type = kRelocTrampoline;

#ifdef ASMJIT_X64
	// If we are compiling in 64-bit mode, we can use trampoline if relative jump
	// is not possible.
	this->_trampolineSize += X64TrampolineWriter::kSizeTotal;
#endif // ARCHITECTURE_SPECIFIC

	rd.size = 4;
	rd.offset = this->getOffset();
	rd.address = target;

	this->_relocData.push_back(rd);

	// Emit dummy 32-bit integer (will be overwritten by relocCode()).
	this->_emitInt32(0);
}

//! @internal
//!
//! @brief Get whether the extended register (additional eight registers
//! introduced by 64-bit mode) is used.
static inline bool X86Assembler_isExtRegisterUsed(const Operand &op)
{
	// Hacky, but correct.
	// - If operand type is register then extended register is register with
	//   index 8 and greater (8 to 15 inclusive).
	// - If operand type is memory operand then we need to take care about
	//   label (in _mem.base) and kInvalidValue, we just decrement the value
	//   by 8 and check if it's at interval 0 to 7 inclusive (if it's there
	//   then it's extended register.
	return (op.isReg() && (op._reg.code & kRegIndexMask)  >= 8U) || (op.isMem() && (((static_cast<uint32_t>(op._mem.base) - 8U) < 8U) || ((static_cast<uint32_t>(op._mem.index) - 8U) < 8U)));
}

// Logging helpers.
static const char *AssemblerX86_operandSize[] =
{
	nullptr,
	"byte ptr ",
	"word ptr ",
	nullptr,
	"dword ptr ",
	nullptr,
	nullptr,
	nullptr,
	"qword ptr ",
	nullptr,
	"tword ptr ",
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	"dqword ptr "
};

static const char X86Assembler_segmentName[] =
	"es:\0"
	"cs:\0"
	"ss:\0"
	"ds:\0"
	"fs:\0"
	"gs:\0"
	"\0\0\0\0";

static char *X86Assembler_dumpInstructionName(char *buf, uint32_t code)
{
	ASMJIT_ASSERT(code < _kX86InstCount);
	return StringUtil::copy(buf, x86InstInfo[code].getName());
}

char *X86Assembler_dumpRegister(char *buf, uint32_t type, uint32_t index)
{
	// NE == Not-Encodable.
	const char reg8l[] = "al\0\0" "cl\0\0" "dl\0\0" "bl\0\0" "spl\0"  "bpl\0"  "sil\0"  "dil\0" ;
	const char reg8h[] = "ah\0\0" "ch\0\0" "dh\0\0" "bh\0\0" "NE\0\0" "NE\0\0" "NE\0\0" "NE\0\0";
	const char reg16[] = "ax\0\0" "cx\0\0" "dx\0\0" "bx\0\0" "sp\0\0" "bp\0\0" "si\0\0" "di\0\0";

	switch (type)
	{
		case kX86RegTypeGpbLo:
			if (index < 8)
				return StringUtil::copy(buf, &reg8l[index * 4]);

			*buf++ = 'r';
			goto _EmitID;

		case kX86RegTypeGpbHi:
			if (index < 4)
				return StringUtil::copy(buf, &reg8h[index * 4]);

		_EmitNE:
			return StringUtil::copy(buf, "NE");

		case kX86RegTypeGpw:
			if (index < 8)
				return StringUtil::copy(buf, &reg16[index * 4]);

			*buf++ = 'r';
			buf = StringUtil::utoa(buf, index);
			*buf++ = 'w';
			return buf;

		case kX86RegTypeGpd:
			if (index < 8)
			{
				*buf++ = 'e';
				return StringUtil::copy(buf, &reg16[index * 4]);
			}

			*buf++ = 'r';
			buf = StringUtil::utoa(buf, index);
			*buf++ = 'd';
			return buf;

		case kX86RegTypeGpq:
			*buf++ = 'r';

			if (index < 8)
				return StringUtil::copy(buf, &reg16[index * 4]);

		_EmitID:
			return StringUtil::utoa(buf, index);

		case kX86RegTypeX87:
			*buf++ = 's';
			*buf++ = 't';
			goto _EmitID;

		case kX86RegTypeMm:
			*buf++ = 'm';
			*buf++ = 'm';
			goto _EmitID;

		case kX86RegTypeXmm:
			*buf++ = 'x';
			*buf++ = 'm';
			*buf++ = 'm';
			goto _EmitID;

		case kX86RegTypeYmm:
			*buf++ = 'y';
			*buf++ = 'm';
			*buf++ = 'm';
			goto _EmitID;

		case kX86RegTypeSeg:
			if (index < kX86RegNumSeg)
				return StringUtil::copy(buf, &X86Assembler_segmentName[index * 4], 2);

			goto _EmitNE;

		default:
			return buf;
	}
}

char *X86Assembler_dumpOperand(char *buf, const Operand *op, uint32_t memRegType, uint32_t loggerFlags)
{
	if (op->isReg())
	{
		const Reg &reg = reinterpret_cast<const Reg &>(*op);
		return X86Assembler_dumpRegister(buf, reg.getRegType(), reg.getRegIndex());
	}
	else if (op->isMem())
	{
		const Mem &mem = reinterpret_cast<const Mem &>(*op);
		uint32_t seg = mem.getSegment();

		bool isAbsolute = false;

		if (op->getSize() <= 16)
			buf = StringUtil::copy(buf, AssemblerX86_operandSize[op->getSize()]);

		if (seg < kX86RegNumSeg)
			buf = StringUtil::copy(buf, &X86Assembler_segmentName[seg * 4]);

		*buf++ = '[';

		switch (mem.getMemType())
		{
			case kOperandMemNative:
				// [base + index << shift + displacement]
				buf = X86Assembler_dumpRegister(buf, memRegType, mem.getBase());
				break;
			case kOperandMemLabel:
				// [label + index << shift + displacement]
				buf += sprintf(buf, "L.%u", mem.getBase() & kOperandIdValueMask);
				break;
			case kOperandMemAbsolute:
				// [absolute]
				isAbsolute = true;
				buf = StringUtil::utoa(buf, reinterpret_cast<sysuint_t>(mem.getTarget()) + mem.getDisplacement(), 16);
		}

		if (mem.hasIndex())
		{
			buf = StringUtil::copy(buf, " + ");
			buf = X86Assembler_dumpRegister(buf, memRegType, mem.getIndex());

			if (mem.getShift())
			{
				buf = StringUtil::copy(buf, " * ");
				*buf++ = "1248"[mem.getShift() & 3];
			}
		}

		if (mem.getDisplacement() && !isAbsolute)
		{
			sysint_t d = mem.getDisplacement();
			uint32_t base = 10;
			char sign = '+';

			if (d < 0)
			{
				d = -d;
				sign = '-';
			}

			buf[0] = ' ';
			buf[1] = sign;
			buf[2] = ' ';
			buf += 3;

			if ((loggerFlags & kLoggerOutputHexDisplacement) && d > 9)
			{
				buf[0] = '0';
				buf[1] = 'x';
				buf += 2;
				base = 16;
			}

			buf = StringUtil::utoa(buf, static_cast<uintptr_t>(d), base);
		}

		*buf++ = ']';
		return buf;
	}
	else if (op->isImm())
	{
		const Imm &i = reinterpret_cast<const Imm &>(*op);

		sysuint_t value = i.getUValue();
		uint32_t base = 10;

		if ((loggerFlags & kLoggerOutputHexImmediate) && value > 9)
			base = 16;

		if (i.isUnsigned() || base == 16)
			return StringUtil::utoa(buf, value, base);
		else
			return StringUtil::itoa(buf, static_cast<sysint_t>(value), base);
	}
	else if (op->isLabel())
		return buf + sprintf(buf, "L.%u", op->getId() & kOperandIdValueMask);
	else
		return StringUtil::copy(buf, "None");
}

static char *X86Assembler_dumpInstruction(char *buf, uint32_t code, uint32_t emitOptions, const Operand *o0, const Operand *o1, const Operand *o2, uint32_t memRegType, uint32_t loggerFlags)
{
	// Rex, lock, and short prefix.
	if (emitOptions & kX86EmitOptionRex)
		buf = StringUtil::copy(buf, "rex ", 4);

	if (emitOptions & kX86EmitOptionLock)
		buf = StringUtil::copy(buf, "lock ", 5);

	if (emitOptions & kX86EmitOptionShortJump)
		buf = StringUtil::copy(buf, "short ", 6);

	// Dump instruction name.
	buf = X86Assembler_dumpInstructionName(buf, code);

	// Dump operands.
	if (!o0->isNone())
	{
		*buf++ = ' ';
		buf = X86Assembler_dumpOperand(buf, o0, memRegType, loggerFlags);
	}
	if (!o1->isNone())
	{
		*buf++ = ',';
		*buf++ = ' ';
		buf = X86Assembler_dumpOperand(buf, o1, memRegType, loggerFlags);
	}
	if (!o2->isNone())
	{
		*buf++ = ',';
		*buf++ = ' ';
		buf = X86Assembler_dumpOperand(buf, o2, memRegType, loggerFlags);
	}

	return buf;
}

static char *X86Assembler_dumpComment(char *buf, size_t len, const uint8_t *binaryData, size_t binaryLen, const char *comment)
{
	size_t currentLength = len;
	size_t commentLength = comment ? strnlen(comment, kMaxCommentLength) : 0;

	if (binaryLen || commentLength)
	{
		size_t align = 32;
		char sep = ';';

		for (size_t i = !binaryLen; i < 2; ++i)
		{
			char *bufBegin = buf;

			// Append align.
			if (currentLength < align) 
				buf = StringUtil::fill(buf, ' ', align - currentLength);

			// Append separator.
			if (sep)
			{
				*buf++ = sep;
				*buf++ = ' ';
			}

			// Append binary data or comment.
			if (!i)
			{
				buf = StringUtil::hex(buf, binaryData, binaryLen);
				if (!commentLength)
					break;
			}
			else
				buf = StringUtil::copy(buf, comment, commentLength);

			currentLength += static_cast<size_t>(buf - bufBegin);
			align += 18;
			sep = '|';
		}
	}

	*buf++ = '\n';
	return buf;
}

static const _OpReg _patchedHiRegs[] =
{
	// Operand   |Size|Reserved0|Reserved1| OperandId    | RegisterCode          |
	// ----------+----+---------+---------+--------------+-----------------------+
	{ kOperandReg, 1, {0        ,0       }, kInvalidValue, kX86RegTypeGpbLo | 4 },
	{ kOperandReg, 1, {0        ,0       }, kInvalidValue, kX86RegTypeGpbLo | 5 },
	{ kOperandReg, 1, {0        ,0       }, kInvalidValue, kX86RegTypeGpbLo | 6 },
	{ kOperandReg, 1, {0        ,0       }, kInvalidValue, kX86RegTypeGpbLo | 7 }
};

void X86Assembler::_emitInstruction(uint32_t code)
{
	this->_emitInstruction(code, &noOperand, &noOperand, &noOperand);
}

void X86Assembler::_emitInstruction(uint32_t code, const Operand *o0)
{
	this->_emitInstruction(code, o0, &noOperand, &noOperand);
}

void X86Assembler::_emitInstruction(uint32_t code, const Operand *o0, const Operand *o1)
{
	this->_emitInstruction(code, o0, o1, &noOperand);
}

void X86Assembler::_emitInstruction(uint32_t code, const Operand *o0, const Operand *o1, const Operand *o2)
{
	ASMJIT_ASSERT(o0);
	ASMJIT_ASSERT(o1);
	ASMJIT_ASSERT(o2);

	const Operand *_loggerOperands[3];

	uint32_t bLoHiUsed = 0;
#ifdef ASMJIT_X86
	uint32_t forceRexPrefix = false;
#else
	uint32_t forceRexPrefix = this->_emitOptions & kX86EmitOptionRex;
#endif
	uint32_t memRegType = kX86RegTypeGpz;

#ifdef ASMJIT_DEBUG
	bool assertIllegal = false;
#endif // ASMJIT_DEBUG

	const Imm *immOperand = nullptr;
	uint32_t immSize = 0;

#define _FINISHED() \
	goto _End

#define _FINISHED_IMMEDIATE(_Operand_, _Size_) \
	do \
	{ \
		immOperand = reinterpret_cast<const Imm *>(_Operand_); \
		immSize = (_Size_); \
		goto _EmitImmediate; \
	} while (0)

	// Convert operands to kOperandNone if needed.
	if (o0->isReg())
		bLoHiUsed |= o0->_reg.code & (kX86RegTypeGpbLo | kX86RegTypeGpbHi);
	if (o1->isReg())
		bLoHiUsed |= o1->_reg.code & (kX86RegTypeGpbLo | kX86RegTypeGpbHi);
	if (o2->isReg())
		bLoHiUsed |= o2->_reg.code & (kX86RegTypeGpbLo | kX86RegTypeGpbHi);

	size_t beginOffset = this->getOffset();
	const X86InstInfo *id = &x86InstInfo[code];

	if (code >= _kX86InstCount)
	{
		this->setError(kErrorUnknownInstruction);
		goto _Cleanup;
	}

	// Check if register operand is BPL, SPL, SIL, DIL and do action that depends
	// to current mode:
	//   - 64-bit: - Force REX prefix.
	//
	// Check if register operand is AH, BH, CH or DH and do action that depends
	// to current mode:
	//   - 32-bit: - Patch operand index (index += 4), because we are using
	//               different index what is used in opcode.
	//   - 64-bit: - Check whether there is REX prefix and raise error if it is.
	//             - Do the same as in 32-bit mode - patch register index.
	//
	// NOTE: This is a hit hacky, but I added this to older code-base and I have
	// no energy to rewrite it. Maybe in future all of this can be cleaned up!
	if (bLoHiUsed | forceRexPrefix)
	{
		_loggerOperands[0] = o0;
		_loggerOperands[1] = o1;
		_loggerOperands[2] = o2;

#ifdef ASMJIT_X64
		// Check if there is register that makes this instruction un-encodable.

		forceRexPrefix |= static_cast<uint32_t>(X86Assembler_isExtRegisterUsed(*o0));
		forceRexPrefix |= static_cast<uint32_t>(X86Assembler_isExtRegisterUsed(*o1));
		forceRexPrefix |= static_cast<uint32_t>(X86Assembler_isExtRegisterUsed(*o2));

		if (o0->isRegType(kX86RegTypeGpbLo) && (o0->_reg.code & kRegIndexMask) >= 4)
			forceRexPrefix = true;
		else if (o1->isRegType(kX86RegTypeGpbLo) && (o1->_reg.code & kRegIndexMask) >= 4)
			forceRexPrefix = true;
		else if (o2->isRegType(kX86RegTypeGpbLo) && (o2->_reg.code & kRegIndexMask) >= 4)
			forceRexPrefix = true;

		if ((bLoHiUsed & kX86RegTypeGpbHi) && forceRexPrefix)
			goto _IllegalInstruction;
#endif // ASMJIT_X64

		// Patch GPB.HI operand index.
		if (bLoHiUsed & kX86RegTypeGpbHi)
		{
			if (o0->isRegType(kX86RegTypeGpbHi))
				o0 = reinterpret_cast<const Operand *>(&_patchedHiRegs[o0->_reg.code & kRegIndexMask]);
			if (o1->isRegType(kX86RegTypeGpbHi))
				o1 = reinterpret_cast<const Operand *>(&_patchedHiRegs[o1->_reg.code & kRegIndexMask]);
			if (o2->isRegType(kX86RegTypeGpbHi))
				o2 = reinterpret_cast<const Operand *>(&_patchedHiRegs[o2->_reg.code & kRegIndexMask]);
		}
	}

	// Check for buffer space (and grow if needed).
	if (!this->canEmit())
		goto _Cleanup;

	if (this->_emitOptions & kX86EmitOptionLock)
	{
		if (!id->isLockable())
			goto _IllegalInstruction;
		this->_emitByte(0xF0);
	}

	switch (id->getGroup())
	{
		case kX86InstGroupNone:
			_FINISHED();

		case kX86InstGroupEmit:
			this->_emitOpCode(id->_opCode[0]);
			_FINISHED();

		case kX86InstGroupArith:
		{
			uint32_t opCode = id->_opCode[0];
			uint8_t opReg = static_cast<uint8_t>(id->_opCodeR);

			// Mem <- Reg
			if (o0->isMem() && o1->isReg())
			{
				this->_emitX86RM(opCode + (o1->getSize() != 1), o1->getSize() == 2, o1->getSize() == 8, reinterpret_cast<const GpReg &>(*o1).getRegCode(), reinterpret_cast<const Operand &>(*o0), 0, forceRexPrefix);
				_FINISHED();
			}

			// Reg <- Reg|Mem
			if (o0->isReg() && o1->isRegMem())
			{
				this->_emitX86RM(opCode + 2 + (o0->getSize() != 1), o0->getSize() == 2, o0->getSize() == 8, reinterpret_cast<const GpReg &>(*o0).getRegCode(), reinterpret_cast<const Operand &>(*o1), 0, forceRexPrefix);
				_FINISHED();
			}

			// Alternate Form - AL, AX, EAX, RAX.
			if (o0->isRegIndex(0) && o1->isImm())
			{
				if (o0->getSize() == 1 || !IntUtil::isInt8(static_cast<const Imm *>(o1)->getValue()))
				{
					if (o0->getSize() == 2)
						this->_emitByte(0x66); // 16-bit.
					else if (o0->getSize() == 8)
						this->_emitByte(0x48); // REX.W.

					this->_emitByte((opReg << 3) | (0x04 + (o0->getSize() != 1)));
					_FINISHED_IMMEDIATE(o1, IntUtil::_min<uint32_t>(o0->getSize(), 4));
				}
			}

			if (o0->isRegMem() && o1->isImm())
			{
				const Imm &imm = reinterpret_cast<const Imm &>(*o1);
				immSize = IntUtil::isInt8(imm.getValue()) ? 1 : IntUtil::_min(o0->getSize(), 4u);

				this->_emitX86RM(id->_opCode[1] + (o0->getSize() != 1 ? (immSize != 1 ? 1 : 3) : 0), o0->getSize() == 2, o0->getSize() == 8, opReg, reinterpret_cast<const Operand &>(*o0), immSize, forceRexPrefix);
				_FINISHED_IMMEDIATE(&imm, immSize);
			}

			break;
		}

		case kX86InstGroupBSwap:
			if (o0->isReg())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);

#ifdef ASMJIT_X64
				this->_emitRexR(dst.getRegType() == kX86RegTypeGpq, 1, dst.getRegCode(), forceRexPrefix);
#endif // ASMJIT_X64
				this->_emitByte(0x0F);
				this->_emitModR(1, dst.getRegCode());
				_FINISHED();
			}

			break;

		case kX86InstGroupBTest:
			if (o0->isRegMem() && o1->isReg())
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);
				const GpReg &src = reinterpret_cast<const GpReg &>(*o1);

				this->_emitX86RM(id->_opCode[0], src.isRegType(kX86RegTypeGpw), src.isRegType(kX86RegTypeGpq), src.getRegCode(), dst, 0, forceRexPrefix);
				_FINISHED();
			}

			if (o0->isRegMem() && o1->isImm())
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);

				this->_emitX86RM(id->_opCode[1], dst.getSize() == 2, dst.getSize() == 8, static_cast<uint8_t>(id->_opCodeR), dst, 1, forceRexPrefix);
				_FINISHED_IMMEDIATE(o1, 1);
			}

			break;

		case kX86InstGroupCall:
			if (o0->isRegTypeMem(kX86RegTypeGpz))
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);
				this->_emitX86RM(0xFF, 0, 0, 2, dst, 0, forceRexPrefix);
				_FINISHED();
			}

			if (o0->isImm())
			{
				const Imm &imm = reinterpret_cast<const Imm &>(*o0);
				this->_emitByte(0xE8);
				this->_emitJmpOrCallReloc(kX86InstGroupCall, reinterpret_cast<void *>(imm.getValue()));
				_FINISHED();
			}

			if (o0->isLabel())
			{
				LabelData &l_data = this->_labels[reinterpret_cast<const Label *>(o0)->getId() & kOperandIdValueMask];

				if (l_data.offset != -1)
				{
					// Bound label.
					static const sysint_t rel32_size = 5;
					sysint_t offs = l_data.offset - this->getOffset();

					ASMJIT_ASSERT(offs <= 0);

					this->_emitByte(0xE8);
					this->_emitInt32(static_cast<int32_t>(offs - rel32_size));
				}
				else
				{
					// Non-bound label.
					this->_emitByte(0xE8);
					this->_emitDisplacement(l_data, -4, 4);
				}
				_FINISHED();
			}

			break;

		case kX86InstGroupCrc32:
			if (o0->isReg() && o1->isRegMem())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				const Operand &src = reinterpret_cast<const Operand &>(*o1);
				ASMJIT_ASSERT(dst.getRegType() == kX86RegTypeGpd || dst.getRegType() == kX86RegTypeGpq);

				this->_emitX86RM(id->_opCode[0] + (src.getSize() != 1), src.getSize() == 2, dst.getRegType() == 8, dst.getRegCode(), src, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupEnter:
			if (o0->isImm() && o1->isImm())
			{
				this->_emitByte(0xC8);
				this->_emitWord(static_cast<uint16_t>(static_cast<uintptr_t>(reinterpret_cast<const Imm &>(*o2).getValue())));
				this->_emitByte(static_cast<uint8_t>(static_cast<uintptr_t>(reinterpret_cast<const Imm &>(*o1).getValue())));
				_FINISHED();
			}

			break;

		case kX86InstGroupIMul:
			// 1 operand
			if (o0->isRegMem() && o1->isNone() && o2->isNone())
			{
				const Operand &src = reinterpret_cast<const Operand &>(*o0);
				this->_emitX86RM(0xF6 + (src.getSize() != 1), src.getSize() == 2, src.getSize() == 8, 5, src, 0, forceRexPrefix);
				_FINISHED();
			}
			// 2 operands
			else if (o0->isReg() && !o1->isNone() && o2->isNone())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				ASMJIT_ASSERT(!dst.isRegType(kX86RegTypeGpw));

				if (o1->isRegMem())
				{
					const Operand &src = reinterpret_cast<const Operand &>(*o1);

					this->_emitX86RM(0x0FAF, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), src, 0, forceRexPrefix);
					_FINISHED();
				}
				else if (o1->isImm())
				{
					const Imm &imm = reinterpret_cast<const Imm &>(*o1);

					if (IntUtil::isInt8(imm.getValue()))
					{
						this->_emitX86RM(0x6B, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), dst, 1, forceRexPrefix);
						_FINISHED_IMMEDIATE(&imm, 1);
					}
					else
					{
						immSize = dst.isRegType(kX86RegTypeGpw) ? 2 : 4;
						this->_emitX86RM(0x69, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), dst, immSize, forceRexPrefix);
						_FINISHED_IMMEDIATE(&imm, immSize);
					}
				}
			}
			// 3 operands
			else if (o0->isReg() && o1->isRegMem() && o2->isImm())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				const Operand &src = reinterpret_cast<const Operand &>(*o1);
				const Imm &imm = reinterpret_cast<const Imm &>(*o2);

				if (IntUtil::isInt8(imm.getValue()))
				{
					this->_emitX86RM(0x6B, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), src, 1, forceRexPrefix);
					_FINISHED_IMMEDIATE(&imm, 1);
				}
				else
				{
					immSize = dst.isRegType(kX86RegTypeGpw) ? 2 : 4;
					this->_emitX86RM(0x69, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), src, immSize, forceRexPrefix);
					_FINISHED_IMMEDIATE(&imm, immSize);
				}
			}

			break;

		case kX86InstGroupIncDec:
			if (o0->isRegMem())
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);

				// INC [r16|r32] in 64-bit mode is not encodable.
#ifdef ASMJIT_X86
				if (dst.isReg() && (dst.isRegType(kX86RegTypeGpw) || dst.isRegType(kX86RegTypeGpd)))
				{
					this->_emitX86Inl(id->_opCode[0], dst.isRegType(kX86RegTypeGpw), 0, reinterpret_cast<const Reg&>(dst).getRegCode(), false);
					_FINISHED();
				}
#endif // ASMJIT_X86

				this->_emitX86RM(id->_opCode[1] + (dst.getSize() != 1), dst.getSize() == 2, dst.getSize() == 8, static_cast<uint8_t>(id->_opCodeR), dst, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupJcc:
			if (o0->isLabel())
			{
				LabelData &l_data = this->_labels[reinterpret_cast<const Label *>(o0)->getId() & kOperandIdValueMask];

				uint32_t hint = static_cast<uint32_t>(o1->isImm() ? reinterpret_cast<const Imm &>(*o1).getValue() : 0);
				bool isShortJump = !!(_emitOptions & kX86EmitOptionShortJump);

				// Emit jump hint if configured for that.
				if ((hint & (kCondHintLikely | kCondHintUnlikely)) && (this->_properties & (1 << kX86PropertyJumpHints)))
				{
					if (hint & kCondHintLikely)
						this->_emitByte(kX86CondPrefixLikely);
					else if (hint & kCondHintUnlikely)
						this->_emitByte(kX86CondPrefixUnlikely);
				}

				if (l_data.offset != -1)
				{
					// Bound label.
					static const sysint_t rel8_size = 2;
					static const sysint_t rel32_size = 6;
					sysint_t offs = l_data.offset - this->getOffset();

					ASMJIT_ASSERT(offs <= 0);

					if (IntUtil::isInt8(offs - rel8_size))
					{
						this->_emitByte(0x70 | static_cast<uint8_t>(id->_opCode[0]));
						this->_emitByte(static_cast<uint8_t>(static_cast<int8_t>(offs - rel8_size)));

						// Change the emit options so logger can log instruction correctly.
						this->_emitOptions |= kX86EmitOptionShortJump;
					}
					else
					{
						if (isShortJump && this->_logger)
						{
							this->_logger->logString("*** ASSEMBLER WARNING: Emitting long conditional jump, but short jump instruction forced!\n");
							this->_emitOptions &= ~kX86EmitOptionShortJump;
						}

						this->_emitByte(0x0F);
						this->_emitByte(0x80 | static_cast<uint8_t>(id->_opCode[0]));
						this->_emitInt32(static_cast<int32_t>(offs - rel32_size));
					}
				}
				else
				{
					// Non-bound label.
					if (isShortJump)
					{
						this->_emitByte(0x70 | static_cast<uint8_t>(id->_opCode[0]));
						this->_emitDisplacement(l_data, -1, 1);
					}
					else
					{
						this->_emitByte(0x0F);
						this->_emitByte(0x80 | static_cast<uint8_t>(id->_opCode[0]));
						this->_emitDisplacement(l_data, -4, 4);
					}
				}
				_FINISHED();
			}

			break;

		case kX86InstGroupJmp:
			if (o0->isRegMem())
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);

				this->_emitX86RM(0xFF, 0, 0, 4, dst, 0, forceRexPrefix);
				_FINISHED();
			}

			if (o0->isImm())
			{
				const Imm &imm = reinterpret_cast<const Imm &>(*o0);
				this->_emitByte(0xE9);
				this->_emitJmpOrCallReloc(kX86InstGroupJmp, reinterpret_cast<void *>(imm.getValue()));
				_FINISHED();
			}

			if (o0->isLabel())
			{
				LabelData &l_data = this->_labels[reinterpret_cast<const Label *>(o0)->getId() & kOperandIdValueMask];
				bool isShortJump = !!(this->_emitOptions & kX86EmitOptionShortJump);

				if (l_data.offset != -1)
				{
					// Bound label.
					static const sysint_t rel8_size = 2;
					static const sysint_t rel32_size = 5;
					sysint_t offs = l_data.offset - this->getOffset();

					if (IntUtil::isInt8(offs - rel8_size))
					{
						this->_emitByte(0xEB);
						this->_emitByte(static_cast<uint8_t>(static_cast<int8_t>(offs - rel8_size)));

						// Change the emit options so logger can log instruction correctly.
						this->_emitOptions |= kX86EmitOptionShortJump;
					}
					else
					{
						if (isShortJump && this->_logger)
						{
							this->_logger->logString("*** ASSEMBLER WARNING: Emitting long jump, but short jump instruction forced!\n");
							this->_emitOptions &= ~kX86EmitOptionShortJump;
						}

						this->_emitByte(0xE9);
						this->_emitInt32(static_cast<int32_t>(offs - rel32_size));
					}
				}
				else
				{
					// Non-bound label.
					if (isShortJump)
					{
						this->_emitByte(0xEB);
						this->_emitDisplacement(l_data, -1, 1);
					}
					else
					{
						this->_emitByte(0xE9);
						this->_emitDisplacement(l_data, -4, 4);
					}
				}
				_FINISHED();
			}

			break;

		case kX86InstGroupLea:
			if (o0->isReg() && o1->isMem())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				const Mem &src = reinterpret_cast<const Mem &>(*o1);

				// Size override prefix support.
				if (src.getSizePrefix())
				{
					this->_emitByte(0x67);
#ifdef ASMJIT_X86
					memRegType = kX86RegTypeGpw;
#else
					memRegType = kX86RegTypeGpd;
#endif
				}

				this->_emitX86RM(0x8D, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), src, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupMem:
			if (o0->isMem())
			{
				this->_emitX86RM(id->_opCode[0], 0, static_cast<uint8_t>(id->_opCode[1]), (uint8_t)id->_opCodeR, reinterpret_cast<const Mem &>(*o0), 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupMov:
		{
			const Operand &dst = *o0;
			const Operand &src = *o1;

			switch ((dst.getType() << 4) | src.getType())
			{
				// Reg <- Reg/Mem
				case (kOperandReg << 4) | kOperandReg:
					// Reg <- Sreg
					if (src.isRegType(kX86RegTypeSeg))
					{
						ASMJIT_ASSERT(dst.isRegType(kX86RegTypeGpw) || dst.isRegType(kX86RegTypeGpd) || dst.isRegType(kX86RegTypeGpq));

						this->_emitX86RM(0x8C, dst.getSize() == 2, dst.getSize() == 8, reinterpret_cast<const SegmentReg &>(src).getRegCode(), reinterpret_cast<const Operand &>(dst), 0, forceRexPrefix);
						_FINISHED();
					}

					// Sreg <- Reg/Mem
					if (dst.isRegType(kX86RegTypeSeg))
					{
						ASMJIT_ASSERT(src.isRegType(kX86RegTypeGpw) || src.isRegType(kX86RegTypeGpd) || src.isRegType(kX86RegTypeGpq));

					_Emit_Mov_Sreg_RM:
						this->_emitX86RM(0x8E, src.getSize() == 2, src.getSize() == 8, reinterpret_cast<const SegmentReg &>(dst).getRegCode(), reinterpret_cast<const Operand &>(src), 0, forceRexPrefix);
						_FINISHED();
					}

					ASMJIT_ASSERT(src.isRegType(kX86RegTypeGpbLo) || src.isRegType(kX86RegTypeGpbHi) || src.isRegType(kX86RegTypeGpw) || src.isRegType(kX86RegTypeGpd) || src.isRegType(kX86RegTypeGpq));
					// ... fall through ...
				case (kOperandReg << 4) | kOperandMem:
					// Sreg <- Mem
					if (dst.isRegType(kX86RegTypeSeg))
						goto _Emit_Mov_Sreg_RM;

					ASMJIT_ASSERT(dst.isRegType(kX86RegTypeGpbLo) || dst.isRegType(kX86RegTypeGpbHi) || dst.isRegType(kX86RegTypeGpw) || dst.isRegType(kX86RegTypeGpd) || dst.isRegType(kX86RegTypeGpq));

					this->_emitX86RM(0x0000008A + (dst.getSize() != 1), dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), reinterpret_cast<const GpReg &>(dst).getRegCode(),
						reinterpret_cast<const Operand &>(src), 0, forceRexPrefix);
					_FINISHED();

				// Reg <- Imm
				case (kOperandReg << 4) | kOperandImm:
				{
					const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
					const Imm &src = reinterpret_cast<const Imm &>(*o1);

					// In 64-bit mode the immediate can be 64-bits long if the
					// destination operand type is register (otherwise 32-bits).
					immSize = dst.getSize();

#ifdef ASMJIT_X64
					// Optimize instruction size by using 32-bit immediate if value can
					// fit into it.
					if (immSize == 8 && IntUtil::isInt32(src.getValue()))
					{
						this->_emitX86RM(0xC7,
							0, // 16BIT
							1, // REX.W
							0, // O
							dst, 0, forceRexPrefix);
						immSize = 4;
					}
					else
#endif // ASMJIT_X64
						this->_emitX86Inl(dst.getSize() == 1 ? 0xB0 : 0xB8, dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), forceRexPrefix);

					_FINISHED_IMMEDIATE(&src, immSize);
				}

				// Mem <- Reg/Sreg
				case (kOperandMem << 4) | kOperandReg:
					if (src.isRegType(kX86RegTypeSeg))
					{
						// Mem <- Sreg
						this->_emitX86RM(0x8C, dst.getSize() == 2, dst.getSize() == 8, reinterpret_cast<const SegmentReg &>(src).getRegCode(), reinterpret_cast<const Operand &>(dst), 0, forceRexPrefix);
					}
					else
					{
						// Mem <- Reg
						ASMJIT_ASSERT(src.isRegType(kX86RegTypeGpbLo) || src.isRegType(kX86RegTypeGpbHi) || src.isRegType(kX86RegTypeGpw) || src.isRegType(kX86RegTypeGpd) || src.isRegType(kX86RegTypeGpq));

						this->_emitX86RM(0x88 + (src.getSize() != 1), src.isRegType(kX86RegTypeGpw), src.isRegType(kX86RegTypeGpq), reinterpret_cast<const GpReg &>(src).getRegCode(),
							reinterpret_cast<const Operand &>(dst), 0, forceRexPrefix);
					}

					_FINISHED();

				// Mem <- Imm
				case (kOperandMem << 4) | kOperandImm:
					immSize = IntUtil::_min(dst.getSize(), 4u);

					this->_emitX86RM(0xC6 + (dst.getSize() != 1), dst.getSize() == 2, dst.getSize() == 8, 0, reinterpret_cast<const Operand &>(dst), immSize, forceRexPrefix);
					_FINISHED_IMMEDIATE(&src, immSize);
			}

			break;
		}

		case kX86InstGroupMovPtr:
			if ((o0->isReg() && o1->isImm()) || (o0->isImm() && o1->isReg()))
			{
				bool reverse = o1->getType() == kOperandReg;
				uint8_t opCode = !reverse ? 0xA0 : 0xA2;
				const GpReg &reg = reinterpret_cast<const GpReg &>(!reverse ? *o0 : *o1);
				const Imm &imm = reinterpret_cast<const Imm &>(!reverse ? *o1 : *o0);

				if (reg.getRegIndex())
					goto _IllegalInstruction;

				if (reg.isRegType(kX86RegTypeGpw))
					this->_emitByte(0x66);
#ifdef ASMJIT_X64
				this->_emitRexR(reg.getSize() == 8, 0, 0, forceRexPrefix);
#endif // ASMJIT_X64
				this->_emitByte(opCode + (reg.getSize() != 1));
				_FINISHED_IMMEDIATE(&imm, sizeof(sysint_t));
			}

			break;

		case kX86InstGroupMovSxMovZx:
			if (o0->isReg() && o1->isRegMem())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				const Operand &src = reinterpret_cast<const Operand &>(*o1);

				if (dst.getSize() == 1)
					goto _IllegalInstruction;

				if (src.getSize() != 1 && src.getSize() != 2)
					goto _IllegalInstruction;

				if (src.getSize() == 2 && dst.getSize() == 2)
					goto _IllegalInstruction;

				this->_emitX86RM(id->_opCode[0] + (src.getSize() != 1), dst.isRegType(kX86RegTypeGpw), dst.isRegType(kX86RegTypeGpq), dst.getRegCode(), src, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

#ifdef ASMJIT_X64
		case kX86InstGroupMovSxD:
			if (o0->isReg() && o1->isRegMem())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				const Operand &src = reinterpret_cast<const Operand &>(*o1);
				this->_emitX86RM(0x00000063, 0, 1, dst.getRegCode(), src, 0, forceRexPrefix);
				_FINISHED();
			}

			break;
#endif // ASMJIT_X64

		case kX86InstGroupPush:
			if (o0->isRegType(kX86RegTypeSeg))
			{
				static const uint32_t opcodeList[] =
				{
					0x06, // ES.
					0x0E, // CS.
					0x16, // SS.
					0x1E, // DS.
					0x0FA0, // FS.
					0x0FA8  // GS.
				};

				unsigned segment = reinterpret_cast<const SegmentReg *>(o0)->getRegIndex();
				ASMJIT_ASSERT(segment < kX86SegCount);

				unsigned opcode = opcodeList[segment];

				if (opcode > 0xFF)
					this->_emitByte(opcode >> 8);
				this->_emitByte(opcode & 0xFF);

				_FINISHED();
			}

			// This section is only for immediates, memory/register operands are handled in kX86InstGroupPop.
			if (o0->isImm())
			{
				const Imm &imm = reinterpret_cast<const Imm &>(*o0);

				if (IntUtil::isInt8(imm.getValue()))
				{
					this->_emitByte(0x6A);
					_FINISHED_IMMEDIATE(&imm, 1);
				}
				else
				{
					this->_emitByte(0x68);
					_FINISHED_IMMEDIATE(&imm, 4);
				}
			}

			// ... goto kX86InstGroupPop ...

		case kX86InstGroupPop:
			if (o0->isRegType(kX86RegTypeSeg))
			{
				static const uint32_t opcodeList[] =
				{
					0x07, // ES.
					0, // CS.
					0x17, // SS.
					0x1F, // DS.
					0x0FA1, // FS.
					0x0FA9  // GS.
				};

				unsigned segment = reinterpret_cast<const SegmentReg *>(o0)->getRegIndex();
				ASMJIT_ASSERT(segment < kX86SegCount);

				unsigned opcode = opcodeList[segment];
				ASMJIT_ASSERT(opcode);

				if (opcode > 0xFF)
					this->_emitByte(opcode >> 8);
				this->_emitByte(opcode & 0xFF);

				_FINISHED();
			}

			if (o0->isReg())
			{
				ASMJIT_ASSERT(o0->isRegType(kX86RegTypeGpw) || o0->isRegType(kX86RegTypeGpz));
				this->_emitX86Inl(id->_opCode[0], o0->isRegType(kX86RegTypeGpw), 0, reinterpret_cast<const GpReg &>(*o0).getRegCode(), forceRexPrefix);
				_FINISHED();
			}

			if (o0->isMem())
			{
				this->_emitX86RM(id->_opCode[1], o0->getSize() == 2, 0, static_cast<uint8_t>(id->_opCodeR), reinterpret_cast<const Operand &>(*o0), 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupRegRm:
			if (o0->isReg() && o1->isRegMem())
			{
				const GpReg &dst = reinterpret_cast<const GpReg &>(*o0);
				const Operand &src = reinterpret_cast<const Operand &>(*o1);
				ASMJIT_ASSERT(dst.getSize() != 1);

				this->_emitX86RM(id->_opCode[0], dst.getRegType() == kX86RegTypeGpw, dst.getRegType() == kX86RegTypeGpq, dst.getRegCode(), src, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupRm:
			if (o0->isRegMem())
			{
				const Operand &op = reinterpret_cast<const Operand &>(*o0);
				this->_emitX86RM(id->_opCode[0] + (op.getSize() != 1), op.getSize() == 2, op.getSize() == 8, static_cast<uint8_t>(id->_opCodeR), op, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupRmByte:
			if (o0->isRegMem())
			{
				const Operand &op = reinterpret_cast<const Operand &>(*o0);

				// Only BYTE register or BYTE/TYPELESS memory location can be used.
				ASMJIT_ASSERT(op.getSize() <= 1);

				this->_emitX86RM(id->_opCode[0], false, false, 0, op, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupRmReg:
			if (o0->isRegMem() && o1->isReg())
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);
				const GpReg &src = reinterpret_cast<const GpReg &>(*o1);
				this->_emitX86RM(id->_opCode[0] + (src.getSize() != 1), src.getRegType() == kX86RegTypeGpw, src.getRegType() == kX86RegTypeGpq, src.getRegCode(), dst, 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupRep:
		{
			uint32_t opCode = id->_opCode[0];
			uint32_t opSize = id->_opCode[1];

			// Emit REP prefix (1 BYTE).
			this->_emitByte(opCode >> 24);

			if (opSize != 1)
				++opCode; // D, Q and W form.
			if (opSize == 2)
				this->_emitByte(0x66); // 16-bit prefix.
#ifdef ASMJIT_X64
			else if (opSize == 8)
				this->_emitByte(0x48); // REX.W prefix.
#endif // ASMJIT_X64

			// Emit opcode (1 BYTE).
			this->_emitByte(opCode & 0xFF);
			_FINISHED();
		}

		case kX86InstGroupRet:
			if (o0->isNone())
			{
				this->_emitByte(0xC3);
				_FINISHED();
			}
			else if (o0->isImm())
			{
				const Imm &imm = reinterpret_cast<const Imm &>(*o0);
				ASMJIT_ASSERT(IntUtil::isUInt16(imm.getValue()));

				if (!imm.getValue())
				{
					this->_emitByte(0xC3);
					_FINISHED();
				}
				else
				{
					this->_emitByte(0xC2);
					_FINISHED_IMMEDIATE(&imm, 2);
				}
			}

			break;

		case kX86InstGroupRot:
			if (o0->isRegMem() && (o1->isRegCode(kX86RegCl) || o1->isImm()))
			{
				// generate opcode. For these operations is base 0xC0 or 0xD0.
				bool useImm8 = o1->isImm() && reinterpret_cast<const Imm &>(*o1).getValue() != 1;
				uint32_t opCode = useImm8 ? 0xC0 : 0xD0;

				// size and operand type modifies the opcode
				if (o0->getSize() != 1)
					opCode |= 0x01;
				if (o1->getType() == kOperandReg)
					opCode |= 0x02;

				this->_emitX86RM(opCode, o0->getSize() == 2, o0->getSize() == 8, static_cast<uint8_t>(id->_opCodeR), reinterpret_cast<const Operand &>(*o0), useImm8 ? 1 : 0, forceRexPrefix);

				if (useImm8)
					_FINISHED_IMMEDIATE(o1, 1);
				else
					_FINISHED();
			}

			break;

		case kX86InstGroupShldShrd:
			if (o0->isRegMem() && o1->isReg() && (o2->isImm() || (o2->isReg() && o2->isRegCode(kX86RegCl))))
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);
				const GpReg &src1 = reinterpret_cast<const GpReg &>(*o1);
				const Operand &src2 = reinterpret_cast<const Operand &>(*o2);

				ASMJIT_ASSERT(dst.getSize() == src1.getSize());

				this->_emitX86RM(id->_opCode[0] + src2.isReg(), src1.isRegType(kX86RegTypeGpw), src1.isRegType(kX86RegTypeGpq), src1.getRegCode(), dst, src2.isImm() ? 1 : 0, forceRexPrefix);
				if (src2.isImm())
					_FINISHED_IMMEDIATE(&src2, 1);
				else
					_FINISHED();
			}

			break;

		case kX86InstGroupTest:
			if (o0->isRegMem() && o1->isReg())
			{
				ASMJIT_ASSERT(o0->getSize() == o1->getSize());
				this->_emitX86RM(0x84 + (o1->getSize() != 1), o1->getSize() == 2, o1->getSize() == 8, reinterpret_cast<const Reg &>(*o1).getRegCode(), reinterpret_cast<const Operand &>(*o0), 0, forceRexPrefix);
				_FINISHED();
			}

			// Alternate Form - AL, AX, EAX, RAX.
			if (o0->isRegIndex(0) && o1->isImm())
			{
				immSize = IntUtil::_min(o0->getSize(), 4u);

				if (o0->getSize() == 2)
					this->_emitByte(0x66); // 16-bit.
#ifdef ASMJIT_X64
				this->_emitRexRM(o0->getSize() == 8, 0, reinterpret_cast<const Operand &>(*o0), forceRexPrefix);
#endif // ASMJIT_X64
				this->_emitByte(0xA8 + (o0->getSize() != 1));
				_FINISHED_IMMEDIATE(o1, immSize);
			}

			if (o0->isRegMem() && o1->isImm())
			{
				immSize = IntUtil::_min(o0->getSize(), 4u);

				if (o0->getSize() == 2)
					this->_emitByte(0x66); // 16-bit.
				this->_emitSegmentPrefix(reinterpret_cast<const Operand &>(*o0)); // Segment prefix.
#ifdef ASMJIT_X64
				this->_emitRexRM(o0->getSize() == 8, 0, reinterpret_cast<const Operand &>(*o0), forceRexPrefix);
#endif // ASMJIT_X64
				this->_emitByte(0xF6 + (o0->getSize() != 1));
				this->_emitModRM(0, reinterpret_cast<const Operand &>(*o0), immSize);
				_FINISHED_IMMEDIATE(o1, immSize);
			}

			break;

		case kX86InstGroupXchg:
			if (o0->isRegMem() && o1->isReg())
			{
				const Operand &dst = reinterpret_cast<const Operand &>(*o0);
				const GpReg &src = reinterpret_cast<const GpReg &>(*o1);

				if (src.isRegType(kX86RegTypeGpw))
					this->_emitByte(0x66); // 16-bit.
				this->_emitSegmentPrefix(dst); // segment prefix
#ifdef ASMJIT_X64
				this->_emitRexRM(src.isRegType(kX86RegTypeGpq), src.getRegCode(), dst, forceRexPrefix);
#endif // ASMJIT_X64

				// Special opcode for index 0 registers (AX, EAX, RAX vs register).
				if ((dst.getType() == kOperandReg && dst.getSize() > 1) && (!reinterpret_cast<const GpReg &>(dst).getRegCode() || !reinterpret_cast<const GpReg &>(src).getRegCode()))
				{
					uint8_t index = reinterpret_cast<const GpReg &>(dst).getRegCode() | src.getRegCode();
					this->_emitByte(0x90 + index);
					_FINISHED();
				}

				this->_emitByte(0x86 + (src.getSize() != 1));
				this->_emitModRM(src.getRegCode(), dst, 0);
				_FINISHED();
			}

			break;

		case kX86InstGroupMovBE:
			if (o0->isReg() && o1->isMem())
			{
				this->_emitX86RM(0x000F38F0, o0->isRegType(kX86RegTypeGpw), o0->isRegType(kX86RegTypeGpq), reinterpret_cast<const GpReg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 0, forceRexPrefix);
				_FINISHED();
			}

			if (o0->isMem() && o1->isReg())
			{
				this->_emitX86RM(0x000F38F1, o1->isRegType(kX86RegTypeGpw), o1->isRegType(kX86RegTypeGpq), reinterpret_cast<const GpReg &>(*o1).getRegCode(), reinterpret_cast<const Mem &>(*o0), 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupX87StM:
			if (o0->isRegType(kX86RegTypeX87))
			{
				uint8_t i1 = reinterpret_cast<const X87Reg &>(*o0).getRegIndex();
				uint8_t i2 = 0;

				if (code != kX86InstFCom && code != kX86InstFComP)
				{
					if (!o1->isRegType(kX86RegTypeX87))
						goto _IllegalInstruction;
					i2 = reinterpret_cast<const X87Reg &>(*o1).getRegIndex();
				}
				else if (i1 && i2)
					goto _IllegalInstruction;

				this->_emitByte(!i1 ? ((id->_opCode[0] & 0xFF000000) >> 24) : ((id->_opCode[0] & 0x00FF0000) >> 16));
				this->_emitByte(!i1 ? ((id->_opCode[0] & 0x0000FF00) >> 8) + i2 : (id->_opCode[0] & 0x000000FF) + i1);
				_FINISHED();
			}

			if (o0->isMem() && (o0->getSize() == 4 || o0->getSize() == 8) && o1->isNone())
			{
				const Mem &m = reinterpret_cast<const Mem &>(*o0);

				// Segment prefix.
				this->_emitSegmentPrefix(m);

				this->_emitByte(o0->getSize() == 4 ? ((id->_opCode[0] & 0xFF000000) >> 24) : ((id->_opCode[0] & 0x00FF0000) >> 16));
				this->_emitModM(static_cast<uint8_t>(id->_opCodeR), m, 0);
				_FINISHED();
			}

			break;

		case kX86InstGroupX87StI:
			if (o0->isRegType(kX86RegTypeX87))
			{
				uint8_t i = reinterpret_cast<const X87Reg &>(*o0).getRegIndex();
				this->_emitByte(static_cast<uint8_t>((id->_opCode[0] & 0x0000FF00) >> 8));
				this->_emitByte(static_cast<uint8_t>((id->_opCode[0] & 0x000000FF) + i));
				_FINISHED();
			}

			break;

		case kX86InstGroupX87Status:
			if (o0->isReg() && reinterpret_cast<const Reg &>(*o0).getRegType() <= kX86RegTypeGpq && !reinterpret_cast<const Reg &>(*o0).getRegIndex())
			{
				this->_emitOpCode(id->_opCode[1]);
				_FINISHED();
			}

			if (o0->isMem())
			{
				this->_emitX86RM(id->_opCode[0], 0, 0, static_cast<uint8_t>(id->_opCodeR), reinterpret_cast<const Mem &>(*o0), 0, forceRexPrefix);
				_FINISHED();
			}

			break;

		case kX86InstGroupX87FldFst:
			if (o0->isRegType(kX86RegTypeX87))
			{
				this->_emitByte(static_cast<uint8_t>((id->_opCode[1] & 0xFF000000) >> 24));
				this->_emitByte(static_cast<uint8_t>((id->_opCode[1] & 0x00FF0000) >> 16) + reinterpret_cast<const X87Reg &>(*o0).getRegIndex());
				_FINISHED();
			}

			// ... fall through to kX86InstGroupX87Mem ...

		case kX86InstGroupX87Mem:
		{
			if (!o0->isMem())
				goto _IllegalInstruction;
			const Mem &m = reinterpret_cast<const Mem &>(*o0);

			uint8_t opCode = 0x00, mod = 0;

			if (o0->getSize() == 2 && (id->_opFlags[0] & kX86InstOpStM2))
			{
				opCode = static_cast<uint8_t>((id->_opCode[0] & 0xFF000000) >> 24);
				mod = static_cast<uint8_t>(id->_opCodeR);
			}
			if (o0->getSize() == 4 && (id->_opFlags[0] & kX86InstOpStM4))
			{
				opCode = static_cast<uint8_t>((id->_opCode[0] & 0x00FF0000) >> 16);
				mod = static_cast<uint8_t>(id->_opCodeR);
			}
			if (o0->getSize() == 8 && (id->_opFlags[0] & kX86InstOpStM8))
			{
				opCode = static_cast<uint8_t>((id->_opCode[0] & 0x0000FF00) >> 8);
				mod = static_cast<uint8_t>(id->_opCode[0] & 0x000000FF);
			}

			if (opCode)
			{
				this->_emitSegmentPrefix(m);
				this->_emitByte(opCode);
				this->_emitModM(mod, m, 0);
				_FINISHED();
			}

			break;
		}

		case kX86InstGroupMmuMov:
		{
			ASMJIT_ASSERT(id->_opFlags[0]);
			ASMJIT_ASSERT(id->_opFlags[1]);

			// Check parameters (X)MM|GP32_64 <- (X)MM|GP32_64|Mem|Imm
			if ((o0->isMem() && !(id->_opFlags[0] & kX86InstOpMem)) || (o0->isRegType(kX86RegTypeMm) && !(id->_opFlags[0] & kX86InstOpMm)) || (o0->isRegType(kX86RegTypeXmm) && !(id->_opFlags[0] & kX86InstOpXmm)) ||
				(o0->isRegType(kX86RegTypeGpd) && !(id->_opFlags[0] & kX86InstOpGd)) || (o0->isRegType(kX86RegTypeGpq) && !(id->_opFlags[0] & kX86InstOpGq)) ||
				(o1->isRegType(kX86RegTypeMm) && !(id->_opFlags[1] & kX86InstOpMm)) || (o1->isRegType(kX86RegTypeXmm) && !(id->_opFlags[1] & kX86InstOpXmm)) ||
				(o1->isRegType(kX86RegTypeGpd) && !(id->_opFlags[1] & kX86InstOpGd)) || (o1->isRegType(kX86RegTypeGpq) && !(id->_opFlags[1] & kX86InstOpGq)) ||
				(o1->isMem() && !(id->_opFlags[1] & kX86InstOpMem)))
				goto _IllegalInstruction;

			// Illegal.
			if (o0->isMem() && o1->isMem())
				goto _IllegalInstruction;

			uint8_t rexw = ((id->_opFlags[0] | id->_opFlags[1]) & kX86InstOpNoRex) ? 0 : o0->isRegType(kX86RegTypeGpq) | o1->isRegType(kX86RegTypeGpq);

			// (X)MM|Reg <- (X)MM|Reg
			if (o0->isReg() && o1->isReg())
			{
				this->_emitMmu(id->_opCode[0], rexw, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Reg &>(*o1), 0);
				_FINISHED();
			}

			// (X)MM|Reg <- Mem
			if (o0->isReg() && o1->isMem())
			{
				this->_emitMmu(id->_opCode[0], rexw, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 0);
				_FINISHED();
			}

			// Mem <- (X)MM|Reg
			if (o0->isMem() && o1->isReg())
			{
				this->_emitMmu(id->_opCode[1], rexw, reinterpret_cast<const Reg &>(*o1).getRegCode(), reinterpret_cast<const Mem &>(*o0), 0);
				_FINISHED();
			}

			break;
		}

		case kX86InstGroupMmuMovD:
			if ((o0->isRegType(kX86RegTypeMm) || o0->isRegType(kX86RegTypeXmm)) && (o1->isRegType(kX86RegTypeGpd) || o1->isMem()))
			{
				this->_emitMmu(o0->isRegType(kX86RegTypeXmm) ? 0x66000F6E : 0x00000F6E, 0, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Operand &>(*o1), 0);
				_FINISHED();
			}

			if ((o0->isRegType(kX86RegTypeGpd) || o0->isMem()) && (o1->isRegType(kX86RegTypeMm) || o1->isRegType(kX86RegTypeXmm)))
			{
				this->_emitMmu(o1->isRegType(kX86RegTypeXmm) ? 0x66000F7E : 0x00000F7E, 0, reinterpret_cast<const Reg &>(*o1).getRegCode(), reinterpret_cast<const Operand &>(*o0), 0);
				_FINISHED();
			}

			break;

		case kX86InstGroupMmuMovQ:
			if (o0->isRegType(kX86RegTypeMm) && o1->isRegType(kX86RegTypeMm))
			{
				this->_emitMmu(0x00000F6F, 0, reinterpret_cast<const MmReg &>(*o0).getRegCode(), reinterpret_cast<const MmReg &>(*o1), 0);
				_FINISHED();
			}

			if (o0->isRegType(kX86RegTypeXmm) && o1->isRegType(kX86RegTypeXmm))
			{
				this->_emitMmu(0xF3000F7E, 0, reinterpret_cast<const XmmReg &>(*o0).getRegCode(), reinterpret_cast<const XmmReg &>(*o1), 0);
				_FINISHED();
			}

			// Convenience - movdq2q
			if (o0->isRegType(kX86RegTypeMm) && o1->isRegType(kX86RegTypeXmm))
			{
				this->_emitMmu(0xF2000FD6, 0, reinterpret_cast<const MmReg &>(*o0).getRegCode(), reinterpret_cast<const XmmReg &>(*o1), 0);
				_FINISHED();
			}

			// Convenience - movq2dq
			if (o0->isRegType(kX86RegTypeXmm) && o1->isRegType(kX86RegTypeMm))
			{
				this->_emitMmu(0xF3000FD6, 0, reinterpret_cast<const XmmReg &>(*o0).getRegCode(), reinterpret_cast<const MmReg &>(*o1), 0);
				_FINISHED();
			}

			if (o0->isRegType(kX86RegTypeMm) && o1->isMem())
			{
				this->_emitMmu(0x00000F6F, 0, reinterpret_cast<const MmReg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 0);
				_FINISHED();
			}

			if (o0->isRegType(kX86RegTypeXmm) && o1->isMem())
			{
				this->_emitMmu(0xF3000F7E, 0, reinterpret_cast<const XmmReg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 0);
				_FINISHED();
			}

			if (o0->isMem() && o1->isRegType(kX86RegTypeMm))
			{
				this->_emitMmu(0x00000F7F, 0, reinterpret_cast<const MmReg &>(*o1).getRegCode(), reinterpret_cast<const Mem &>(*o0), 0);
				_FINISHED();
			}

			if (o0->isMem() && o1->isRegType(kX86RegTypeXmm))
			{
				this->_emitMmu(0x66000FD6, 0, reinterpret_cast<const XmmReg &>(*o1).getRegCode(), reinterpret_cast<const Mem &>(*o0), 0);
				_FINISHED();
			}

#ifdef ASMJIT_X64
			if ((o0->isRegType(kX86RegTypeMm) || o0->isRegType(kX86RegTypeXmm)) && (o1->isRegType(kX86RegTypeGpq) || o1->isMem()))
			{
				this->_emitMmu(o0->isRegType(kX86RegTypeXmm) ? 0x66000F6E : 0x00000F6E, 1, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Operand &>(*o1), 0);
				_FINISHED();
			}

			if ((o0->isRegType(kX86RegTypeGpq) || o0->isMem()) && (o1->isRegType(kX86RegTypeMm) || o1->isRegType(kX86RegTypeXmm)))
			{
				this->_emitMmu(o1->isRegType(kX86RegTypeXmm) ? 0x66000F7E : 0x00000F7E, 1, reinterpret_cast<const Reg &>(*o1).getRegCode(), reinterpret_cast<const Operand &>(*o0), 0);
				_FINISHED();
			}
#endif // ASMJIT_X64

			break;

		case kX86InstGroupMmuExtract:
		{
			if (!(o0->isRegMem() && (o1->isRegType(kX86RegTypeXmm) || (code == kX86InstPExtrW && o1->isRegType(kX86RegTypeMm))) && o2->isImm()))
				goto _IllegalInstruction;

			uint32_t opCode = id->_opCode[0];
			uint8_t isGpdGpq = o0->isRegType(kX86RegTypeGpd) | o0->isRegType(kX86RegTypeGpq);

			if (code == kX86InstPExtrB && (o0->getSize() && o0->getSize() != 1) && !isGpdGpq)
				goto _IllegalInstruction;
			if (code == kX86InstPExtrW && (o0->getSize() && o0->getSize() != 2) && !isGpdGpq)
				goto _IllegalInstruction;
			if (code == kX86InstPExtrD && (o0->getSize() && o0->getSize() != 4) && !isGpdGpq)
				goto _IllegalInstruction;
			if (code == kX86InstPExtrQ && (o0->getSize() && o0->getSize() != 8) && !isGpdGpq)
				goto _IllegalInstruction;

			if (o1->isRegType(kX86RegTypeXmm))
				opCode |= 0x66000000;

			if (o0->isReg())
			{
				this->_emitMmu(opCode, id->_opCodeR | static_cast<uint8_t>(o0->isRegType(kX86RegTypeGpq)), reinterpret_cast<const Reg &>(*o1).getRegCode(), reinterpret_cast<const Reg &>(*o0), 1);
				_FINISHED_IMMEDIATE(o2, 1);
			}

			if (o0->isMem())
			{
				this->_emitMmu(opCode, static_cast<uint8_t>(id->_opCodeR), reinterpret_cast<const Reg &>(*o1).getRegCode(), reinterpret_cast<const Mem &>(*o0), 1);
				_FINISHED_IMMEDIATE(o2, 1);
			}

			break;
		}

		case kX86InstGroupMmuPrefetch:
			if (o0->isMem() && o1->isImm())
			{
				const Mem &mem = reinterpret_cast<const Mem &>(*o0);
				const Imm &hint = reinterpret_cast<const Imm &>(*o1);

				this->_emitMmu(0x00000F18, 0, static_cast<uint8_t>(hint.getValue()), mem, 0);
				_FINISHED();
			}

			break;

		case kX86InstGroupMmuRmI:
		{
			ASMJIT_ASSERT(id->_opFlags[0]);
			ASMJIT_ASSERT(id->_opFlags[1]);

			// Check parameters (X)MM|GP32_64 <- (X)MM|GP32_64|Mem|Imm
			if (!o0->isReg() || (o0->isRegType(kX86RegTypeMm) && !(id->_opFlags[0] & kX86InstOpMm)) || (o0->isRegType(kX86RegTypeXmm) && !(id->_opFlags[0] & kX86InstOpXmm)) ||
				(o0->isRegType(kX86RegTypeGpd) && !(id->_opFlags[0] & kX86InstOpGd)) || (o0->isRegType(kX86RegTypeGpq) && !(id->_opFlags[0] & kX86InstOpGq)) ||
				(o1->isRegType(kX86RegTypeMm) && !(id->_opFlags[1] & kX86InstOpMm)) || (o1->isRegType(kX86RegTypeXmm) && !(id->_opFlags[1] & kX86InstOpXmm)) ||
				(o1->isRegType(kX86RegTypeGpd) && !(id->_opFlags[1] & kX86InstOpGd)) || (o1->isRegType(kX86RegTypeGpq) && !(id->_opFlags[1] & kX86InstOpGq)) ||
				(o1->isMem() && !(id->_opFlags[1] & kX86InstOpMem)) || (o1->isImm() && !(id->_opFlags[1] & kX86InstOpImm)))
				goto _IllegalInstruction;

			uint32_t prefix = ((id->_opFlags[0] & kX86InstOpMmXmm) == kX86InstOpMmXmm && o0->isRegType(kX86RegTypeXmm)) ||
				((id->_opFlags[1] & kX86InstOpMmXmm) == kX86InstOpMmXmm && o1->isRegType(kX86RegTypeXmm)) ? 0x66000000 : 0x00000000;

			uint8_t rexw = ((id->_opFlags[0] | id->_opFlags[1]) & kX86InstOpNoRex) ? 0 : o0->isRegType(kX86RegTypeGpq) | o1->isRegType(kX86RegTypeGpq);

			// (X)MM <- (X)MM (opcode0)
			if (o1->isReg())
			{
				if (!(id->_opFlags[1] & (kX86InstOpMmXmm | kX86InstOpGqd)))
					goto _IllegalInstruction;
				this->_emitMmu(id->_opCode[0] | prefix, rexw, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Reg &>(*o1), 0);
				_FINISHED();
			}
			// (X)MM <- Mem (opcode0)
			if (o1->isMem())
			{
				if (!(id->_opFlags[1] & kX86InstOpMem))
					goto _IllegalInstruction;
				this->_emitMmu(id->_opCode[0] | prefix, rexw, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 0);
				_FINISHED();
			}
			// (X)MM <- Imm (opcode1+opcodeR)
			if (o1->isImm())
			{
				if (!(id->_opFlags[1] & kX86InstOpImm))
					goto _IllegalInstruction;
				this->_emitMmu(id->_opCode[1] | prefix, rexw, static_cast<uint8_t>(id->_opCodeR), reinterpret_cast<const Reg &>(*o0), 1);
				_FINISHED_IMMEDIATE(o1, 1);
			}

			break;
		}

		case kX86InstGroupMmuRmImm8:
		{
			ASMJIT_ASSERT(id->_opFlags[0]);
			ASMJIT_ASSERT(id->_opFlags[1]);

			// Check parameters (X)MM|GP32_64 <- (X)MM|GP32_64|Mem|Imm
			if (!o0->isReg() || (o0->isRegType(kX86RegTypeMm ) && !(id->_opFlags[0] & kX86InstOpMm)) || (o0->isRegType(kX86RegTypeXmm) && !(id->_opFlags[0] & kX86InstOpXmm)) ||
				(o0->isRegType(kX86RegTypeGpd) && !(id->_opFlags[0] & kX86InstOpGd)) || (o0->isRegType(kX86RegTypeGpq) && !(id->_opFlags[0] & kX86InstOpGq)) ||
				(o1->isRegType(kX86RegTypeMm) && !(id->_opFlags[1] & kX86InstOpMm)) || (o1->isRegType(kX86RegTypeXmm) && !(id->_opFlags[1] & kX86InstOpXmm)) ||
				(o1->isRegType(kX86RegTypeGpd) && !(id->_opFlags[1] & kX86InstOpGd)) || (o1->isRegType(kX86RegTypeGpq) && !(id->_opFlags[1] & kX86InstOpGq)) ||
				(o1->isMem() && !(id->_opFlags[1] & kX86InstOpMem)) || !o2->isImm())
				goto _IllegalInstruction;

			uint32_t prefix = ((id->_opFlags[0] & kX86InstOpMmXmm) == kX86InstOpMmXmm && o0->isRegType(kX86RegTypeXmm)) ||
				((id->_opFlags[1] & kX86InstOpMmXmm) == kX86InstOpMmXmm && o1->isRegType(kX86RegTypeXmm)) ? 0x66000000 : 0x00000000;

			uint8_t rexw = ((id->_opFlags[0]|id->_opFlags[1]) & kX86InstOpNoRex) ? 0 : o0->isRegType(kX86RegTypeGpq) | o1->isRegType(kX86RegTypeGpq);

			// (X)MM <- (X)MM (opcode0)
			if (o1->isReg())
			{
				if (!(id->_opFlags[1] & (kX86InstOpMmXmm | kX86InstOpGqd)))
					goto _IllegalInstruction;
				this->_emitMmu(id->_opCode[0] | prefix, rexw, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Reg &>(*o1), 1);
				_FINISHED_IMMEDIATE(o2, 1);
			}
			// (X)MM <- Mem (opcode0)
			if (o1->isMem())
			{
				if (!(id->_opFlags[1] & kX86InstOpMem))
					goto _IllegalInstruction;
				this->_emitMmu(id->_opCode[0] | prefix, rexw, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 1);
				_FINISHED_IMMEDIATE(o2, 1);
			}

			break;
		}

		case kX86InstGroupMmuRm3dNow:
			if (o0->isRegType(kX86RegTypeMm) && (o1->isRegType(kX86RegTypeMm) || o1->isMem()))
			{
				this->_emitMmu(id->_opCode[0], 0, reinterpret_cast<const Reg &>(*o0).getRegCode(), reinterpret_cast<const Mem &>(*o1), 1);
				this->_emitByte(static_cast<uint8_t>(id->_opCode[1]));
				_FINISHED();
			}

			break;
	}

_IllegalInstruction:
	// Set an error. If we run in release mode assertion will be not used, so we
	// must inform about invalid state.
	this->setError(kErrorIllegalInstruction);

#ifdef ASMJIT_DEBUG
	assertIllegal = true;
#endif // ASMJIT_DEBUG
	goto _End;

_EmitImmediate:
	sysint_t value = immOperand->getValue();
	switch (immSize)
	{
		case 1:
			this->_emitByte(static_cast<uint8_t>(static_cast<sysuint_t>(value)));
			break;
		case 2:
			this->_emitWord(static_cast<uint16_t>(static_cast<sysuint_t>(value)));
			break;
		case 4:
			this->_emitDWord(static_cast<uint32_t>(static_cast<sysuint_t>(value)));
			break;
#ifdef ASMJIT_X64
		case 8:
			this->_emitQWord(static_cast<uint64_t>(static_cast<sysuint_t>(value)));
			break;
#endif // ASMJIT_X64
		default:
			ASMJIT_ASSERT(0);
	}

_End:
	if (this->_logger
#ifdef ASMJIT_DEBUG
		|| assertIllegal
#endif // ASMJIT_DEBUG
		)
	{
		char bufStorage[512];
		char *buf = bufStorage;

		// Detect truncated operand.
		Imm immTemporary(0);
		uint32_t loggerFlags = 0;

		// Use the original operands, because BYTE some of them were replaced.
		if (bLoHiUsed)
		{
			o0 = _loggerOperands[0];
			o1 = _loggerOperands[1];
			o2 = _loggerOperands[2];
		}

		if (immOperand)
		{
			sysint_t value = immOperand->getValue();
			bool isUnsigned = immOperand->isUnsigned();

			switch (immSize)
			{
				case 1:
					if (isUnsigned && !IntUtil::isUInt8(value))
					{
						immTemporary.setValue(static_cast<uint8_t>(static_cast<sysuint_t>(value)), true);
						break;
					}
					if (!isUnsigned && !IntUtil::isInt8(value))
					{
						immTemporary.setValue(static_cast<uint8_t>(static_cast<sysuint_t>(value)), false);
						break;
					}
					break;
				case 2:
					if (isUnsigned && !IntUtil::isUInt16(value))
					{
						immTemporary.setValue(static_cast<uint16_t>(static_cast<sysuint_t>(value)), true);
						break;
					}
					if (!isUnsigned && !IntUtil::isInt16(value))
					{
						immTemporary.setValue(static_cast<uint16_t>(static_cast<sysuint_t>(value)), false);
						break;
					}
					break;
				case 4:
					if (isUnsigned && !IntUtil::isUInt32(value))
					{
						immTemporary.setValue(static_cast<uint32_t>(static_cast<sysuint_t>(value)), true);
						break;
					}
					if (!isUnsigned && !IntUtil::isInt32(value))
					{
						immTemporary.setValue(static_cast<uint32_t>(static_cast<sysuint_t>(value)), false);
						break;
					}
					break;
			}

			if (immTemporary.getValue())
			{
				if (o0 == immOperand)
					o0 = &immTemporary;
				if (o1 == immOperand)
					o1 = &immTemporary;
				if (o2 == immOperand)
					o2 = &immTemporary;
			}
		}

		if (this->_logger)
		{
			buf = StringUtil::copy(buf, this->_logger->getInstructionPrefix());
			loggerFlags = this->_logger->getFlags();
		}

		buf = X86Assembler_dumpInstruction(buf, code, this->_emitOptions, o0, o1, o2, memRegType, loggerFlags);

		if (loggerFlags & kLoggerOutputBinary)
			buf = X86Assembler_dumpComment(buf, static_cast<size_t>(buf - bufStorage), this->getCode() + beginOffset, this->getOffset() - beginOffset, this->_inlineComment);
		else
			buf = X86Assembler_dumpComment(buf, static_cast<size_t>(buf - bufStorage), nullptr, 0, this->_inlineComment);

		// We don't need to NULL terminate the resulting string.
#ifdef ASMJIT_DEBUG
		if (this->_logger)
#endif // ASMJIT_DEBUG
			this->_logger->logString(bufStorage, static_cast<size_t>(buf - bufStorage));

#ifdef ASMJIT_DEBUG
		if (assertIllegal)
		{
			// Here we need to NULL terminate.
			buf[0] = '\0';

			// Raise an assertion failure, because this situation shouldn't happen.
			assertionFailure(__FILE__, __LINE__, bufStorage);
		}
#endif // ASMJIT_DEBUG
	}

_Cleanup:
	this->_inlineComment = nullptr;
	this->_emitOptions = 0;
}

void X86Assembler::_emitJcc(uint32_t code, const Label *label, uint32_t hint)
{
	if (hint == kCondHintNone)
		this->_emitInstruction(code, label);
	else
	{
		Imm imm(hint);
		this->_emitInstruction(code, label, &imm);
	}
}

// ============================================================================
// [AsmJit::Assembler - Relocation helpers]
// ============================================================================

size_t X86Assembler::relocCode(void *_dst, sysuint_t addressBase) const
{
	// Copy code to virtual memory (this is a given _dst pointer).
	uint8_t *dst = reinterpret_cast<uint8_t *>(_dst);

	size_t coff = this->_buffer.getOffset();

	// We are copying the exact size of the generated code. Extra code for trampolines
	// is generated on-the-fly by relocator (this code doesn't exist at the moment).
	memcpy(dst, this->_buffer.getData(), coff);

#ifdef ASMJIT_X64
	// Trampoline pointer.
	uint8_t *tramp = dst + coff;
#endif // ASMJIT_X64

	// Relocate all recorded locations.
	size_t i;
	size_t len = this->_relocData.size();

	for (i = 0; i < len; ++i)
	{
		const RelocData &r = this->_relocData[i];
		sysint_t val = 0;

#ifdef ASMJIT_X64
		// Whether to use trampoline, can be only used if relocation type is
		// kRelocAbsToRel.
		bool useTrampoline = false;
#endif // ASMJIT_X64

		// Be sure that reloc data structure is correct.
		//ASMJIT_ASSERT((size_t)(r.offset + r.size) <= csize);

		switch (r.type)
		{
			case kRelocAbsToAbs:
				val = reinterpret_cast<sysint_t>(r.address);
				break;

			case kRelocRelToAbs:
				val = static_cast<sysint_t>(addressBase + r.destination);
				break;

			case kRelocAbsToRel:
			case kRelocTrampoline:
				val = static_cast<sysint_t>(reinterpret_cast<sysuint_t>(r.address) - (addressBase + static_cast<sysuint_t>(r.offset) + 4));

#ifdef ASMJIT_X64
				if (r.type == kRelocTrampoline && !IntUtil::isInt32(val))
				{
					val = static_cast<sysint_t>(reinterpret_cast<sysuint_t>(tramp) - (reinterpret_cast<sysuint_t>(_dst) + static_cast<sysuint_t>(r.offset) + 4));
					useTrampoline = true;
				}
#endif // ASMJIT_X64
				break;

			default:
				ASMJIT_ASSERT(0);
		}

		switch (r.size)
		{
			case 4:
				*reinterpret_cast<int32_t *>(dst + r.offset) = static_cast<int32_t>(val);
				break;

			case 8:
				*reinterpret_cast<int64_t *>(dst + r.offset) = static_cast<int64_t>(val);
				break;

			default:
				ASMJIT_ASSERT(0);
		}

#ifdef ASMJIT_X64
		if (useTrampoline)
		{
			if (this->getLogger())
				this->getLogger()->logFormat("; Trampoline from %p -> %p\n", reinterpret_cast<int8_t *>(addressBase) + r.offset, r.address);

			X64TrampolineWriter::writeTrampoline(tramp, reinterpret_cast<uint64_t>(r.address));
			tramp += X64TrampolineWriter::kSizeTotal;
		}
#endif // ASMJIT_X64
	}

#ifdef ASMJIT_X64
	return static_cast<size_t>(tramp - dst);
#else
	return coff;
#endif // ASMJIT_X64
}

// ============================================================================
// [AsmJit::Assembler - EmbedLabel]
// ============================================================================

void X86Assembler::embedLabel(const Label &label)
{
	ASMJIT_ASSERT(label.getId() != kInvalidValue);
	if (!this->canEmit())
		return;

	LabelData &l_data = this->_labels[label.getId() & kOperandIdValueMask];
	RelocData r_data;

	if (this->_logger)
		this->_logger->logFormat(sizeof(sysint_t) == 4 ? ".dd L.%u\n" : ".dq L.%u\n", static_cast<uint32_t>(label.getId()) & kOperandIdValueMask);

	r_data.type = kRelocRelToAbs;
	r_data.size = sizeof(sysint_t);
	r_data.offset = this->getOffset();
	r_data.destination = 0;

	if (l_data.offset != -1)
		// Bound label.
		r_data.destination = l_data.offset;
	else
	{
		// Non-bound label. Need to chain.
		LabelLink *link = this->_newLabelLink();

		link->prev = l_data.links;
		link->offset = this->getOffset();
		link->displacement = 0;
		link->relocId = this->_relocData.size();

		l_data.links = link;
	}

	this->_relocData.push_back(r_data);

	// Emit dummy intptr_t (4 or 8 bytes that depends on address size).
	this->_emitIntPtrT(0);
}

// ============================================================================
// [AsmJit::Assembler - Align]
// ============================================================================

void X86Assembler::align(uint32_t m)
{
	if (!this->canEmit())
		return;

	if (this->_logger)
		this->_logger->logFormat("%s.align %u\n", this->_logger->getInstructionPrefix(), static_cast<unsigned>(m));

	if (!m)
		return;

	if (m > 64)
	{
		ASMJIT_ASSERT(0);
		return;
	}

	sysint_t i = m - (this->getOffset() % m);
	if (static_cast<uint32_t>(i) == m)
		return;

	if (this->_properties & (1 << kX86PropertyOptimizedAlign))
	{
		const X86CpuInfo *ci = X86CpuInfo::getGlobal();

		// NOPs optimized for Intel:
		//   Intel 64 and IA-32 Architectures Software Developer's Manual
		//   - Volume 2B 
		//   - Instruction Set Reference N-Z
		//     - NOP

		// NOPs optimized for AMD:
		//   Software Optimization Guide for AMD Family 10h Processors (Quad-Core)
		//   - 4.13 - Code Padding with Operand-Size Override and Multibyte NOP

		// Intel and AMD.
		static const uint8_t nop1[] = { 0x90 };
		static const uint8_t nop2[] = { 0x66, 0x90 };
		static const uint8_t nop3[] = { 0x0F, 0x1F, 0x00 };
		static const uint8_t nop4[] = { 0x0F, 0x1F, 0x40, 0x00 };
		static const uint8_t nop5[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
		static const uint8_t nop6[] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
		static const uint8_t nop7[] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };
		static const uint8_t nop8[] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
		static const uint8_t nop9[] = { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };

		// AMD.
		static const uint8_t nop10[] = { 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
		static const uint8_t nop11[] = { 0x66, 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };

		const uint8_t *p;
		sysint_t n;

		if (ci->getVendorId() == kCpuIntel && ((ci->getFamily() & 0x0F) == 6 || (ci->getFamily() & 0x0F) == 15))
		{
			do
			{
				switch (i)
				{
					case 1:
						p = nop1;
						n = 1;
						break;
					case 2:
						p = nop2;
						n = 2;
						break;
					case 3:
						p = nop3;
						n = 3;
						break;
					case 4:
						p = nop4;
						n = 4;
						break;
					case 5:
						p = nop5;
						n = 5;
						break;
					case 6:
						p = nop6;
						n = 6;
						break;
					case 7:
						p = nop7;
						n = 7;
						break;
					case 8:
						p = nop8;
						n = 8;
						break;
					default:
						p = nop9;
						n = 9;
				}

				i -= n;
				do
				{
					this->_emitByte(*p++);
				} while (--n);
			} while (i);

			return;
		}

		if (ci->getVendorId() == kCpuAmd && ci->getFamily() >= 0x0F)
		{
			do
			{
				switch (i)
				{
					case 1:
						p = nop1;
						n = 1;
						break;
					case 2:
						p = nop2;
						n = 2;
						break;
					case 3:
						p = nop3;
						n = 3;
						break;
					case 4:
						p = nop4;
						n = 4;
						break;
					case 5:
						p = nop5;
						n = 5;
						break;
					case 6:
						p = nop6; n = 6;
						break;
					case 7:
						p = nop7;
						n = 7;
						break;
					case 8:
						p = nop8;
						n = 8;
						break;
					case 9:
						p = nop9;
						n = 9;
						break;
					case 10:
						p = nop10;
						n = 10;
						break;
					default:
						p = nop11;
						n = 11;
				}

				i -= n;
				do
				{
					this->_emitByte(*p++);
				} while (--n);
			} while (i);

			return;
		}
#ifdef ASMJIT_X86
		// Legacy NOPs, 0x90 with 0x66 prefix.
		do
		{
			switch (i)
			{
				default:
					this->_emitByte(0x66);
					--i;
				case 3:
					this->_emitByte(0x66);
					--i;
				case 2:
					this->_emitByte(0x66);
					--i;
				case 1:
					this->_emitByte(0x90);
					--i;
			}
		} while(i);
#endif
	}

	// Legacy NOPs, only 0x90. In 64-bit mode, we can't use 0x66 prefix.
	do
	{
		this->_emitByte(0x90);
	} while (--i);
}

// ============================================================================
// [AsmJit::Assembler - Label]
// ============================================================================

Label X86Assembler::newLabel()
{
	Label label;
	label._base.id = static_cast<uint32_t>(this->_labels.size()) | kOperandIdTypeLabel;

	LabelData l_data;
	l_data.offset = -1;
	l_data.links = nullptr;
	this->_labels.push_back(l_data);

	return label;
}

void X86Assembler::registerLabels(size_t count)
{
	// Duplicated newLabel() code, but we are not creating Label instances.
	LabelData l_data;
	l_data.offset = -1;
	l_data.links = nullptr;

	for (size_t i = 0; i < count; ++i)
		this->_labels.push_back(l_data);
}

void X86Assembler::bind(const Label &label)
{
	// Only labels created by newLabel() can be used by Assembler.
	ASMJIT_ASSERT(label.getId() != kInvalidValue);
	// Never go out of bounds.
	ASMJIT_ASSERT((label.getId() & kOperandIdValueMask) < this->_labels.size());

	// Get label data based on label id.
	LabelData &l_data = this->_labels[label.getId() & kOperandIdValueMask];

	// Label can be bound only once.
	ASMJIT_ASSERT(l_data.offset == -1);

	// Log.
	if (this->_logger)
		this->_logger->logFormat("L.%u:\n", static_cast<uint32_t>(label.getId()) & kOperandIdValueMask);

	sysint_t pos = this->getOffset();

	LabelLink *link = l_data.links;
	LabelLink *prev = nullptr;

	while (link)
	{
		sysint_t offset = link->offset;

		if (link->relocId != -1)
			// If linked label points to RelocData then instead of writing relative
			// displacement to assembler stream, we will write it to RelocData.
			this->_relocData[link->relocId].destination += pos;
		else
		{
			// Not using relocId, this means that we overwriting real displacement
			// in assembler stream.
			int32_t patchedValue = static_cast<int32_t>(pos - offset + link->displacement);
			uint32_t size = this->getByteAt(offset);

			// Only these size specifiers are allowed.
			ASMJIT_ASSERT(size == 1 || size == 4);

			if (size == 4)
				this->setInt32At(offset, patchedValue);
			else // if (size == 1)
			{
				if (IntUtil::isInt8(patchedValue))
					this->setByteAt(offset, static_cast<uint8_t>(static_cast<int8_t>(patchedValue)));
				else
					// Fatal error.
					this->setError(kErrorIllegalShortJump);
			}
		}

		prev = link->prev;
		link = prev;
	}

	// Chain unused links.
	link = l_data.links;
	if (link)
	{
		if (!prev)
			prev = link;

		prev->prev = this->_unusedLinks;
		this->_unusedLinks = link;
	}

	// Unlink label if it was linked.
	l_data.offset = pos;
	l_data.links = nullptr;
}

// ============================================================================
// [AsmJit::Assembler - Make]
// ============================================================================

void *X86Assembler::make()
{
	// Do nothing on error state or when no instruction was emitted.
	if (this->_error || !this->getCodeSize())
		return nullptr;

	void *p;
	this->_error = this->_context->generate(&p, this);
	return p;
}

} // AsmJit namespace

// [Api-End]
#include "../core/apiend.h"