/* Copyright 2012-2013 Peter Goodman, all rights reserved. */

#include "granary/test.h"

#if CONFIG_DEBUG_RUN_TEST_CASES

#define TEST_CBR 1
#define TEST_JECXZ 1
#define TEST_LOOP 1


/// For each jump type, expand some macro with enough info to generate test
/// code.
///
/// Note: not all possible condition codes need to be set here (especially for
///       things like jle where one of two conditions can be met); only one
///       satisfying condition needs to be met.
#define FOR_EACH_CBR(macro, ...) \
    macro(jo, OF, ~0, ##__VA_ARGS__) \
    macro(jno, 0, ~OF, ##__VA_ARGS__) \
    macro(jb, CF, ~0, ##__VA_ARGS__) \
    macro(jnb, 0, ~CF, ##__VA_ARGS__) \
    macro(jz, ZF, ~0, ##__VA_ARGS__) \
    macro(jnz, 0, ~ZF, ##__VA_ARGS__) \
    macro(jbe, (CF | ZF), ~0, ##__VA_ARGS__) \
    macro(jnbe, 0, ~(CF | ZF), ##__VA_ARGS__) \
    macro(js, SF, ~0, ##__VA_ARGS__) \
    macro(jns, 0, ~SF, ##__VA_ARGS__) \
    macro(jp, PF, ~0, ##__VA_ARGS__) \
    macro(jnp, 0, ~PF, ##__VA_ARGS__) \
    macro(jl, SF, ~0, ##__VA_ARGS__) \
    macro(jnl, 0, ~SF, ##__VA_ARGS__) \
    macro(jle, (ZF | SF), ~0, ##__VA_ARGS__) \
    macro(jnle, 0, ~(ZF | SF), ##__VA_ARGS__)


/// Make a short and "far" test for a conditional branch opcode. The
/// the target of the short jumps needs to be outside of the basic
/// block, which will stop at ret (otherwise it would be optimized into
/// and in-block jump).
#define MAKE_CBR_TEST_FUNC(opcode, or_flag, and_flag) \
    static int direct_cti_ ## opcode ## _short_true(void) throw() { \
        register int64_t ret asm("rcx") = 0; \
        ASM( \
            "pushf;" \
            "movq $%c1, %%rax;" \
            "or %%rax, (%%rsp);" \
            "movq $%c2, %%rax;" \
            "and %%rax, (%%rsp);" \
            "popf;" \
            TO_STRING(opcode) " 1f;" \
            "mov $0, %%rax;" \
            "jmp 2f;" \
        "1:  mov $1, %%rax;" \
        "2:  movq %%rax, %0;" \
            : "=r"(ret) \
            : "i"(or_flag), "i"(and_flag) \
            : "rax" \
        ); \
        return ret; \
    } \
    \
    static int direct_cti_ ## opcode ## _short_false(void) throw() { \
        register int64_t ret asm("rcx") = 0; \
        ASM( \
            "pushf;" \
            "movq $%c1, %%rax;" \
            "or %%rax, (%%rsp);" \
            "movq $%c2, %%rax;" \
            "and %%rax, (%%rsp);" \
            "popf;" \
            TO_STRING(opcode) " 1f;" \
            "mov $1, %%rax;" \
            "jmp 2f;" \
        "1:  mov $0, %%rax;" \
        "2:  movq %%rax, %0;" \
            : "=r"(ret) \
            : "i"(~and_flag), "i"(~or_flag) \
            : "rax" \
        ); \
        return ret; \
    } \
    \
    static int direct_cti_ ## opcode ## _long_true(void) throw() { \
        register int64_t ret asm("rcx") = 0; \
        ASM( \
            "call 1f;" \
            "movq %%rax, %0;" \
            "jmp 2f;" \
        "1:" \
            "pushf;" \
            "movq $%c1, %%rax;" \
            "or %%rax, (%%rsp);" \
            "movq $%c2, %%rax;" \
            "and %%rax, (%%rsp);" \
            "popf;" \
            TO_STRING(opcode) " " TO_STRING(SYMBOL(granary_test_return_true)) ";" \
            "mov $0, %%rax;" \
            "ret;" \
        "2:" \
            : "=r"(ret) \
            : "i"(or_flag), "i"(and_flag) \
            : "rax" \
        ); \
        return ret; \
    } \
    \
    static int direct_cti_ ## opcode ## _long_false(void) throw() { \
        register int64_t ret asm("rcx") = 0; \
        ASM( \
            "call 1f;" \
            "movq %%rax, %0;" \
            "jmp 2f;" \
        "1:" \
            "pushf;" \
            "movq $%c1, %%rax;" \
            "or %%rax, (%%rsp);" \
            "movq $%c2, %%rax;" \
            "and %%rax, (%%rsp);" \
            "popf;" \
            TO_STRING(opcode) " " TO_STRING(SYMBOL(granary_test_return_false)) ";" \
            "mov $1, %%rax;" \
            "ret;" \
        "2:" \
            : "=r"(ret) \
            : "i"(~and_flag), "i"(~or_flag) \
            : "rax" \
        ); \
        return ret; \
    }


#define RUN_CBR_TEST_FUNC(opcode, or_flag, and_flag, code) \
    { \
        granary::app_pc short_cbr_true( \
            (granary::app_pc) direct_cti_ ## opcode ## _short_true); \
        granary::basic_block bb_short_true( \
            granary::code_cache::find(short_cbr_true, granary::TEST_POLICY)); \
        \
        granary::app_pc short_cbr_false( \
            (granary::app_pc) direct_cti_ ## opcode ## _short_false); \
        granary::basic_block bb_short_false( \
            granary::code_cache::find(short_cbr_false, granary::TEST_POLICY)); \
        \
        granary::app_pc long_cbr_true( \
            (granary::app_pc) direct_cti_ ## opcode ## _long_true); \
        granary::basic_block bb_long_true( \
            granary::code_cache::find(long_cbr_true, granary::TEST_POLICY)); \
        \
        granary::app_pc long_cbr_false( \
            (granary::app_pc) direct_cti_ ## opcode ## _long_false); \
        granary::basic_block bb_long_false( \
            granary::code_cache::find(long_cbr_false, granary::TEST_POLICY)); \
        \
        code \
    }


namespace test {

    enum {
        CF      = (1 << 0), // carry
        PF      = (1 << 2), // parity
        AF      = (1 << 4), // adjust
        ZF      = (1 << 6), // zero
        SF      = (1 << 7), // sign
        DF      = (1 << 10), // direction
        OF      = (1 << 11) // overflow
    };
    
#if TEST_CBR


    FOR_EACH_CBR(MAKE_CBR_TEST_FUNC)


    static void direct_cbrs_patched_correctly(void) {
        FOR_EACH_CBR(RUN_CBR_TEST_FUNC, {
            ASSERT(bb_short_true.call<bool>());
            ASSERT(bb_short_false.call<bool>());
            ASSERT(bb_long_true.call<bool>());
            ASSERT(bb_long_false.call<bool>());
        })
    }


    ADD_TEST(direct_cbrs_patched_correctly,
        "Test that targets of direct conditional branches are correctly patched.")

#endif

#if TEST_JECXZ

    static bool direct_jecxz_short_true(void) {
        register int64_t ret asm("rbx") = 0;
        ASM(
            "mov $0, %%rcx;"
            "jrcxz 1f;"
            "mov $0, %%rax;"
            "jmp 2f;"
        "1:  mov $1, %%rax;"
        "2:  movq %%rax, %0;"
            : "=r"(ret)
            :
            : "rax", "rcx"
        );
        return ret;
    }

    static bool direct_jecxz_short_false(void) {
        register int64_t ret asm("rbx") = 0;
        ASM(
            "mov $1, %%rcx;"
            "jrcxz 1f;"
            "mov $1, %%rax;"
            "jmp 2f;"
        "1:  mov $0, %%rax;"
        "2:  movq %%rax, %0;"
            : "=r"(ret)
            :
            : "rax", "rcx"
        );
        return ret;
    }


    /// Test jcxz/jecxz/jrcxz; Note: there is no far version of jrxcz.
    static void direct_jexcz_patched_correctly(void) {
        granary::app_pc jecxz_short_true((granary::app_pc) direct_jecxz_short_true);
        granary::basic_block bb_jecxz_short_true(granary::code_cache::find(
            jecxz_short_true, granary::TEST_POLICY));

        granary::app_pc jecxz_short_false((granary::app_pc) direct_jecxz_short_false);
        granary::basic_block bb_jecxz_short_false(granary::code_cache::find(
            jecxz_short_false, granary::TEST_POLICY));

        ASSERT(bb_jecxz_short_true.call<bool>());
        ASSERT(bb_jecxz_short_false.call<bool>());
    }


    ADD_TEST(direct_jexcz_patched_correctly,
        "Test that targets of direct jecxz branches are correctly patched.")

#endif

#if TEST_LOOP

    static int direct_loop_return_5(void) {
        register int64_t ret asm("rbx") = 0;
        ASM(
            "mov $0, %%rax;"
            "mov $5, %%rcx;" // count down from 5
        "1:  inc %%rax;"
            "loop 2f;"
            "jmp 3f;"
        "2:  jmp 1b;"
        "3:  movq %%rax, %0;"
            : "=r"(ret)
        );
        return ret;
    }


    /// Test jcxz/jecxz/jrcxz; Note: there is no far version of jrxcz.
    static void direct_loop_patched_correctly(void) {
        granary::app_pc loop_short_5((granary::app_pc) direct_loop_return_5);
        granary::basic_block bb_loop_short_5(granary::code_cache::find(
            loop_short_5, granary::TEST_POLICY));
        ASSERT(5 == bb_loop_short_5.call<int>());
    }


    ADD_TEST(direct_loop_patched_correctly,
        "Test that targets of direct loop and loopcc branches are correctly patched.")
#endif
}

#endif

