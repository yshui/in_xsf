// [AsmJit]
// Complete JIT Assembler for C++ Language.
//
// [License]
// Zlib - See COPYING file in this package.

#define ASMJIT_EXPORTS

// [Dependencies - AsmJit]
#include "../core/intutil.h"
#include "../core/stringutil.h"

#include "../x86/x86assembler.h"
#include "../x86/x86compiler.h"
#include "../x86/x86compilercontext.h"
#include "../x86/x86compilerfunc.h"
#include "../x86/x86compileritem.h"
#include "../x86/x86util.h"

// [Api-Begin]
#include "../core/apibegin.h"

namespace AsmJit
{

// ============================================================================
// [AsmJit::CompilerUtil]
// ============================================================================

bool CompilerUtil::isStack16ByteAligned()
{
	// Stack is always aligned to 16-bytes when using 64-bit OS.
	bool result = sizeof(uintptr_t) == 8;

	// Modern Linux, APPLE and UNIX guarantees stack alignment to 16 bytes by
	// default. I'm really not sure about all UNIX operating systems, because
	// 16-byte alignment is an addition to an older specification.
#if defined(__linux__) ||  defined(__linux) || defined(linux) || defined(__unix__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DARWIN__) || defined(__APPLE__)
	result = true;
#endif // __linux__

	return result;
}

// ============================================================================
// [AsmJit::X86Compiler - Construction / Destruction]
// ============================================================================

X86Compiler::X86Compiler(Context *context) : Compiler(context)
{
	this->_properties |= IntUtil::maskFromIndex(kX86PropertyOptimizedAlign);
}

X86Compiler::~X86Compiler()
{
}

// ============================================================================
// [AsmJit::Compiler - Function Builder]
// ============================================================================

X86CompilerFuncDecl *X86Compiler::newFunc_(uint32_t convention, uint32_t returnType, const uint32_t *arguments, uint32_t argumentsCount)
{
	ASMJIT_ASSERT(!this->_func);

	X86CompilerFuncDecl *func = Compiler_newItem<X86CompilerFuncDecl>(this);

	this->_func = func;
	this->_varNameId = 0;

	func->setPrototype(convention, returnType, arguments, argumentsCount);
	this->addItem(func);

	this->bind(func->_entryLabel);
	func->_createVariables();

	return func;
}

X86CompilerFuncDecl *X86Compiler::endFunc()
{
	X86CompilerFuncDecl *func = this->getFunc();
	ASMJIT_ASSERT(func);

	this->bind(func->_exitLabel);
	this->addItem(func->_end);

	func->setFuncFlag(kFuncFlagIsFinished);
	this->_func = nullptr;

	return func;
}

// ============================================================================
// [AsmJit::Compiler - EmitInstruction]
// ============================================================================

static inline X86CompilerInst *X86Compiler_newInstruction(X86Compiler *self, uint32_t code, Operand *opData, uint32_t opCount)
{
	if (code >= _kX86InstJBegin && code <= _kX86InstJEnd)
	{
		void *p = self->_zoneMemory.alloc(sizeof(X86CompilerJmpInst));
		return new(p) X86CompilerJmpInst(self, code, opData, opCount);
	}
	else
	{
		void *p = self->_zoneMemory.alloc(sizeof(X86CompilerInst) + opCount * sizeof(Operand));
		return new(p) X86CompilerInst(self, code, opData, opCount);
	}
}

void X86Compiler::_emitInstruction(uint32_t code)
{
	X86CompilerInst *inst = X86Compiler_newInstruction(this, code, nullptr, 0);

	if (!inst)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(inst);

	if (this->_cc)
	{
		inst->_offset = this->_cc->_currentOffset;
		inst->prepare(*this->_cc);
	}
}

void X86Compiler::_emitInstruction(uint32_t code, const Operand *o0)
{
	Operand *operands = reinterpret_cast<Operand *>(this->_zoneMemory.alloc(sizeof(Operand)));

	if (!operands)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	operands[0] = *o0;
	X86CompilerInst *inst = X86Compiler_newInstruction(this, code, operands, 1);

	if (!inst)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(inst);

	if (this->_cc)
	{
		inst->_offset = this->_cc->_currentOffset;
		inst->prepare(*this->_cc);
	}
}

void X86Compiler::_emitInstruction(uint32_t code, const Operand *o0, const Operand *o1)
{
	Operand *operands = reinterpret_cast<Operand *>(this->_zoneMemory.alloc(2 * sizeof(Operand)));

	if (!operands)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	operands[0] = *o0;
	operands[1] = *o1;
	X86CompilerInst *inst = X86Compiler_newInstruction(this, code, operands, 2);

	if (!inst)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(inst);

	if (this->_cc)
	{
		inst->_offset = this->_cc->_currentOffset;
		inst->prepare(*this->_cc);
	}
}

void X86Compiler::_emitInstruction(uint32_t code, const Operand *o0, const Operand *o1, const Operand *o2)
{
	Operand *operands = reinterpret_cast<Operand *>(this->_zoneMemory.alloc(3 * sizeof(Operand)));

	if (!operands)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	operands[0] = *o0;
	operands[1] = *o1;
	operands[2] = *o2;
	X86CompilerInst *inst = X86Compiler_newInstruction(this, code, operands, 3);

	if (!inst)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(inst);

	if (this->_cc)
	{
		inst->_offset = this->_cc->_currentOffset;
		inst->prepare(*this->_cc);
	}
}

void X86Compiler::_emitInstruction(uint32_t code, const Operand *o0, const Operand *o1, const Operand *o2, const Operand *o3)
{
	Operand *operands = reinterpret_cast<Operand *>(this->_zoneMemory.alloc(4 * sizeof(Operand)));

	if (!operands)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	operands[0] = *o0;
	operands[1] = *o1;
	operands[2] = *o2;
	operands[3] = *o3;
	X86CompilerInst *inst = X86Compiler_newInstruction(this, code, operands, 4);

	if (!inst)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(inst);

	if (this->_cc)
	{
		inst->_offset = this->_cc->_currentOffset;
		inst->prepare(*this->_cc);
	}
}

void X86Compiler::_emitInstruction(uint32_t code, const Operand *o0, const Operand *o1, const Operand *o2, const Operand *o3, const Operand *o4)
{
	Operand *operands = reinterpret_cast<Operand *>(this->_zoneMemory.alloc(5 * sizeof(Operand)));

	if (!operands)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	operands[0] = *o0;
	operands[1] = *o1;
	operands[2] = *o2;
	operands[3] = *o3;
	operands[4] = *o4;
	X86CompilerInst *inst = X86Compiler_newInstruction(this, code, operands, 5);

	if (!inst)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(inst);

	if (this->_cc)
	{
		inst->_offset = this->_cc->_currentOffset;
		inst->prepare(*this->_cc);
	}
}

void X86Compiler::_emitJcc(uint32_t code, const Label *label, uint32_t hint)
{
	if (hint == kCondHintNone)
		this->_emitInstruction(code, label);
	else
	{
		Imm imm(hint);
		this->_emitInstruction(code, label, &imm);
	}
}

X86CompilerFuncCall *X86Compiler::_emitCall(const Operand *o0)
{
	X86CompilerFuncDecl *func = this->getFunc();

	if (!func)
	{
		this->setError(kErrorNoFunction);
		return nullptr;
	}

	X86CompilerFuncCall *call = Compiler_newItem<X86CompilerFuncCall>(this, func, o0);
	if (!call)
	{
		this->setError(kErrorNoHeapMemory);
		return nullptr;
	}

	this->addItem(call);

	return call;
}

void X86Compiler::_emitReturn(const Operand *first, const Operand *second)
{
	X86CompilerFuncDecl *func = this->getFunc();

	if (!func)
	{
		this->setError(kErrorNoFunction);
		return;
	}

	X86CompilerFuncRet *ret = Compiler_newItem<X86CompilerFuncRet>(this, func, first, second);

	if (!ret)
	{
		this->setError(kErrorNoHeapMemory);
		return;
	}

	this->addItem(ret);
}

// ============================================================================
// [AsmJit::Compiler - Align]
// ============================================================================

void X86Compiler::align(uint32_t m)
{
	this->addItem(Compiler_newItem<X86CompilerAlign>(this, m));
}

// ============================================================================
// [AsmJit::Compiler - Label]
// ============================================================================

Label X86Compiler::newLabel()
{
	Label label;
	label._base.id = static_cast<uint32_t>(_targets.getLength()) | kOperandIdTypeLabel;

	CompilerTarget *target = Compiler_newItem<X86CompilerTarget>(this, label);
	this->_targets.append(target);

	return label;
}

void X86Compiler::bind(const Label &label)
{
	uint32_t id = label.getId() & kOperandIdValueMask;

	ASMJIT_ASSERT(id != kInvalidValue);
	ASMJIT_ASSERT(id < this->_targets.getLength());

	this->addItem(this->_targets[id]);
}

// ============================================================================
// [AsmJit::Compiler - Variables]
// ============================================================================

X86CompilerVar *X86Compiler::_newVar(const char *name, uint32_t type, uint32_t size)
{
	X86CompilerVar *var = reinterpret_cast<X86CompilerVar *>(this->_zoneMemory.alloc(sizeof(X86CompilerVar)));
	if (!var)
		return nullptr;

	char nameBuffer[32];
	if (!name)
	{
		sprintf(nameBuffer, "var_%d", this->_varNameId);
		name = nameBuffer;
		++this->_varNameId;
	}

	var->_name = this->_zoneMemory.sdup(name);
	var->_id = static_cast<uint32_t>(this->_vars.getLength()) | kOperandIdTypeVar;

	var->_type = static_cast<uint8_t>(type);
	var->_class = x86VarInfo[type].getClass();
	var->_priority = 10;

	var->_isRegArgument = false;
	var->_isMemArgument = false;
	var->_isCalculated = false;
	var->_unused = 0;

	var->_size = size;

	var->firstItem = nullptr;
	var->lastItem = nullptr;
	var->funcScope = this->getFunc();
	var->funcCall = nullptr;

	var->homeRegisterIndex = kRegIndexInvalid;
	var->prefRegisterMask = 0;

	var->homeMemoryOffset = 0;
	var->homeMemoryData = nullptr;

	var->regIndex = kRegIndexInvalid;
	var->workOffset = kInvalidValue;

	var->nextActive = nullptr;
	var->prevActive = nullptr;

	var->state = kVarStateUnused;
	var->changed = false;
	var->saveOnUnuse = false;

	var->regReadCount = 0;
	var->regWriteCount = 0;
	var->regRwCount = 0;

	var->regGpbLoCount = 0;
	var->regGpbHiCount = 0;

	var->memReadCount = 0;
	var->memWriteCount = 0;
	var->memRwCount = 0;

	var->tPtr = nullptr;

	this->_vars.append(var);
	return var;
}

GpVar X86Compiler::newGpVar(uint32_t varType, const char *name)
{
	ASMJIT_ASSERT(varType < kX86VarTypeCount && (x86VarInfo[varType].getClass() & kX86VarClassGp));

#ifdef ASMJIT_X86
	if (x86VarInfo[varType].getSize() > 4)
	{
		varType = kX86VarTypeGpd;
		if (this->_logger)
			this->_logger->logString("*** COMPILER WARNING: QWORD variable translated to DWORD, FIX YOUR CODE! ***\n");
	}
#endif // ASMJIT_X86

	X86CompilerVar *var = this->_newVar(name, varType, x86VarInfo[varType].getSize());
	return var->asGpVar();
}

GpVar X86Compiler::getGpArg(uint32_t argIndex)
{
	X86CompilerFuncDecl *func = this->getFunc();
	GpVar var;

	if (func)
	{
		X86FuncDecl *decl = func->getDecl();

		if (argIndex < decl->getArgumentsCount())
		{
			X86CompilerVar *cv = func->getVar(argIndex);

			var._var.id = cv->getId();
			var._var.size = cv->getSize();
			var._var.regCode = x86VarInfo[cv->getType()].getCode();
			var._var.varType = cv->getType();
		}
	}

	return var;
}

MmVar X86Compiler::newMmVar(uint32_t varType, const char *name)
{
	ASMJIT_ASSERT(varType < kX86VarTypeCount && (x86VarInfo[varType].getClass() & kX86VarClassMm));

	X86CompilerVar *var = this->_newVar(name, varType, 8);
	return var->asMmVar();
}

MmVar X86Compiler::getMmArg(uint32_t argIndex)
{
	X86CompilerFuncDecl *func = this->getFunc();
	MmVar var;

	if (func)
	{
		const X86FuncDecl *decl = func->getDecl();

		if (argIndex < decl->getArgumentsCount())
		{
			X86CompilerVar *cv = func->getVar(argIndex);

			var._var.id = cv->getId();
			var._var.size = cv->getSize();
			var._var.regCode = x86VarInfo[cv->getType()].getCode();
			var._var.varType = cv->getType();
		}
	}

	return var;
}

XmmVar X86Compiler::newXmmVar(uint32_t varType, const char *name)
{
	ASMJIT_ASSERT(varType < kX86VarTypeCount && (x86VarInfo[varType].getClass() & kX86VarClassXmm));

	X86CompilerVar *var = this->_newVar(name, varType, 16);
	return var->asXmmVar();
}

XmmVar X86Compiler::getXmmArg(uint32_t argIndex)
{
	X86CompilerFuncDecl *func = this->getFunc();
	XmmVar var;

	if (func)
	{
		const X86FuncDecl *decl = func->getDecl();

		if (argIndex < decl->getArgumentsCount())
		{
			X86CompilerVar *cv = func->getVar(argIndex);

			var._var.id = cv->getId();
			var._var.size = cv->getSize();
			var._var.regCode = x86VarInfo[cv->getType()].getCode();
			var._var.varType = cv->getType();
		}
	}

	return var;
}

void X86Compiler::_vhint(Var &var, uint32_t hintId, uint32_t hintValue)
{
	if (var.getId() == kInvalidValue)
		return;

	X86CompilerVar *cv = this->_getVar(var.getId());
	ASMJIT_ASSERT(cv);

	X86CompilerHint *item = Compiler_newItem<X86CompilerHint>(this, cv, hintId, hintValue);
	this->addItem(item);
}

void X86Compiler::alloc(Var &var)
{
	this->_vhint(var, kVarHintAlloc, kInvalidValue);
}

void X86Compiler::alloc(Var &var, uint32_t regIndex)
{
	if (regIndex > 31)
		return;

	this->_vhint(var, kVarHintAlloc, IntUtil::maskFromIndex(regIndex));
}

void X86Compiler::alloc(Var &var, const Reg &reg)
{
	this->_vhint(var, kVarHintAlloc, IntUtil::maskFromIndex(reg.getRegIndex()));
}

void X86Compiler::save(Var &var)
{
	this->_vhint(var, kVarHintSave, kInvalidValue);
}

void X86Compiler::spill(Var &var)
{
	this->_vhint(var, kVarHintSpill, kInvalidValue);
}

void X86Compiler::unuse(Var &var)
{
	this->_vhint(var, kVarHintUnuse, kInvalidValue);
}

uint32_t X86Compiler::getPriority(Var &var) const
{
	if (var.getId() == kInvalidValue)
		return kInvalidValue;

	X86CompilerVar *vdata = this->_getVar(var.getId());
	ASMJIT_ASSERT(vdata);

	return vdata->getPriority();
}

void X86Compiler::setPriority(Var &var, uint32_t priority)
{
	if (var.getId() == kInvalidValue)
		return;

	X86CompilerVar *vdata = this->_getVar(var.getId());
	ASMJIT_ASSERT(vdata);

	if (priority > 100)
		priority = 100;
	vdata->_priority = static_cast<uint8_t>(priority);
}

bool X86Compiler::getSaveOnUnuse(Var &var) const
{
	if (var.getId() == kInvalidValue)
		return false;

	X86CompilerVar *vdata = this->_getVar(var.getId());
	ASMJIT_ASSERT(vdata);

	return !!vdata->saveOnUnuse;
}

void X86Compiler::setSaveOnUnuse(Var &var, bool value)
{
	if (var.getId() == kInvalidValue)
		return;

	X86CompilerVar *vdata = this->_getVar(var.getId());
	ASMJIT_ASSERT(vdata);

	vdata->saveOnUnuse = value;
}

void X86Compiler::rename(Var &var, const char *name)
{
	if (var.getId() == kInvalidValue)
		return;

	X86CompilerVar *vdata = this->_getVar(var.getId());
	ASMJIT_ASSERT(vdata);

	vdata->_name = this->_zoneMemory.sdup(name);
}

// ============================================================================
// [AsmJit::Compiler - State]
// ============================================================================

X86CompilerState *X86Compiler::_newState(uint32_t memVarsCount)
{
	X86CompilerState *state = reinterpret_cast<X86CompilerState *>(this->_zoneMemory.alloc(sizeof(X86CompilerState) + memVarsCount * sizeof(void *)));
	return state;
}

// ============================================================================
// [AsmJit::Compiler - Make]
// ============================================================================

void *X86Compiler::make()
{
	X86Assembler x86Asm(this->_context);

	x86Asm._properties = this->_properties;
	x86Asm.setLogger(this->_logger);

	this->serialize(x86Asm);

	if (this->getError())
		return nullptr;

	if (x86Asm.getError())
	{
		this->setError(x86Asm.getError());
		return nullptr;
	}

	void *result = x86Asm.make();

	if (this->_logger)
		this->_logger->logFormat("*** COMPILER SUCCESS - Wrote %u bytes, code: %u, trampolines: %u.\n\n", static_cast<unsigned>(x86Asm.getCodeSize()), static_cast<unsigned>(x86Asm.getOffset()),
			static_cast<unsigned>(x86Asm.getTrampolineSize()));

	return result;
}

void X86Compiler::serialize(Assembler &a)
{
	X86CompilerContext x86Context(this);
	X86Assembler &x86Asm = static_cast<X86Assembler &>(a);

	CompilerItem *start = this->_first;
	CompilerItem *stop = nullptr;

	// Register all labels.
	x86Asm.registerLabels(this->_targets.getLength());

	// Make code.
	for (;;)
	{
		this->_cc = nullptr;

		// ------------------------------------------------------------------------
		// [Find Function]
		// ------------------------------------------------------------------------

		for (;;)
		{
			if (!start)
				return;

			if (start->getType() == kCompilerItemFuncDecl)
				break;

			start->emit(x86Asm);
			start = start->getNext();
		}

		// ------------------------------------------------------------------------
		// [Setup CompilerContext]
		// ------------------------------------------------------------------------

		stop = static_cast<X86CompilerFuncDecl *>(start)->getEnd();

		x86Context._func = static_cast<X86CompilerFuncDecl *>(start);
		x86Context._start = start;
		x86Context._stop = stop;
		x86Context._extraBlock = stop->getPrev();

		// Detect whether the function generation was finished.
		if (!x86Context._func->isFinished() || !x86Context._func->getEnd()->getPrev())
		{
			this->setError(kErrorIncompleteFunction);
			return;
		}

		// ------------------------------------------------------------------------
		// Step 1:
		// - Assign/increment offset of each item.
		// - Extract variables from instructions.
		// - Prepare variables for register allocator:
		//   - Update read(r) / write(w) / read/write(x) statistics.
		//   - Update register / memory usage statistics.
		//   - Find scope (first / last item) of variables.
		// ------------------------------------------------------------------------

		CompilerItem *cur;
		for (cur = start; ; cur = cur->getNext())
		{
			cur->prepare(x86Context);
			if (cur == stop)
				break;
		}

		// We set compiler context also to Compiler so newly emitted instructions 
		// can call CompilerItem::prepare() on itself.
		this->_cc = &x86Context;

		// ------------------------------------------------------------------------
		// Step 2:
		// - Translate special instructions (imul, cmpxchg8b, ...).
		// - Alloc registers.
		// - Translate forward jumps.
		// - Alloc memory operands (variables related).
		// - Emit function prolog.
		// - Emit function epilog.
		// - Patch memory operands (variables related).
		// - Dump function prototype and variable statistics (if enabled).
		// ------------------------------------------------------------------------

		// Translate special instructions and run alloc registers.
		cur = start;

		do
		{
			do
			{
				// Assign current offset of each item back to CompilerContext.
				x86Context._currentOffset = cur->_offset;
				// Assign previous item to compiler so each variable spill/alloc will
				// be emitted before.
				this->_current = cur->getPrev();

				cur = cur->translate(x86Context);
			} while (cur);

			x86Context._isUnreachable = true;

			size_t len = x86Context._backCode.getLength();
			while (x86Context._backPos < len)
			{
				cur = x86Context._backCode[x86Context._backPos++]->getNext();
				if (!cur->isTranslated())
					break;

				cur = nullptr;
			}
		} while (cur);

		// Translate forward jumps.
		ForwardJumpData *j = x86Context._forwardJumps;
		while (j)
		{
			x86Context._assignState(j->state);
			this->_current = j->inst->getPrev();
			j->inst->doJump(x86Context);
			j = j->next;
		}

		// Alloc memory operands (variables related).
		x86Context._allocMemoryOperands();

		// Emit function prolog / epilog.
		x86Context.getFunc()->_preparePrologEpilog(x86Context);

		this->_current = x86Context._func->getEntryTarget();
		x86Context.getFunc()->_emitProlog(x86Context);

		this->_current = x86Context._func->getExitTarget();
		x86Context.getFunc()->_emitEpilog(x86Context);

		// Patch memory operands (variables related).
		this->_current = this->_last;
		x86Context._patchMemoryOperands(start, stop);

		// Dump function prototype and variable statistics (if enabled).
		if (this->_logger)
			x86Context.getFunc()->_dumpFunction(x86Context);

		// ------------------------------------------------------------------------
		// Hack: need to register labels that was created by the Step 2.
		// ------------------------------------------------------------------------

		if (x86Asm._labels.size() < this->_targets.getLength())
			x86Asm.registerLabels(this->_targets.getLength() - x86Asm._labels.size());

		CompilerItem *extraBlock = x86Context._extraBlock;

		// ------------------------------------------------------------------------
		// Step 3:
		// - Emit instructions to Assembler stream.
		// ------------------------------------------------------------------------

		for (cur = start; ; cur = cur->getNext())
		{
			cur->emit(x86Asm);
			if (cur == extraBlock)
				break;
		}

		// ------------------------------------------------------------------------
		// Step 4:
		// - Emit everything else (post action).
		// ------------------------------------------------------------------------

		for (cur = start; ; cur = cur->getNext())
		{
			cur->post(x86Asm);
			if (cur == extraBlock)
				break;
		}

		start = extraBlock->getNext();
		x86Context._clear();
	}
}

} // AsmJit namespace

// [Api-End]
#include "../core/apiend.h"
