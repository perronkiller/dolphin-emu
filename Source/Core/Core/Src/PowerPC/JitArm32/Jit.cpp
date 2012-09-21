// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include <map>

#include "Common.h"
#include "../../HLE/HLE.h"
#include "../../Core.h"
#include "../../PatchEngine.h"
#include "../../CoreTiming.h"
#include "../../ConfigManager.h"
#include "../PowerPC.h"
#include "../Profiler.h"
#include "../PPCTables.h"
#include "../PPCAnalyst.h"
#include "../../HW/Memmap.h"
#include "../../HW/GPFifo.h"
#include "Jit.h"
#include "JitArm_Tables.h"
#include "ArmEmitter.h"
#include "../JitInterface.h"

using namespace ArmGen;
using namespace PowerPC;

static int CODE_SIZE = 1024*1024*32;
namespace CPUCompare
{
	extern u32 m_BlockStart;
}

void JitArm::Init()
{
	AllocCodeSpace(CODE_SIZE);
	blocks.Init();
	asm_routines.Init();
	gpr.Init(this);
	jo.enableBlocklink = false;
	jo.optimizeGatherPipe = false;
	gpr.SetEmitter(this);
}

void JitArm::ClearCache() 
{
	ClearCodeSpace();
	blocks.Clear();
}

void JitArm::Shutdown()
{
	FreeCodeSpace();
	blocks.Shutdown();
	asm_routines.Shutdown();
}

// This is only called by Default() in this file. It will execute an instruction with the interpreter functions.
void JitArm::WriteCallInterpreter(UGeckoInstruction inst)
{
	gpr.Flush();
//	fpr.Flush(FLUSH_ALL);
	NOP();
	Interpreter::_interpreterInstruction instr = GetInterpreterOp(inst);
	ARMABI_CallFunctionC((void*)instr, inst.hex);
	gpr.SetEmitter(this);
	gpr.ReloadPPC();
}
void JitArm::unknown_instruction(UGeckoInstruction inst)
{
	PanicAlert("unknown_instruction %08x - Fix me ;)", inst.hex);
}

void JitArm::Default(UGeckoInstruction _inst)
{
	WriteCallInterpreter(_inst.hex);
}

void JitArm::HLEFunction(UGeckoInstruction _inst)
{
	printf("Trying to HLE a call, can't do this yet\n");
}

void JitArm::DoNothing(UGeckoInstruction _inst)
{
	// Yup, just don't do anything.

}

static const bool ImHereDebug = false;
static const bool ImHereLog = false;
static std::map<u32, int> been_here;

static void ImHere()
{
	static File::IOFile f;
	if (ImHereLog)
	{
		if (!f)
		{
			f.Open("log32.txt", "w");
		}
		fprintf(f.GetHandle(), "%08x\n", PC);
	}
	if (been_here.find(PC) != been_here.end())
	{
		been_here.find(PC)->second++;
		if ((been_here.find(PC)->second) & 1023)
			return;
	}
	DEBUG_LOG(DYNA_REC, "I'm here - PC = %08x , LR = %08x", PC, LR);
	been_here[PC] = 1;
}

void JitArm::Cleanup()
{
	if (jo.optimizeGatherPipe && js.fifoBytesThisBlock > 0)
		ARMABI_CallFunction((void *)&GPFifo::CheckGatherPipe);
}
void JitArm::DoDownCount()
{
	ARMReg A = gpr.GetReg();
	ARMReg B = gpr.GetReg();
	ARMABI_MOVI2R(A, Mem(&CoreTiming::downcount));
	LDR(B, A);
	if(js.downcountAmount < 255) // We can enlarge this if we used rotations
	{
		SUBS(B, B, js.downcountAmount);
		STR(A, B);
	}
	else
	{
		ARMReg C = gpr.GetReg();
		ARMABI_MOVI2R(C, js.downcountAmount);
		SUBS(B, B, C);
		STR(A, B);
	}
	gpr.Unlock(A, B);
}
void JitArm::WriteExitDestInR(ARMReg Reg) 
{
	ARMReg A = gpr.GetReg();
	ARMABI_MOVI2R(A, (u32)&PC);
	STR(A, Reg);
	gpr.Unlock(Reg); // This was locked in the instruction beforehand.
	Cleanup();
	DoDownCount();
	ARMABI_MOVI2R(A, (u32)asm_routines.dispatcher);
	B(A);
	gpr.Unlock(A);
}
void JitArm::WriteRfiExitDestInR(ARMReg Reg) 
{
	ARMReg A = gpr.GetReg();
	ARMABI_MOVI2R(A, (u32)&PC);
	STR(A, Reg);
	gpr.Unlock(Reg); // This was locked in the instruction beforehand
	Cleanup();
	DoDownCount();

	ARMABI_MOVI2R(A, (u32)asm_routines.testExceptions);
	B(A);
	gpr.Unlock(A);
}
void JitArm::WriteExceptionExit()
{
	ARMReg A = gpr.GetReg(false);
	Cleanup();
	DoDownCount();

	ARMABI_MOVI2R(A, (u32)asm_routines.testExceptions);
	B(A);
}
void JitArm::WriteExit(u32 destination, int exit_num)
{
	Cleanup();

	DoDownCount(); 

	//If nobody has taken care of this yet (this can be removed when all branches are done)
	JitBlock *b = js.curBlock;
	b->exitAddress[exit_num] = destination;
	b->exitPtrs[exit_num] = GetWritableCodePtr();
	
	// Link opportunity!
	int block = blocks.GetBlockNumberFromStartAddress(destination);
	if (block >= 0 && jo.enableBlocklink) 
	{
		// It exists! Joy of joy!
		B(blocks.GetBlock(block)->checkedEntry);
		b->linkStatus[exit_num] = true;
	}
	else 
	{
		ARMABI_MOVI2M((u32)&PC, destination); // Watch out! This uses R14 and R12!
		ARMReg A = gpr.GetReg(false);
		ARMABI_MOVI2R(A, (u32)asm_routines.dispatcher);
		B(A);	
	}
	printf("Jump Out: %08x: Available block: %s\n", destination, (block >= 0)
	? "True" : "False");
}

void STACKALIGN JitArm::Run()
{
	CompiledCode pExecAddr = (CompiledCode)asm_routines.enterCode;
	pExecAddr();
}

void JitArm::SingleStep()
{
	CompiledCode pExecAddr = (CompiledCode)asm_routines.enterCode;
	pExecAddr();
}

void JitArm::Trace()
{
	char regs[500] = "";
	char fregs[750] = "";

#ifdef JIT_LOG_GPR
	for (int i = 0; i < 32; i++)
	{
		char reg[50];
		sprintf(reg, "r%02d: %08x ", i, PowerPC::ppcState.gpr[i]);
		strncat(regs, reg, 500);
	}
#endif

#ifdef JIT_LOG_FPR
	for (int i = 0; i < 32; i++)
	{
		char reg[50];
		sprintf(reg, "f%02d: %016x ", i, riPS0(i));
		strncat(fregs, reg, 750);
	}
#endif	

	DEBUG_LOG(DYNA_REC, "JIT64 PC: %08x SRR0: %08x SRR1: %08x CRfast: %02x%02x%02x%02x%02x%02x%02x%02x FPSCR: %08x MSR: %08x LR: %08x %s %s", 
		PC, SRR0, SRR1, PowerPC::ppcState.cr_fast[0], PowerPC::ppcState.cr_fast[1], PowerPC::ppcState.cr_fast[2], PowerPC::ppcState.cr_fast[3], 
		PowerPC::ppcState.cr_fast[4], PowerPC::ppcState.cr_fast[5], PowerPC::ppcState.cr_fast[6], PowerPC::ppcState.cr_fast[7], PowerPC::ppcState.fpscr, 
		PowerPC::ppcState.msr, PowerPC::ppcState.spr[8], regs, fregs);
}

void STACKALIGN JitArm::Jit(u32 em_address)
{
	if (GetSpaceLeft() < 0x10000 || blocks.IsFull() || Core::g_CoreStartupParameter.bJITNoBlockCache)
	{
		ClearCache();
	}

	printf("Addr: %08x\n", em_address);
	int block_num = blocks.AllocateBlock(em_address);
	JitBlock *b = blocks.GetBlock(block_num);
	const u8* BlockPtr = DoJit(em_address, &code_buffer, b);
	blocks.FinalizeBlock(block_num, jo.enableBlocklink, BlockPtr);
}


const u8* JitArm::DoJit(u32 em_address, PPCAnalyst::CodeBuffer *code_buf, JitBlock *b)
{
	int blockSize = code_buf->GetSize();

	// Memory exception on instruction fetch
	bool memory_exception = false;

	// A broken block is a block that does not end in a branch
	bool broken_block = false;

	if (Core::g_CoreStartupParameter.bEnableDebugging)
	{
		// Comment out the following to disable breakpoints (speed-up)
		blockSize = 1;
		broken_block = true;
		Trace();
	}

	if (em_address == 0)
	{
		Core::SetState(Core::CORE_PAUSE);
		PanicAlert("ERROR: Compiling at 0. LR=%08x CTR=%08x", LR, CTR);
	}

	if (Core::g_CoreStartupParameter.bMMU && (em_address & JIT_ICACHE_VMEM_BIT))
	{
		if (!Memory::TranslateAddress(em_address, Memory::FLAG_OPCODE))
		{
			// Memory exception occurred during instruction fetch
			memory_exception = true;
		}
	}


	int size = 0;
	js.isLastInstruction = false;
	js.blockStart = em_address;
	js.fifoBytesThisBlock = 0;
	js.curBlock = b;
	js.block_flags = 0;
	js.cancel = false;

	// Analyze the block, collect all instructions it is made of (including inlining,
	// if that is enabled), reorder instructions for optimal performance, and join joinable instructions.
	u32 nextPC = em_address;
	u32 merged_addresses[32];
	const int capacity_of_merged_addresses = sizeof(merged_addresses) / sizeof(merged_addresses[0]);
	int size_of_merged_addresses = 0;
	if (!memory_exception)
	{
		// If there is a memory exception inside a block (broken_block==true), compile up to that instruction.
		nextPC = PPCAnalyst::Flatten(em_address, &size, &js.st, &js.gpa, &js.fpa, broken_block, code_buf, blockSize, merged_addresses, capacity_of_merged_addresses, size_of_merged_addresses);
	}
	PPCAnalyst::CodeOp *ops = code_buf->codebuffer;

	const u8 *start = GetCodePtr(); // TODO: Test if this or AlignCode16 make a difference from GetCodePtr
	b->checkedEntry = start;
	b->runCount = 0;

	// Downcount flag check, Only valid for linked blocks
	/*Fixupbranch skip = B_CC(CC_GE);
	ARMABI_MOVI2M((u32)&PC, js.blockStart);
	ARMABI_MOVI2R(R14, (u32)asm_routines.doTiming);
	B(14);
	SetJumpTarget(skip);*/

	const u8 *normalEntry = GetCodePtr();
	b->normalEntry = normalEntry;

	if(ImHereDebug)
		ARMABI_CallFunction((void *)&ImHere); //Used to get a trace of the last few blocks before a crash, sometimes VERY useful
	if (js.fpa.any)
	{
		// This block uses FPU - needs to add FP exception bailout
		ARMReg A = gpr.GetReg();
		ARMReg RB = gpr.GetReg();
		ARMReg C = gpr.GetReg();
		ARMABI_MOVI2R(A, (u32)&PowerPC::ppcState.msr); // R0
		ARMABI_MOVI2R(RB, (u32)&PC); // R1
		Operand2 Shift(2, 10); // 1 << 13
		ARMABI_MOVI2R(C, js.blockStart); // R3
		LDR(A, A);
		TST(A, Shift);
		FixupBranch b1 = B_CC(CC_NEQ);
		STR(RB, C);
		ARMABI_MOVI2R(A, (u32)asm_routines.fpException);
		B(A);
		SetJumpTarget(b1);
		gpr.Unlock(A, RB, C);	
	}
	// Conditionally add profiling code.
	if (Profiler::g_ProfileBlocks) {
		ARMReg A = gpr.GetReg();
		ARMReg B = gpr.GetReg();
		ARMABI_MOVI2R(A, (u32)&b->runCount); // Load in to register
		LDR(B, A); // Load the actual value in to R11.
		ADD(B, B, 1); // Add one to the value
		STR(A, B); // Now store it back in the memory location 
		// get start tic
		PROFILER_QUERY_PERFORMANCE_COUNTER(&b->ticStart);
		gpr.Unlock(A, B);
	}
	gpr.Start(js.gpa);
	js.downcountAmount = 0;
	if (!Core::g_CoreStartupParameter.bEnableDebugging)
	{
		for (int i = 0; i < size_of_merged_addresses; ++i)
		{
			const u32 address = merged_addresses[i];
			js.downcountAmount += PatchEngine::GetSpeedhackCycles(address);
		}
	}

	js.skipnext = false;
	js.blockSize = size;
	js.compilerPC = nextPC;
	// Translate instructions
	printf("OPS: %08x\n", size);
	for (int i = 0; i < (int)size; i++)
	{
		js.compilerPC = ops[i].address;
		js.op = &ops[i];
		js.instructionNumber = i;
		const GekkoOPInfo *opinfo = ops[i].opinfo;
		js.downcountAmount += (opinfo->numCyclesMinusOne + 1);

		if (i == (int)size - 1)
		{
			// WARNING - cmp->branch merging will screw this up.
			js.isLastInstruction = true;
			js.next_inst = 0;
			if (Profiler::g_ProfileBlocks) {
				// CAUTION!!! push on stack regs you use, do your stuff, then pop
				PROFILER_VPUSH;
				// get end tic
				PROFILER_QUERY_PERFORMANCE_COUNTER(&b->ticStop);
				// tic counter += (end tic - start tic)
				PROFILER_ADD_DIFF_LARGE_INTEGER(&b->ticCounter, &b->ticStop, &b->ticStart);
				PROFILER_VPOP;
			}
		}
		else
		{
			// help peephole optimizations
			js.next_inst = ops[i + 1].inst;
			js.next_compilerPC = ops[i + 1].address;
		}
		
		if (!ops[i].skip)
		{
			printf("Start: %08x OP '%s' Info\n",GetCodePtr(),  PPCTables::GetInstructionName(ops[i].inst));
			/*GekkoOPInfo* Info = GetOpInfo(ops[i].inst.hex);
				printf("\tOuts\n");
				if (Info->flags & FL_OUT_A)
					printf("\t-OUT_A: %x\n", ops[i].inst.RA);
				if(Info->flags & FL_OUT_D)
					printf("\t-OUT_D: %x\n", ops[i].inst.RD);
				printf("\tIns\n");
				// A, AO, B, C, S
				if(Info->flags & FL_IN_A)
					printf("\t-IN_A: %x\n", ops[i].inst.RA);
				if(Info->flags & FL_IN_A0)
					printf("\t-IN_A0: %x\n", ops[i].inst.RA);
				if(Info->flags & FL_IN_B)
					printf("\t-IN_B: %x\n", ops[i].inst.RB);
				if(Info->flags & FL_IN_C)
					printf("\t-IN_C: %x\n", ops[i].inst.RC);
				if(Info->flags & FL_IN_S)
					printf("\t-IN_S: %x\n", ops[i].inst.RS);*/
				gpr.Analyze(ops[i].inst);
				JitArmTables::CompileInstruction(ops[i]);
		}
	}
	if(broken_block)
	{
		WriteExit(nextPC, 0);
	}
			
	b->flags = js.block_flags;
	b->codeSize = (u32)(GetCodePtr() - normalEntry);
	b->originalSize = size;

	Flush();
	return start;
}

void ArmJit(u32 *em_address)
{	
	jitarm->Jit(*em_address);
}