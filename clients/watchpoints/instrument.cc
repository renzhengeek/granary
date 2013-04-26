/*
 * instrument.cc
 *
 *  Created on: 2013-04-20
 *      Author: pag
 */


#include "clients/watchpoints/instrument.h"

using namespace granary;

namespace client { namespace wp {

    /// Find memory operands that might need to be checked for watchpoints.
    /// If one is found, then num_ops is incremented, and the operand
    /// reference is stored in the passed array.
    void find_memory_operand(
        const operand_ref &op,
        watchpoint_tracker &tracker
    ) throw() {

        // in 64-bit mode, we'll ignore GS and FS-segmented addresses because
        // the offsets from those are generally not addresses.
        if(dynamorio::BASE_DISP_kind != op->kind
        || dynamorio::DR_SEG_GS == op->seg.segment
        || dynamorio::DR_SEG_FS == op->seg.segment) {
            return;
        }

        register_manager rm;
        const operand ref_to_op(*op);
        rm.kill(ref_to_op);

        // make sure we've got at least one general purpose register
        dynamorio::reg_id_t reg(rm.get_zombie());
        if(!reg) {
            return;
        }

        unsigned num_regs(0);
        do {
            if(dynamorio::DR_REG_RSP == reg
            IF_WP_IGNORE_FRAME_POINTER( || dynamorio::DR_REG_RBP == reg )) {
                return;
            }

            ++num_regs;
            reg = rm.get_zombie();
        } while(reg);

        // Two registers are used in the base/disp (base & index), thus this is
        // not an "implicit" operand to an instruction, and so we can replace
        // the operand.
        //
        // Note: there is one exception to this: XLAT / XLATB. This will be
        //       handled by noticing that the index reg is a 8 bit reg, and
        //       only the base reg of any base/disp type implicit operands
        //       can have a watched address.
        if(2 == num_regs) {
            tracker.can_replace[tracker.num_ops] = true;

        // This is a register that is not typically an implicitly used register
        // (one of the specialised general purpose regs). Note: this depends on
        // the ordering of regs in the enum. Specifically, this looks for
        // R8-R15 as being okay to alter.
        } else if(dynamorio::DR_REG_RDI < reg) {
            tracker.can_replace[tracker.num_ops] = true;

        // If either of the scale or index are non-zero, then it's not an
        // implicit operand. The exception to this is base/disp using RSP as the
        // base reg (e.g. CALL, RET, PUSH, etc.); these operands are all avoided
        // using a different check.
        } else if(0 != op->value.base_disp.disp
               || 0 != op->value.base_disp.scale) {
            tracker.can_replace[tracker.num_ops] = true;

        // We need to leave this operand alone; which means we are required to
        // save the original regs, then modify them in place.
        } else {
            tracker.can_replace[tracker.num_ops] = false;
        }

        tracker.ops[tracker.num_ops] = op;

        ++(tracker.num_ops);
    }


    /// Small state machine to track whether or not we can clobber the carry
    /// flag. The carry flag is relevant because we use the BT instruction to
    /// determine if the address is a watched address.
    void track_carry_flag(
        watchpoint_tracker &tracker,
        instruction in,
        bool &next_reads_carry_flag
    ) throw() {
        const unsigned eflags(dynamorio::instr_get_eflags(in));

        // assume flags do not propagate through RETs.
        if(in.is_return()) {
            next_reads_carry_flag = false;
            tracker.restore_carry_flag_before = false;
            tracker.restore_carry_flag_after = false;
            return;

        // for a specific propagation for CTIs.
        } else if(in.is_cti()) {
            next_reads_carry_flag = true;
            tracker.restore_carry_flag_before = true;
            tracker.restore_carry_flag_after = false;
            return;
        }

        // Read-after-write dependency.
        if(eflags & EFLAGS_READ_CF) {
            next_reads_carry_flag = true;

            tracker.restore_carry_flag_before = true;
            tracker.restore_carry_flag_after = false;

        // Output dependency.
        } else if(eflags & EFLAGS_WRITE_CF) {
            next_reads_carry_flag = false;

            tracker.restore_carry_flag_before = false;
            tracker.restore_carry_flag_after = false;

        // inherit
        } else {
            tracker.restore_carry_flag_before = false;
            tracker.restore_carry_flag_after = next_reads_carry_flag;
        }
    }


    /// Get a register that can be clobbered.
    dynamorio::reg_id_t watchpoint_tracker::get_zombie(void) throw() {
        dynamorio::reg_id_t reg;
        do {
            reg = live_regs.get_zombie();
        } while(reg && used_regs.is_undead(reg));

        if(reg) {
            used_regs.revive(reg);
        }

        return reg;
    }


    /// Get a register of a particular scale that can be clobbered.
    dynamorio::reg_id_t watchpoint_tracker::get_zombie(
        dynamorio::reg_id_t scale
    ) throw() {
        dynamorio::reg_id_t reg;

        do {
            reg = live_regs.get_zombie(scale);
        } while(reg && used_regs.is_undead(reg));

        if(reg) {
            used_regs.revive(reg);
        }

        return reg;
    }


    /// Get a register that can be spilled.
    dynamorio::reg_id_t watchpoint_tracker::get_spill(void) throw() {
        return used_regs.get_zombie();
    }


    /// Get a register of a particular scale that can be spilled.
    dynamorio::reg_id_t watchpoint_tracker::get_spill(
        dynamorio::reg_id_t scale
    ) throw() {
        return used_regs.get_zombie(scale);
    }


    /// Perform watchpoint-specific mangling of an instruction.
    instruction mangle(
        instruction_list &ls,
        instruction in,
        watchpoint_tracker &tracker
    ) throw() {
        instruction ret(in);
        bool can_replace(false);
        bool update_can_replace(false);

        // mangle a push instruction.
        switch(in.op_code()) {

        case dynamorio::OP_push: {
            dynamorio::reg_id_t spill_reg(tracker.get_zombie());
            operand op = *tracker.ops[0];

            // a dead register is available.
            if(spill_reg) {
                const operand dead_reg(spill_reg);
                ret = ls.insert_before(in, mov_ld_(dead_reg, *op));
                ls.insert_before(in, push_(dead_reg));
                ret.set_pc(in.pc());

            // we need to spill a register to emulate the PUSH.
            } else {
                spill_reg = tracker.get_spill();
                const operand dead_reg(spill_reg);

                ret = ls.insert_before(in, lea_(reg::rsp, reg::rsp[-8]));
                ret.set_pc(in.pc());
                ls.insert_before(in, push_(dead_reg));
                ret = ls.insert_before(in, mov_ld_(dead_reg, *op));
                ls.insert_before(in, mov_st_(reg::rsp[8], dead_reg));
                ls.insert_before(in, pop_(dead_reg));
            }

            ls.remove(in);
            break;
        }

        // mark all operands as being non-changeable. XLAT/XLATB is the only
        // instruction that has an implicit operand with both a base and index
        // register. The trick here though is that RBX is the only possible
        // operand containing the watchpoint (that needs to be checked), as the
        // AL register is not wide enough to contain a watched address.
        case dynamorio::OP_xlat:
            update_can_replace = true;
            break;

        // optimisation for simple instructions that are typically used and
        // are known to have replaceable operands.
        case dynamorio::OP_mov_ld:
        case dynamorio::OP_mov_st:
        case dynamorio::OP_add:
        case dynamorio::OP_sub:
        case dynamorio::OP_inc:
        case dynamorio::OP_dec:
            update_can_replace = true;
            can_replace = true;
            break;

        default: break;
        }

        // update the replacability of the operands, if possible.
        if(update_can_replace) {
            for(unsigned i(0); i < MAX_NUM_OPERANDS; ++i) {
                tracker.can_replace[i] = can_replace;
            }
        }

        return ret;
    }


    /// Save the carry flag, if needed. We use the carry flag extensively. For
    /// example, the BT instruction can detect a watched address, and that sets
    /// the carry flag. Big enough rotations of RCL and RCR only modify the
    /// carry flag (which can be used to mask the tainted bits), and STC and CLC
    /// can set/clear the carry flag, respectively.
    static dynamorio::reg_id_t save_carry_flag(
        instruction_list &ls,
        instruction before,
        watchpoint_tracker &tracker,
        bool &spilled_carry_flag
    ) throw() {

        dynamorio::reg_id_t carry_flag(0);
        if(!tracker.restore_carry_flag_before
        && !tracker.restore_carry_flag_after) {
            return carry_flag;
        }

        carry_flag = tracker.get_zombie(dynamorio::DR_REG_AL);
        if(!carry_flag) {
            carry_flag = tracker.get_spill(dynamorio::DR_REG_AL);
            ls.insert_before(before,
                push_(operand(carry_flag - (dynamorio::DR_REG_AL - 1))));
            spilled_carry_flag = true;
        }

        ls.insert_before(before,
            setcc_(dynamorio::OP_setg, operand(carry_flag)));

        return carry_flag;
    }


    /// Replace/update operands around the memory instruction. This will
    /// update the `labels` field of the `operand_tracker` with labels in
    /// instruction stream so that a `Watcher` can inject its own specific
    /// instrumentation in at those well-defined points. This will also
    /// update the `sources` and `dests` field appropriately so that the
    /// `Watcher`s read/write visitors can access operands containing the
    /// watched addresses.
    void visit_operands(
        granary::instruction_list &ls,
        granary::instruction in,
        watchpoint_tracker &tracker
    ) throw() {

        instruction before(ls.insert_before(in, label_()));
        instruction after(ls.insert_after(in, label_()));

        // save the carry flag
        bool spilled_carry_flag(false);
        dynamorio::reg_id_t carry_flag(save_carry_flag(
            ls, before, tracker, spilled_carry_flag));

        // update each operand
        bool spilled_op_reg[MAX_NUM_OPERANDS];
        dynamorio::reg_id_t op_reg[MAX_NUM_OPERANDS];

        enum {
            REG_8_OFFSET = dynamorio::DR_REG_AL - 1,
            REG_16_OFFSET = dynamorio::DR_REG_AX - 1,
            REG_OFFSET = (8 == NUM_HIGH_ORDER_BITS
                ? REG_8_OFFSET
                : REG_16_OFFSET),
            REG_SCALE = (8 == NUM_HIGH_ORDER_BITS
                ? dynamorio::DR_REG_AL
                : dynamorio::DR_REG_AX)
        };

        // constructor for the immediate value that is used to mask the watched
        // address.
        operand (*mov_mask_imm_)(uint64_t) = (8 == NUM_HIGH_ORDER_BITS
            ? int8_
            : int16_);

        // constructor for the instruction to jump around the watchpoint
        // instrumentation if a watched address isn't detected
        instruction (*jmp_around_)(dynamorio::opnd_t t) = (DISTINGUISHING_BIT
            ? jnb_
            : jb_);

        for(unsigned i(0); i < tracker.num_ops; ++i) {
            const operand_ref &op(tracker.ops[i]);
            const operand ref_to_op(*op);
            const bool can_change(tracker.can_replace[i]);

            // get a register where we can compute the watched address, store
            // the base or index reg of the operand, and potentially do other
            // things.
            //
            // We get 16-bit compatible regs so that we can set their bits to
            // all 1 or 0 (depending on user/kernel space) to mask the high
            // order bits of the address.
            op_reg[i] = tracker.get_zombie(REG_SCALE);
            if(!op_reg[i]) {
                op_reg[i] = tracker.get_spill(REG_SCALE);
                op_reg[i] -= REG_OFFSET;
                spilled_op_reg[i] = true;

                ls.insert_before(before, push_(operand(op_reg[i])));
            } else {
                op_reg[i] -= REG_OFFSET;
                spilled_op_reg[i] = false;
            }

            operand addr(op_reg[i]);

            // leave the original register alone
            if(can_change) {
                const_cast<operand_ref &>(op) = *addr;

            } else if(!op->value.base_disp.base_reg) {
                FAULT; // unknown condition.
            }

            // the resolved (potentially) watchpoint address.
            ls.insert_before(before, lea_(addr, ref_to_op));

            // check for a watchpoint.
            ls.insert_before(before,
                bt_(addr, int8_(DISTINGUISHING_BIT_OFFSET)));

            instruction not_a_watchpoint(label_());
            ls.insert_before(before,
                mangled(jmp_around_(instr_(not_a_watchpoint))));

            // we've found a watchpoint; note: we assume that watchpoint-
            // implementation instrumentation will not clobber the operands
            // from sources/dests.
            tracker.labels[i] = ls.insert_before(before, label_());
            if(op.is_source) {
                tracker.sources[i] = addr;
            } else {
                tracker.dests[i] = addr;
            }

            // save the original register value if we're not modifying the
            // original operand. If we can change the operand, then we already
            // have, so we don't need to worry about index/displacement of
            // this operand.
            dynamorio::reg_id_t unwatched_addr_reg(op_reg[i]);
            if(!can_change) {
                unwatched_addr_reg = op->value.base_disp.base_reg;
                ls.insert_before(before, mov_st_(
                    addr, operand(op->value.base_disp.base_reg)));
            }

            // mask the high order bits
            operand unwatched_addr(unwatched_addr_reg);
            ls.insert_before(before, bswap_(unwatched_addr));
            if(DISTINGUISHING_BIT) {
                ls.insert_before(before, mov_imm_(
                    operand(unwatched_addr_reg + REG_OFFSET),
                    mov_mask_imm_(0)));
            } else {
                ls.insert_before(before, mov_imm_(
                    operand(unwatched_addr_reg + REG_OFFSET),
                    mov_mask_imm_(-1)));
            }
            ls.insert_before(before, bswap_(unwatched_addr));

#if 0
            // mask / unwatch the address before the instruction is executed.
            // need to shift by `NUM_HIGH_ORDER_BITS + 1` so that we can mask
            // the watchpoint index.
            ls.insert_before(before, rcl_(
                unwatched_addr, int8_(NUM_HIGH_ORDER_BITS + 1)));

            if(DISTINGUISHING_BIT) {
                ls.insert_before(before, clc_());
                ls.insert_before(before, mov_imm_(
                    operand(unwatched_addr_reg + REG_16_OFFSET),
                    int16_(0)));
            } else {
                ls.insert_before(before, stc_());
                ls.insert_before(before, mov_imm_(
                    operand(unwatched_addr_reg + REG_16_OFFSET),
                    int16_(-1)));
            }

            ls.insert_before(before, rcr_(
                unwatched_addr, int8_(NUM_HIGH_ORDER_BITS + 1)));
#endif

            ls.insert_before(before, not_a_watchpoint);
        }

        // restore the carry flag before executing the instruction.
        if(tracker.restore_carry_flag_before) {
            ls.insert_before(before, rcl_(operand(carry_flag), int8_(64)));
        }

        // restore any spilled registers / clobbered registers.
        for(int i(tracker.num_ops); i --> 0; ) {
            const operand_ref &op(tracker.ops[i]);
            const bool can_change(tracker.can_replace[i]);
            operand addr(op_reg[i]);

            // restore the original register
            if(!can_change) {
                ls.insert_before(after, mov_st_(
                    operand(op->value.base_disp.base_reg), addr));
            }

            // unspill.
            if(spilled_op_reg[i]) {
                ls.insert_before(after, pop_(addr));
            }
        }

        // restore the carry flag after executing the instruction.
        if(tracker.restore_carry_flag_after) {
            ls.insert_before(after, rcl_(operand(carry_flag), int8_(64)));
        }

        if(spilled_carry_flag) {
            carry_flag -= (dynamorio::DR_REG_AL - 1);
            ls.insert_before(after, pop_(operand(carry_flag)));
        }
    }
}}