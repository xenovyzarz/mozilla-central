/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=99:
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(jsion_baseline_helpers_x64_h__) && defined(JS_ION)
#define jsion_baseline_helpers_x64_h__

#include "ion/IonMacroAssembler.h"
#include "ion/BaselineRegisters.h"
#include "ion/BaselineIC.h"

namespace js {
namespace ion {

// Distance from rsp to the top Value inside an IC stub (this is the return address).
static const size_t ICStackValueOffset = sizeof(void *);

inline void
EmitRestoreTailCallReg(MacroAssembler &masm)
{
    masm.pop(BaselineTailCallReg);
}

inline void
EmitCallIC(CodeOffsetLabel *patchOffset, MacroAssembler &masm)
{
    // Move ICEntry offset into BaselineStubReg
    CodeOffsetLabel offset = masm.movWithPatch(ImmWord(-1), BaselineStubReg);
    *patchOffset = offset;

    // Load stub pointer into BaselineStubReg
    masm.movq(Operand(BaselineStubReg, (int32_t) ICEntry::offsetOfFirstStub()),
              BaselineStubReg);

    // Load stubcode pointer from BaselineStubEntry into BaselineTailCallReg
    // BaselineTailCallReg will always be unused in the contexts where ICs are called.
    masm.movq(Operand(BaselineStubReg, (int32_t) ICStub::offsetOfStubCode()),
              BaselineTailCallReg);

    // Call the stubcode.
    masm.call(BaselineTailCallReg);
}

inline void
EmitEnterTypeMonitorIC(MacroAssembler &masm)
{
    // This is expected to be called from within an IC, when BaselineStubReg
    // is properly initialized to point to the stub.
    masm.movq(Operand(BaselineStubReg, (int32_t) ICMonitoredStub::offsetOfFirstMonitorStub()),
              BaselineStubReg);

    // Load stubcode pointer from BaselineStubEntry into BaselineTailCallReg.
    masm.movq(Operand(BaselineStubReg, (int32_t) ICStub::offsetOfStubCode()),
              BaselineTailCallReg);

    // Jump to the stubcode.
    masm.jmp(Operand(BaselineTailCallReg));
}

inline void
EmitReturnFromIC(MacroAssembler &masm)
{
    masm.ret();
}

inline void
EmitTailCallVM(IonCode *target, MacroAssembler &masm, uint32_t argSize)
{
    // We an assume during this that R0 and R1 have been pushed.
    masm.movq(BaselineFrameReg, ScratchReg);
    masm.addq(Imm32(BaselineFrame::FramePointerOffset), ScratchReg);
    masm.subq(BaselineStackReg, ScratchReg);

    // Store frame size without VMFunction arguments for GC marking.
    masm.movq(ScratchReg, rdx);
    masm.subq(Imm32(argSize), rdx);
    masm.storePtr(rdx, Address(BaselineFrameReg, BaselineFrame::reverseOffsetOfFrameSize()));

    // Push frame descriptor and perform the tail call.
    masm.makeFrameDescriptor(ScratchReg, IonFrame_BaselineJS);
    masm.push(ScratchReg);
    masm.push(BaselineTailCallReg);
    masm.jmp(target);
}

inline void
EmitCreateStubFrameDescriptor(MacroAssembler &masm, Register reg)
{
    // Compute stub frame size. We have to add two pointers: the stub reg and previous
    // frame pointer pushed by EmitEnterStubFrame.
    masm.movq(BaselineFrameReg, reg);
    masm.addq(Imm32(sizeof(void *) * 2), reg);
    masm.subq(BaselineStackReg, reg);

    masm.makeFrameDescriptor(reg, IonFrame_BaselineStub);
}

inline void
EmitCallVM(IonCode *target, MacroAssembler &masm)
{
    EmitCreateStubFrameDescriptor(masm, ScratchReg);
    masm.push(ScratchReg);
    masm.call(target);
}

inline void
EmitEnterStubFrame(MacroAssembler &masm, Register)
{
    EmitRestoreTailCallReg(masm);

    // Compute frame size.
    masm.movq(BaselineFrameReg, ScratchReg);
    masm.addq(Imm32(BaselineFrame::FramePointerOffset), ScratchReg);
    masm.subq(BaselineStackReg, ScratchReg);

    masm.storePtr(ScratchReg, Address(BaselineFrameReg, BaselineFrame::reverseOffsetOfFrameSize()));

    // Push frame descriptor and return address.
    masm.makeFrameDescriptor(ScratchReg, IonFrame_BaselineJS);
    masm.push(ScratchReg);
    masm.push(BaselineTailCallReg);

    // Save old frame pointer, stack pointer and stub reg.
    masm.push(BaselineStubReg);
    masm.push(BaselineFrameReg);
    masm.mov(BaselineStackReg, BaselineFrameReg);
}

inline void
EmitLeaveStubFrame(MacroAssembler &masm, bool calledIntoIon = false)
{
    // Ion frames do not save and restore the frame pointer. If we called
    // into Ion, we have to restore the stack pointer from the frame descriptor.
    // If we performed a VM call, the descriptor has been popped already so
    // in that case we use the frame pointer.
    if (calledIntoIon) {
        masm.pop(ScratchReg);
        masm.shrq(Imm32(FRAMESIZE_SHIFT), ScratchReg);
        masm.addq(ScratchReg, BaselineStackReg);
    } else {
        masm.mov(BaselineFrameReg, BaselineStackReg);
    }

    masm.pop(BaselineFrameReg);
    masm.pop(BaselineStubReg);

    // Load the return address.
    masm.pop(BaselineTailCallReg);

    // Discard the frame descriptor.
    masm.pop(ScratchReg);

    // Push return address so that the stack matches the state before
    // entering the stub frame.
    masm.push(BaselineTailCallReg);
}

inline void
EmitStowICValues(MacroAssembler &masm, int values)
{
    JS_ASSERT(values >= 0 && values <= 2);
    switch(values) {
      case 1:
        // Stow R0
        masm.pop(BaselineTailCallReg);
        masm.pushValue(R0);
        masm.push(BaselineTailCallReg);
        break;
      case 2:
        // Stow R0 and R1
        masm.pop(BaselineTailCallReg);
        masm.pushValue(R0);
        masm.pushValue(R1);
        masm.push(BaselineTailCallReg);
        break;
    }
}

inline void
EmitUnstowICValues(MacroAssembler &masm, int values)
{
    JS_ASSERT(values >= 0 && values <= 2);
    switch(values) {
      case 1:
        // Unstow R0
        masm.pop(BaselineTailCallReg);
        masm.popValue(R0);
        masm.push(BaselineTailCallReg);
        break;
      case 2:
        // Untow R0 and R1
        masm.pop(BaselineTailCallReg);
        masm.popValue(R1);
        masm.popValue(R0);
        masm.push(BaselineTailCallReg);
        break;
    }
}

inline void
EmitCallTypeUpdateIC(MacroAssembler &masm, IonCode *code)
{
    // R0 contains the value that needs to be typechecked.

    // Save the current BaselineStubReg to stack
    masm.push(BaselineStubReg);

    // This is expected to be called from within an IC, when BaselineStubReg
    // is properly initialized to point to the stub.
    masm.movq(Operand(BaselineStubReg, (int32_t) ICUpdatedStub::offsetOfFirstUpdateStub()),
              BaselineStubReg);

    // Load stubcode pointer from BaselineStubReg into BaselineTailCallReg.
    masm.movq(Operand(BaselineStubReg, (int32_t) ICStub::offsetOfStubCode()),
              BaselineTailCallReg);

    // Call the stubcode.
    masm.call(BaselineTailCallReg);

    // Restore the old stub reg.
    masm.pop(BaselineStubReg);

    // The update IC will store 0 or 1 in R1.scratchReg() reflecting if the
    // value in R0 type-checked properly or not.
    Label success;
    masm.cmpPtr(R1.scratchReg(), ImmWord(1));
    masm.j(Assembler::Equal, &success);

    // If the IC failed, then call the update fallback function.
    EmitEnterStubFrame(masm, R1.scratchReg());

    masm.pushValue(R0);
    masm.push(BaselineStubReg);

    EmitCallVM(code, masm);
    EmitLeaveStubFrame(masm);

    // Success at end.
    masm.bind(&success);
}

inline void
EmitStubGuardFailure(MacroAssembler &masm)
{
    // NOTE: This routine assumes that the stub guard code left the stack in the
    // same state it was in when it was entered.

    // BaselineStubEntry points to the current stub.

    // Load next stub into BaselineStubReg
    masm.movq(Operand(BaselineStubReg, ICStub::offsetOfNext()), BaselineStubReg);

    // Load stubcode pointer from BaselineStubEntry into BaselineTailCallReg
    // BaselineTailCallReg will always be unused in the contexts where IC stub guards fail
    masm.movq(Operand(BaselineStubReg, ICStub::offsetOfStubCode()), BaselineTailCallReg);

    // Return address is already loaded, just jump to the next stubcode.
    masm.jmp(Operand(BaselineTailCallReg));
}


} // namespace ion
} // namespace js

#endif

