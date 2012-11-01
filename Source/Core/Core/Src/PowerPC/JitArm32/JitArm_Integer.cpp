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
#include "Common.h"
#include "Thunk.h"

#include "../../Core.h"
#include "../PowerPC.h"
#include "../../CoreTiming.h"
#include "../PPCTables.h"
#include "ArmEmitter.h"

#include "Jit.h"
#include "JitRegCache.h"
#include "JitAsm.h"
extern u32 Helper_Mask(u8 mb, u8 me);
// ADDI and RLWINMX broken for now

// Assumes that Sign and Zero flags were set by the last operation. Preserves all flags and registers.
// Jit64 ComputerRC is signed
// JIT64 GenerateRC is unsigned
void JitArm::GenerateRC(int cr) {
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[cr]);

	MOV(rB, 0x4); // Result > 0
	SetCC(CC_EQ); MOV(rB, 0x2); // Result == 0
	SetCC(CC_MI); MOV(rB, 0x8); // Result < 0
	SetCC();

	STRB(rA, rB, 0);
	gpr.Unlock(rA, rB);
}
void JitArm::ComputeRC(int cr) {
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast[cr]);

	MOV(rB, 0x2); // Result == 0
	SetCC(CC_LT); MOV(rB, 0x8); // Result < 0
	SetCC(CC_GT); MOV(rB, 0x4); // Result > 0
	SetCC();

	STRB(rA, rB, 0);
	gpr.Unlock(rA, rB);
}

void JitArm::addi(UGeckoInstruction inst)
{
	ARMReg RD = gpr.R(inst.RD);

	if (inst.RA)
	{
		ARMReg rA = gpr.GetReg(false);
		ARMReg RA = gpr.R(inst.RA);
		ARMABI_MOVI2R(rA, (u32)inst.SIMM_16);
		ADD(RD, RA, rA);
	}
	else
		ARMABI_MOVI2R(RD, inst.SIMM_16);
}
void JitArm::addis(UGeckoInstruction inst)
{
	ARMReg RD = gpr.R(inst.RD);
	if (inst.RA)
	{
		ARMReg rA = gpr.GetReg(false);
		ARMReg RA = gpr.R(inst.RA);
		ARMABI_MOVI2R(rA, inst.SIMM_16 << 16);
		ADD(RD, RA, rA);
	}
	else
		ARMABI_MOVI2R(RD, inst.SIMM_16 << 16);
}
void JitArm::addx(UGeckoInstruction inst)
{
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RB = gpr.R(inst.RB);
	ARMReg RD = gpr.R(inst.RD);
	ADDS(RD, RA, RB);
	if (inst.Rc) ComputeRC();
}
// Wrong - 28/10/2012
void JitArm::mulli(UGeckoInstruction inst)
{
	Default(inst); return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RD = gpr.R(inst.RD);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, inst.SIMM_16);
	MUL(RD, RA, rA);
	gpr.Unlock(rA);
}
void JitArm::ori(UGeckoInstruction inst)
{
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, inst.UIMM);
	ORR(RA, RS, rA);
	gpr.Unlock(rA);
}
void JitArm::oris(UGeckoInstruction inst)
{
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, inst.UIMM << 16);
	ORR(RA, RS, rA);
	gpr.Unlock(rA);
}
void JitArm::extshx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Integer)
	ARMReg RA, RS;
	RA = gpr.R(inst.RA);
	RS = gpr.R(inst.RS);
	SXTH(RA, RS);
	if (inst.RC){
		CMP(RA, 0);
		ComputeRC();
	}
}
void JitArm::extsbx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Integer)
	ARMReg RA, RS;
	RA = gpr.R(inst.RA);
	RS = gpr.R(inst.RS);
	SXTB(RA, RS);
	if (inst.RC){
		CMP(RA, 0);
		ComputeRC();
	}
}
void JitArm::cmp (UGeckoInstruction inst)
{
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RB = gpr.R(inst.RB);
	int crf = inst.CRFD;
	CMP(RA, RB);
	ComputeRC(crf);
}
void JitArm::cmpi(UGeckoInstruction inst)
{
	ARMReg RA = gpr.R(inst.RA);
	ARMReg rA = gpr.GetReg();
	int crf = inst.CRFD;
	ARMABI_MOVI2R(rA, inst.SIMM_16);
	CMP(RA, rA);
	gpr.Unlock(rA);
	ComputeRC(crf);
}
void JitArm::cmpli(UGeckoInstruction inst)
{
	ARMReg RA = gpr.R(inst.RA);
	ARMReg rA = gpr.GetReg();
	int crf = inst.CRFD;
	ARMABI_MOVI2R(rA, (u32)inst.UIMM);
	CMP(RA, rA);

	// Unsigned GenerateRC()
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, (u32)&PowerPC::ppcState.cr_fast);
	
	MOV(rB, 0x2); // Result == 0
	SetCC(CC_LO); MOV(rB, 0x8); // Result < 0
	SetCC(CC_HI); MOV(rB, 0x4); // Result > 0
	SetCC();

	SetJumpTarget(continue1);
	SetJumpTarget(continue2);
	STRB(rA, rB, crf);
	gpr.Unlock(rA, rB);

}
// Wrong - 27/10/2012
void JitArm::negx(UGeckoInstruction inst)
{
	INSTRUCTION_START
	JITDISABLE(Integer)
	Default(inst);return;
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RD = gpr.R(inst.RD);

	RSBS(RD, RA, 0);
	if (inst.Rc)
	{
		GenerateRC();
	}
	if (inst.OE)
	{
		BKPT(0x333);
		//GenerateOverflow();
	}
}
void JitArm::orx(UGeckoInstruction inst)
{
	ARMReg rA = gpr.R(inst.RA);
	ARMReg rS = gpr.R(inst.RS);
	ARMReg rB = gpr.R(inst.RB);
	ORRS(rA, rS, rB);
	if (inst.Rc)
		ComputeRC();
}
void JitArm::rlwimix(UGeckoInstruction inst)
{
	u32 mask = Helper_Mask(inst.MB,inst.ME);
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMReg rB = gpr.GetReg();
	ARMABI_MOVI2R(rA, mask);

	Operand2 Shift(32 - inst.SH, ROR, RS); // This rotates left, while ARM has only rotate right, so swap it.
	if (inst.Rc)
	{
		BIC (rB, RA, rA); // RA & ~mask
		AND (rA, rA, Shift);
		ORRS(RA, rB, rA);
		GenerateRC();	
	}
	else
	{
		BIC (rB, RA, rA); // RA & ~mask
		AND (rA, rA, Shift);
		ORR(RA, rB, rA);
	}
	gpr.Unlock(rA, rB);
}
void JitArm::rlwinmx(UGeckoInstruction inst)
{
	u32 mask = Helper_Mask(inst.MB,inst.ME);
	ARMReg RA = gpr.R(inst.RA);
	ARMReg RS = gpr.R(inst.RS);
	ARMReg rA = gpr.GetReg();
	ARMABI_MOVI2R(rA, mask);

	Operand2 Shift(32 - inst.SH, ROR, RS); // This rotates left, while ARM has only rotate right, so swap it.
	if (inst.Rc)
	{
		ANDS(RA, rA, Shift);
		GenerateRC();	
	}
	else
		AND (RA, rA, Shift);
	gpr.Unlock(rA);

	//m_GPR[inst.RA] = _rotl(m_GPR[inst.RS],inst.SH) & mask;
}
