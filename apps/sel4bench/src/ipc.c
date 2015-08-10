/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(NICTA_GPL)
 */
/* This is very much a work in progress IPC benchmarking set. Goal is
   to eventually use this to replace the rest of the random benchmarking
   happening in this app with just what we need */

#include <autoconf.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <simple/simple.h>
#include <sched/sched.h>
#include <sel4/sel4.h>
#include <sel4bench/sel4bench.h>
#include <sel4utils/process.h>
#include <string.h>
#include <utils/util.h>
#include <vka/vka.h>

#include "benchmark.h"
#include "math.h"
#include "timing.h"

#define __SWINUM(x) ((x) & 0x00ffffff)

#define NUM_ARGS 2
#define WARMUPS 16
#define RUNS 16
#define OVERHEAD_RETRIES 4

#define NOPS ""

#define DO_CALL_X86(ep, tag, sys) do { \
    uint32_t ep_copy = ep; \
    asm volatile( \
        "movl %%esp, %%ecx \n"\
        "leal 1f, %%edx \n"\
        "1: \n" \
        sys" \n" \
        : \
         "+S" (tag), \
         "+b" (ep_copy) \
        : \
         "a" (seL4_SysCall) \
        : \
         "ecx", \
         "edx" \
    ); \
} while(0)
#define DO_CALL_10_X86(ep, tag, sys) do { \
    uint32_t ep_copy = ep; \
    asm volatile( \
        "pushl %%ebp \n"\
        "movl %%esp, %%ecx \n"\
        "leal 1f, %%edx \n"\
        "1: \n" \
        sys" \n" \
        "popl %%ebp \n"\
        : \
         "+S" (tag), \
         "+b" (ep_copy) \
        : \
         "a" (seL4_SysCall) \
        : \
         "ecx", \
         "edx", \
         "edi" \
    ); \
} while(0)
#define DO_SEND_X86(ep, tag, sys) do { \
    uint32_t ep_copy = ep; \
    uint32_t tag_copy = tag.words[0]; \
    asm volatile( \
        "movl %%esp, %%ecx \n"\
        "leal 1f, %%edx \n"\
        "1: \n" \
        sys" \n" \
        : \
         "+S" (tag_copy), \
         "+b" (ep_copy) \
        : \
         "a" (seL4_SysSend) \
        : \
         "ecx", \
         "edx" \
    ); \
} while(0)
#define DO_REPLY_WAIT_X86(ep, tag, sys) do { \
    uint32_t ep_copy = ep; \
    asm volatile( \
        "movl %%esp, %%ecx \n"\
        "leal 1f, %%edx \n"\
        "1: \n" \
        sys" \n" \
        : \
         "+S" (tag), \
         "+b" (ep_copy) \
        : \
         "a" (seL4_SysReplyWait) \
        : \
         "ecx", \
         "edx" \
    ); \
} while(0)
#define DO_REPLY_WAIT_10_X86(ep, tag, sys) do { \
    uint32_t ep_copy = ep; \
    asm volatile( \
        "pushl %%ebp \n"\
        "movl %%esp, %%ecx \n"\
        "leal 1f, %%edx \n"\
        "1: \n" \
        sys" \n" \
        "popl %%ebp \n"\
        : \
         "+S" (tag), \
         "+b" (ep_copy) \
        : \
         "a" (seL4_SysReplyWait) \
        : \
         "ecx", \
         "edx", \
         "edi" \
    ); \
} while(0)
#define DO_WAIT_X86(ep, sys) do { \
    uint32_t ep_copy = ep; \
    asm volatile( \
        "movl %%esp, %%ecx \n"\
        "leal 1f, %%edx \n"\
        "1: \n" \
        sys" \n" \
        : \
         "+b" (ep_copy) \
        : \
         "a" (seL4_SysWait) \
        : \
         "ecx", \
         "edx", \
         "esi" \
    ); \
} while(0)
#define DO_CALL_ARM(ep, tag, swi) do { \
    register seL4_Word dest asm("r0") = (seL4_Word)ep; \
    register seL4_MessageInfo_t info asm("r1") = tag; \
    register seL4_Word scno asm("r7") = seL4_SysCall; \
    asm volatile(NOPS swi NOPS \
        : "+r"(dest), "+r"(info) \
        : [swi_num] "i" __SWINUM(seL4_SysCall), "r"(scno) \
    ); \
} while(0)
#define DO_CALL_10_ARM(ep, tag, swi) do { \
    register seL4_Word dest asm("r0") = (seL4_Word)ep; \
    register seL4_MessageInfo_t info asm("r1") = tag; \
    register seL4_Word scno asm("r7") = seL4_SysCall; \
    asm volatile(NOPS swi NOPS \
        : "+r"(dest), "+r"(info) \
        : [swi_num] "i" __SWINUM(seL4_SysCall), "r"(scno) \
        : "r2", "r3", "r4", "r5" \
    ); \
} while(0)
#define DO_SEND_ARM(ep, tag, swi) do { \
    register seL4_Word dest asm("r0") = (seL4_Word)ep; \
    register seL4_MessageInfo_t info asm("r1") = tag; \
    register seL4_Word scno asm("r7") = seL4_SysSend; \
    asm volatile(NOPS swi NOPS \
        : "+r"(dest), "+r"(info) \
        : [swi_num] "i" __SWINUM(seL4_SysSend), "r"(scno) \
    ); \
} while(0)
#define DO_REPLY_WAIT_10_ARM(ep, tag, swi) do { \
    register seL4_Word src asm("r0") = (seL4_Word)ep; \
    register seL4_MessageInfo_t info asm("r1") = tag; \
    register seL4_Word scno asm("r7") = seL4_SysReplyWait; \
    asm volatile(NOPS swi NOPS \
        : "+r"(src), "+r"(info) \
        : [swi_num] "i" __SWINUM(seL4_SysReplyWait), "r"(scno) \
        : "r2", "r3", "r4", "r5" \
    ); \
} while(0)
#define DO_REPLY_WAIT_ARM(ep, tag, swi) do { \
    register seL4_Word src asm("r0") = (seL4_Word)ep; \
    register seL4_MessageInfo_t info asm("r1") = tag; \
    register seL4_Word scno asm("r7") = seL4_SysReplyWait; \
    asm volatile(NOPS swi NOPS \
        : "+r"(src), "+r"(info) \
        : [swi_num] "i" __SWINUM(seL4_SysReplyWait), "r"(scno) \
    ); \
} while(0)
#define DO_WAIT_ARM(ep, swi) do { \
    register seL4_Word src asm("r0") = (seL4_Word)ep; \
    register seL4_MessageInfo_t info asm("r1"); \
    register seL4_Word scno asm("r7") = seL4_SysWait; \
    asm volatile(NOPS swi NOPS \
        : "+r"(src), "=r"(info) \
        : [swi_num] "i" __SWINUM(seL4_SysWait), "r"(scno) \
    ); \
} while(0)

#define DO_CALL_X64(ep, tag, sys) do { \
    uint64_t ep_copy = ep; \
    asm volatile(\
            "movq   %%rsp, %%rcx \n" \
            "leaq   1f, %%rdx \n" \
            "1: \n" \
            sys" \n" \
            : \
            "+S" (tag), \
            "+b" (ep_copy) \
            : \
            "a" (seL4_SysCall) \
            : \
            "rcx", "rdx"\
            ); \
} while (0)

#define DO_CALL_10_X64(ep, tag, sys) do {\
    uint64_t ep_copy = ep; \
    seL4_Word mr0 = 0; \
    seL4_Word mr1 = 1; \
    register seL4_Word mr2 asm ("r8") = 2;  \
    register seL4_Word mr3 asm ("r9") = 3;  \
    register seL4_Word mr4 asm ("r10") = 4; \
    register seL4_Word mr5 asm ("r11") = 5; \
    register seL4_Word mr6 asm ("r12") = 6; \
    register seL4_Word mr7 asm ("r13") = 7; \
    register seL4_Word mr8 asm ("r14") = 8; \
    register seL4_Word mr9 asm ("r15") = 9; \
    asm volatile(                           \
            "push   %%rbp \n"               \
            "movq   %%rcx, %%rbp \n"        \
            "movq   %%rsp, %%rcx \n"        \
            "leaq   1f, %%rdx \n"           \
            "1: \n"                         \
            sys" \n"                        \
            "movq   %%rbp, %%rcx \n"        \
            "pop    %%rbp \n"               \
            :                               \
            "+S" (tag),                     \
            "+b" (ep_copy),                 \
            "+D" (mr0), "+c" (mr1), "+r" (mr2), "+r" (mr3), \
            "+r" (mr4), "+r" (mr5), "+r" (mr6), "+r" (mr7), \
            "+r" (mr8), "+r" (mr9)          \
            :                               \
            "a" (seL4_SysCall)              \
            :                               \
            "rdx"                           \
            );                              \
} while (0)

#define DO_SEND_X64(ep, tag, sys) do { \
    uint64_t ep_copy = ep; \
    uint32_t tag_copy = tag.words[0]; \
    asm volatile( \
            "movq   %%rsp, %%rcx \n" \
            "leaq   1f, %%rdx \n" \
            "1: \n" \
            sys" \n" \
            : \
            "+S" (tag_copy), \
            "+b" (ep_copy) \
            : \
            "a" (seL4_SysSend) \
            : \
            "rcx", "rdx" \
            ); \
} while (0)

#define DO_REPLY_WAIT_X64(ep, tag, sys) do { \
    uint64_t ep_copy = ep; \
    asm volatile( \
            "movq   %%rsp, %%rcx \n" \
            "leaq   1f, %%rdx \n" \
            "1: \n" \
            sys" \n" \
            : \
            "+S" (tag), \
            "+b" (ep_copy) \
            : \
            "a"(seL4_SysReplyWait) \
            : \
            "rcx", "rdx" \
            ); \
} while (0)

#define DO_REPLY_WAIT_10_X64(ep, tag, sys) do { \
    uint64_t ep_copy = ep;                      \
    seL4_Word mr0 = 0;                          \
    seL4_Word mr1 = -1;                         \
    register seL4_Word mr2 asm ("r8") = -2;     \
    register seL4_Word mr3 asm ("r9") = -3;     \
    register seL4_Word mr4 asm ("r10") = -4;    \
    register seL4_Word mr5 asm ("r11") = -5;    \
    register seL4_Word mr6 asm ("r12") = -6;    \
    register seL4_Word mr7 asm ("r13") = -7;    \
    register seL4_Word mr8 asm ("r14") = -8;    \
    register seL4_Word mr9 asm ("r15") = -9;    \
    asm volatile(                               \
            "push   %%rbp \n"                   \
            "movq   %%rcx, %%rbp \n"            \
            "movq   %%rsp, %%rcx \n"            \
            "leaq   1f, %%rdx \n"               \
            "1: \n"                             \
            sys" \n"                            \
            "movq   %%rbp, %%rcx \n"            \
            "pop    %%rbp \n"                   \
            :                                   \
            "+S" (tag),                         \
            "+b" (ep_copy),                     \
            "+D" (mr0), "+c" (mr1), "+r" (mr2), \
            "+r" (mr3), "+r" (mr4), "+r" (mr5), \
            "+r" (mr6), "+r" (mr7), "+r" (mr8), \
            "+r" (mr9)                          \
            :                                   \
            "a" (seL4_SysReplyWait)             \
            :                                   \
            "rdx"                               \
            );                                  \
} while (0)

#define DO_WAIT_X64(ep, sys) do { \
    uint64_t ep_copy = ep; \
    uint64_t tag = 0; \
    asm volatile( \
            "movq   %%rsp, %%rcx \n" \
            "leaq   1f, %%rdx \n" \
            "1: \n" \
            sys" \n" \
            : \
            "+S" (tag) ,\
            "+b" (ep_copy) \
            : \
            "a" (seL4_SysWait) \
            :  "rcx", "rdx" \
            ); \
} while (0)

#define READ_COUNTER_ARMV6(var) do { \
    asm volatile("b 2f\n\
                1:mrc p15, 0, %[counter], c15, c12," SEL4BENCH_ARM1136_COUNTER_CCNT "\n\
                  bx lr\n\
                2:sub r8, pc, #16\n\
                  .word 0xe7f000f0" \
        : [counter] "=r"(var) \
        : \
        : "r8", "lr"); \
} while(0)

#define READ_COUNTER_ARMV7(var) do { \
    asm volatile("mrc p15, 0, %0, c9, c13, 0\n" \
        : "=r"(var) \
    ); \
} while(0)

#define READ_COUNTER_X86(var) do { \
    uint32_t low, high; \
    asm volatile( \
        "movl $0, %%eax \n" \
        "movl $0, %%ecx \n" \
        "cpuid \n" \
        "rdtsc \n" \
        "movl %%edx, %0 \n" \
        "movl %%eax, %1 \n" \
        "movl $0, %%eax \n" \
        "movl $0, %%ecx \n" \
        "cpuid \n" \
        : \
         "=r"(high), \
         "=r"(low) \
        : \
        : "eax", "ebx", "ecx", "edx" \
    ); \
    (var) = (((uint64_t)high) << 32ull) | ((uint64_t)low); \
} while(0)

#define READ_COUNTER_X64_BEFORE(var) do { \
    uint32_t low, high; \
    asm volatile( \
            "cpuid \n" \
            "rdtsc \n" \
            "movl %%edx, %0 \n" \
            "movl %%eax, %1 \n" \
            : "=r"(high), "=r"(low) \
            : \
            : "%rax", "%rbx", "%rcx", "%rdx"); \
    (var) = (((uint64_t)high) << 32ull) | ((uint64_t)low); \
} while (0)

#define READ_COUNTER_X64_AFTER(var) do { \
    uint32_t low, high; \
    asm volatile( \
            "rdtscp \n" \
            "movl %%edx, %0 \n" \
            "movl %%eax, %1 \n" \
            "cpuid \n" \
            : "=r"(high), "=r"(low) \
            : \
            : "%rax", "rbx", "%rcx", "%rdx"); \
    (var) = (((uint64_t)high) << 32ull) | ((uint64_t)low); \
} while (0)

#if defined(CONFIG_ARCH_ARM)
#define DO_REAL_CALL(ep, tag) DO_CALL_ARM(ep, tag, "swi %[swi_num]")
#define DO_NOP_CALL(ep, tag) DO_CALL_ARM(ep, tag, "nop")
#define DO_REAL_REPLY_WAIT(ep, tag) DO_REPLY_WAIT_ARM(ep, tag, "swi %[swi_num]")
#define DO_NOP_REPLY_WAIT(ep, tag) DO_REPLY_WAIT_ARM(ep, tag, "nop")
#define DO_REAL_CALL_10(ep, tag) DO_CALL_10_ARM(ep, tag, "swi %[swi_num]")
#define DO_NOP_CALL_10(ep, tag) DO_CALL_10_ARM(ep, tag, "nop")
#define DO_REAL_REPLY_WAIT_10(ep, tag) DO_REPLY_WAIT_10_ARM(ep, tag, "swi %[swi_num]")
#define DO_NOP_REPLY_WAIT_10(ep, tag) DO_REPLY_WAIT_10_ARM(ep, tag, "nop")
#define DO_REAL_SEND(ep, tag) DO_SEND_ARM(ep, tag, "swi %[swi_num]")
#define DO_NOP_SEND(ep, tag) DO_SEND_ARM(ep, tag, "nop")
#define DO_REAL_WAIT(ep) DO_WAIT_ARM(ep, "swi %[swi_num]")
#define DO_NOP_WAIT(ep) DO_WAIT_ARM(ep, "nop")
#elif defined(CONFIG_ARCH_IA32)

#ifdef CONFIG_X86_64
#define DO_REAL_CALL(ep, tag) DO_CALL_X64(ep, tag, "sysenter")
#define DO_NOP_CALL(ep, tg) DO_CALL_X64(ep, tag, ".byte 0x90")
#define DO_REAL_REPLY_WAIT(ep, tag) DO_REPLY_WAIT_X64(ep, tag, "sysenter")
#define DO_NOP_REPLY_WAIT(ep, tag) DO_REPLY_WAIT_X64(ep, tag, ".byte 0x90")
#define DO_REAL_CALL_10(ep, tag) DO_CALL_10_X64(ep, tag, "sysenter")
#define DO_NOP_CALL_10(ep, tag) DO_CALL_10_X64(ep, tag, ".byte 0x90")
#define DO_REAL_REPLY_WAIT_10(ep, tag) DO_REPLY_WAIT_10_X64(ep, tag, "sysenter")
#define DO_NOP_REPLY_WAIT_10(ep, tag) DO_REPLY_WAIT_10_X64(ep, tag, ".byte 0x90")
#define DO_REAL_SEND(ep, tag) DO_SEND_X64(ep, tag, "sysenter")
#define DO_NOP_SEND(ep, tag) DO_SEND_X64(ep, tag, ".byte 0x90")
#define DO_REAL_WAIT(ep) DO_WAIT_X64(ep, "sysenter")
#define DO_NOP_WAIT(ep) DO_WAIT_X64(ep, ".byte 0x90")
#else
#define DO_REAL_CALL(ep, tag) DO_CALL_X86(ep, tag, "sysenter")
#define DO_NOP_CALL(ep, tag) DO_CALL_X86(ep, tag, ".byte 0x66\n.byte 0x90")
#define DO_REAL_REPLY_WAIT(ep, tag) DO_REPLY_WAIT_X86(ep, tag, "sysenter")
#define DO_NOP_REPLY_WAIT(ep, tag) DO_REPLY_WAIT_X86(ep, tag, ".byte 0x66\n.byte 0x90")
#define DO_REAL_CALL_10(ep, tag) DO_CALL_10_X86(ep, tag, "sysenter")
#define DO_NOP_CALL_10(ep, tag) DO_CALL_10_X86(ep, tag, ".byte 0x66\n.byte 0x90")
#define DO_REAL_REPLY_WAIT_10(ep, tag) DO_REPLY_WAIT_10_X86(ep, tag, "sysenter")
#define DO_NOP_REPLY_WAIT_10(ep, tag) DO_REPLY_WAIT_10_X86(ep, tag, ".byte 0x66\n.byte 0x90")
#define DO_REAL_SEND(ep, tag) DO_SEND_X86(ep, tag, "sysenter")
#define DO_NOP_SEND(ep, tag) DO_SEND_X86(ep, tag, ".byte 0x66\n.byte 0x90")
#define DO_REAL_WAIT(ep) DO_WAIT_X86(ep, "sysenter")
#define DO_NOP_WAIT(ep) DO_WAIT_X86(ep, ".byte 0x66\n.byte 0x90")
#endif
#else
#error Unsupported arch
#endif

#if defined(CONFIG_ARCH_ARM_V6)
#define READ_COUNTER_BEFORE READ_COUNTER_ARMV6
#define READ_COUNTER_AFTER  READ_COUNTER_ARMV6
#elif defined(CONFIG_ARCH_ARM_V7A)
#define READ_COUNTER_BEFORE READ_COUNTER_ARMV7
#define READ_COUNTER_AFTER  READ_COUNTER_ARMV7
#elif defined(CONFIG_ARCH_IA32)
#ifdef CONFIG_X86_64
#define READ_COUNTER_BEFORE READ_COUNTER_X64_BEFORE
#define READ_COUNTER_AFTER  READ_COUNTER_X64_AFTER
#define ALLOW_UNSTABLE_OVERHEAD
#else
#define READ_COUNTER_BEFORE READ_COUNTER_X86
#define READ_COUNTER_AFTER  READ_COUNTER_X86
#define ALLOW_UNSTABLE_OVERHEAD
#endif
#else
#error Unsupported arch
#endif

/* The fence is designed to try and prevent the compiler optimizing across code boundaries
   that we don't want it to. The reason for preventing optimization is so that things like
   overhead calculations aren't unduly influenced */
#define FENCE() asm volatile("" ::: "memory")


#define OVERHEAD_BENCH_PARAMS(n) { .name = n }

enum overhead_benchmarks {
    CALL_OVERHEAD,
    REPLY_WAIT_OVERHEAD,
    SEND_OVERHEAD,
    WAIT_OVERHEAD,
    CALL_10_OVERHEAD,
    REPLY_WAIT_10_OVERHEAD,
    /******/
    NOVERHEADBENCHMARKS
};

enum overheads {
    CALL_REPLY_WAIT_OVERHEAD,
    CALL_REPLY_WAIT_10_OVERHEAD,
    SEND_WAIT_OVERHEAD,
    /******/
    NOVERHEADS
};

typedef struct benchmark_params {
    const char* name;
    const char* caption;
    helper_func_t server_fn, client_fn;
    bool same_vspace;
    uint8_t server_prio, client_prio;
    enum overheads overhead_id;
} benchmark_params_t;

struct overhead_benchmark_params {
    const char* name;
};

typedef struct helper_thread {
    sel4utils_process_config_t config;
    sel4utils_process_t process;
    seL4_CPtr ep;
    seL4_CPtr result_ep;
    char *argv[NUM_ARGS];
    char argv_strings[NUM_ARGS][WORD_STRING_SIZE];
} helper_thread_t;

uint32_t ipc_call_func(int argc, char *argv[]);
uint32_t ipc_call_func2(int argc, char *argv[]);
uint32_t ipc_call_10_func(int argc, char *argv[]);
uint32_t ipc_call_10_func2(int argc, char *argv[]);
uint32_t ipc_replywait_func2(int argc, char *argv[]);
uint32_t ipc_replywait_func(int argc, char *argv[]);
uint32_t ipc_replywait_10_func2(int argc, char *argv[]);
uint32_t ipc_replywait_10_func(int argc, char *argv[]);
uint32_t ipc_send_func(int argc, char *argv[]);
uint32_t ipc_wait_func(int argc, char *argv[]);

/* array of benchmarks to run */
static const benchmark_params_t benchmark_params[] = {
    {
        .name        = "Intra-AS Call",
        .caption     = "Call+ReplyWait Intra-AS test 1",
        .client_fn   = ipc_call_func2,
        .server_fn   = ipc_replywait_func2,
        .same_vspace = true,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Intra-AS ReplyWait",
        .caption     = "Call+ReplyWait Intra-AS test 2",
        .client_fn   = ipc_call_func,
        .server_fn   = ipc_replywait_func,
        .same_vspace = true,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS Call",
        .caption     = "Call+ReplyWait Inter-AS test 1",
        .client_fn   = ipc_call_func2,
        .server_fn   = ipc_replywait_func2,
        .same_vspace = false,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS ReplyWait",
        .caption     = "Call+ReplyWait Inter-AS test 2",
        .client_fn   = ipc_call_func,
        .server_fn   = ipc_replywait_func,
        .same_vspace = false,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS Call (Low to High)",
        .caption     = "Call+ReplyWait Different prio test 1",
        .client_fn   = ipc_call_func2,
        .server_fn   = ipc_replywait_func2,
        .same_vspace = false,
        .client_prio = 50,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS ReplyWait (High to Low)",
        .caption     = "Call+ReplyWait Different prio test 4",
        .client_fn   = ipc_call_func,
        .server_fn   = ipc_replywait_func,
        .same_vspace = false,
        .client_prio = 50,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS Call (High to Low)",
        .caption     = "Call+ReplyWait Different prio test 3",
        .client_fn   = ipc_call_func2,
        .server_fn   = ipc_replywait_func2,
        .same_vspace = false,
        .client_prio = 100,
        .server_prio = 50,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS ReplyWait (Low to High)",
        .caption     = "Call+ReplyWait Different prio test 2",
        .client_fn   = ipc_call_func,
        .server_fn   = ipc_replywait_func,
        .same_vspace = false,
        .client_prio = 100,
        .server_prio = 50,
        .overhead_id = CALL_REPLY_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS Send",
        .caption     = "Send test",
        .client_fn   = ipc_send_func,
        .server_fn   = ipc_wait_func,
        .same_vspace = FALSE,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = SEND_WAIT_OVERHEAD
    },
    {
        .name        = "Inter-AS Call(10)",
        .caption     = "Call+ReplyWait long message test 1",
        .client_fn   = ipc_call_10_func2,
        .server_fn   = ipc_replywait_10_func2,
        .same_vspace = false,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_10_OVERHEAD
    },
    {
        .name        = "Inter-AS ReplyWait(10)",
        .caption     = "Call+ReplyWait long message test 2",
        .client_fn   = ipc_call_10_func,
        .server_fn   = ipc_replywait_10_func,
        .same_vspace = false,
        .client_prio = 100,
        .server_prio = 100,
        .overhead_id = CALL_REPLY_WAIT_10_OVERHEAD
    }
};

static const struct overhead_benchmark_params overhead_benchmark_params[] = {
    [CALL_OVERHEAD]          = {"call"},
    [REPLY_WAIT_OVERHEAD]    = {"reply wait"},
    [SEND_OVERHEAD]          = {"send"},
    [WAIT_OVERHEAD]          = {"wait"},
    [CALL_10_OVERHEAD]       = {"call"},
    [REPLY_WAIT_10_OVERHEAD] = {"reply wait"},
};

typedef struct bench_result {
    double variance;
    double stddev;
    double stddev_pc;
    double mean;
    ccnt_t min;
    ccnt_t max;
} bench_result_t;


struct bench_results {
    /* Raw results from benchmarking. These get checked for sanity */
    ccnt_t overhead_benchmarks[NOVERHEADBENCHMARKS][RUNS];
    ccnt_t benchmarks[ARRAY_SIZE(benchmark_params)][RUNS];
    /* A worst case overhead */
    ccnt_t overheads[NOVERHEADS];
    /* Calculated results to print out */
    bench_result_t results[ARRAY_SIZE(benchmark_params)];
};

#if defined(CCNT32BIT)
static void
send_result(seL4_CPtr ep, ccnt_t result)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 1);
    seL4_SetMR(0, result);
    seL4_Send(ep, tag);
}
#elif defined(CCNT64BIT)
static void
send_result(seL4_CPtr ep, ccnt_t result)
{
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR(0, (uint32_t)(result >> 32ull));
    seL4_SetMR(1, (uint32_t)(result & 0xFFFFFFFF));
    seL4_Send(ep, tag);
}
#else
#error Unknown ccnt size
#endif

static inline void
dummy_seL4_Send(seL4_CPtr ep, seL4_MessageInfo_t tag)
{
    (void)ep;
    (void)tag;
}

static inline void
dummy_seL4_Call(seL4_CPtr ep, seL4_MessageInfo_t tag)
{
    (void)ep;
    (void)tag;
}

static inline void
dummy_seL4_Wait(seL4_CPtr ep, void *badge)
{
    (void)ep;
    (void)badge;
}

static inline void
dummy_seL4_Reply(seL4_MessageInfo_t tag)
{
    (void)tag;
}

#define IPC_CALL_FUNC(name, bench_func, send_func, call_func, send_start_end, length) \
uint32_t name(int argc, char *argv[]) { \
    uint32_t i; \
    ccnt_t start UNUSED, end UNUSED; \
    seL4_CPtr ep = atoi(argv[0]);\
    seL4_CPtr result_ep = atoi(argv[1]);\
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length); \
    call_func(ep, tag); \
    FENCE(); \
    for (i = 0; i < WARMUPS; i++) { \
        READ_COUNTER_BEFORE(start); \
        bench_func(ep, tag); \
        READ_COUNTER_AFTER(end); \
    } \
    FENCE(); \
    send_result(result_ep, send_start_end); \
    send_func(ep, tag); \
    return 0; \
}

IPC_CALL_FUNC(ipc_call_func, DO_REAL_CALL, seL4_Send, dummy_seL4_Call, end, 0)
IPC_CALL_FUNC(ipc_call_func2, DO_REAL_CALL, dummy_seL4_Send, seL4_Call, start, 0)
IPC_CALL_FUNC(ipc_call_10_func, DO_REAL_CALL_10, seL4_Send, dummy_seL4_Call, end, 10)
IPC_CALL_FUNC(ipc_call_10_func2, DO_REAL_CALL_10, dummy_seL4_Send, seL4_Call, start, 10)

#define IPC_REPLY_WAIT_FUNC(name, bench_func, reply_func, wait_func, send_start_end, length) \
uint32_t name(int argc, char *argv[]) { \
    uint32_t i; \
    ccnt_t start UNUSED, end UNUSED; \
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length); \
    seL4_CPtr ep = atoi(argv[0]);\
    seL4_CPtr result_ep = atoi(argv[1]);\
    wait_func(ep, NULL); \
    FENCE(); \
    for (i = 0; i < WARMUPS; i++) { \
        READ_COUNTER_BEFORE(start); \
        bench_func(ep, tag); \
        READ_COUNTER_AFTER(end); \
    } \
    FENCE(); \
    send_result(result_ep, send_start_end); \
    reply_func(tag); \
    return 0; \
}

IPC_REPLY_WAIT_FUNC(ipc_replywait_func2, DO_REAL_REPLY_WAIT, seL4_Reply, seL4_Wait, end, 0)
IPC_REPLY_WAIT_FUNC(ipc_replywait_func, DO_REAL_REPLY_WAIT, dummy_seL4_Reply, seL4_Wait, start, 0)
IPC_REPLY_WAIT_FUNC(ipc_replywait_10_func2, DO_REAL_REPLY_WAIT_10, seL4_Reply, seL4_Wait, end, 10)
IPC_REPLY_WAIT_FUNC(ipc_replywait_10_func, DO_REAL_REPLY_WAIT_10, dummy_seL4_Reply, seL4_Wait, start, 10)

uint32_t
ipc_wait_func(int argc, char *argv[])
{
    uint32_t i;
    ccnt_t start UNUSED, end UNUSED;
    seL4_CPtr ep = atoi(argv[0]);
    seL4_CPtr result_ep = atoi(argv[1]);
    FENCE();
    for (i = 0; i < WARMUPS; i++) {
        READ_COUNTER_BEFORE(start);
        DO_REAL_WAIT(ep);
        READ_COUNTER_AFTER(end);
    }
    FENCE();
    DO_REAL_WAIT(ep);
    send_result(result_ep, end);
    return 0;
}

uint32_t
ipc_send_func(int argc, char *argv[])
{
    uint32_t i;
    ccnt_t start UNUSED, end UNUSED;
    seL4_CPtr ep = atoi(argv[0]);
    seL4_CPtr result_ep = atoi(argv[1]);
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0);
    FENCE();
    for (i = 0; i < WARMUPS; i++) {
        READ_COUNTER_BEFORE(start);
        DO_REAL_SEND(ep, tag);
        READ_COUNTER_AFTER(end);
    }
    FENCE();
    send_result(result_ep, start);
    DO_REAL_SEND(ep, tag);
    return 0;
}

#define MEASURE_OVERHEAD(op, dest, decls) do { \
    uint32_t i; \
    timing_init(); \
    for (i = 0; i < OVERHEAD_RETRIES; i++) { \
        uint32_t j; \
        for (j = 0; j < RUNS; j++) { \
            uint32_t k; \
            decls; \
            ccnt_t start, end; \
            FENCE(); \
            for (k = 0; k < WARMUPS; k++) { \
                READ_COUNTER_BEFORE(start); \
                op; \
                READ_COUNTER_AFTER(end); \
            } \
            FENCE(); \
            dest[j] = end - start; \
        } \
        if (results_stable(dest)) break; \
    } \
    timing_destroy(); \
} while(0)

static int
results_stable(ccnt_t *array)
{
    uint32_t i;
    for (i = 1; i < RUNS; i++) {
        if (array[i] != array[i - 1]) {
            return 0;
        }
    }
    return 1;
}

static void
measure_overhead(struct bench_results *results)
{
    MEASURE_OVERHEAD(DO_NOP_CALL(0, tag),
                     results->overhead_benchmarks[CALL_OVERHEAD],
                     seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0));
    MEASURE_OVERHEAD(DO_NOP_REPLY_WAIT(0, tag),
                     results->overhead_benchmarks[REPLY_WAIT_OVERHEAD],
                     seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0));
    MEASURE_OVERHEAD(DO_NOP_SEND(0, tag),
                     results->overhead_benchmarks[SEND_OVERHEAD],
                     seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0));
    MEASURE_OVERHEAD(DO_NOP_WAIT(0),
                     results->overhead_benchmarks[WAIT_OVERHEAD],
                     {});
    MEASURE_OVERHEAD(DO_NOP_CALL_10(0, tag10),
                     results->overhead_benchmarks[CALL_10_OVERHEAD],
                     seL4_MessageInfo_t tag10 = seL4_MessageInfo_new(0, 0, 0, 10));
    MEASURE_OVERHEAD(DO_NOP_REPLY_WAIT_10(0, tag10),
                     results->overhead_benchmarks[REPLY_WAIT_10_OVERHEAD],
                     seL4_MessageInfo_t tag10 = seL4_MessageInfo_new(0, 0, 0, 10));
}

#if defined(CCNT32BIT)
static ccnt_t get_result(seL4_CPtr ep)
{
    seL4_Wait(ep, NULL);
    return seL4_GetMR(0);
}
#elif defined(CCNT64BIT)
static ccnt_t get_result(seL4_CPtr ep)
{
    seL4_Wait(ep, NULL);
    return ( ((ccnt_t)seL4_GetMR(0)) << 32ull) | ((ccnt_t)seL4_GetMR(1));
}
#else
#error Unknown ccnt size
#endif

void
init_client_config(env_t env, helper_thread_t *client, helper_func_t client_fn, int prio)
{
    /* set up process a */
    bzero(&client->config, sizeof(client->config));
    client->config.is_elf = false;
    client->config.create_cspace = true;
    client->config.one_level_cspace_size_bits = CONFIG_SEL4UTILS_CSPACE_SIZE_BITS;
    client->config.create_vspace = true;
    client->config.reservations = &env->region;
    client->config.num_reservations = 1;
    client->config.create_fault_endpoint = false;
    client->config.fault_endpoint.cptr = 0; /* benchmark threads do not have fault eps */
    client->config.priority = prio;
    client->config.entry_point = client_fn;
#ifndef CONFIG_KERNEL_STABLE
    client->config.asid_pool = simple_get_init_cap(&env->simple, seL4_CapInitThreadASIDPool);
#endif

}

void
init_server_config(env_t env, helper_thread_t *server, helper_func_t server_fn, int prio,
                   helper_thread_t *client, int same_vspace)
{
    /* set up process b - b's config is nearly the same as a's */
    server->config = client->config;
    server->config.priority = prio;
    server->config.entry_point = server_fn;

    if (same_vspace) {
        /* b shares a's cspace and vspace */
        server->config.create_cspace = false;
        server->config.cnode = client->process.cspace;
        server->config.create_vspace = false;
        server->config.vspace = &client->process.vspace;
    }
}

void
run_bench(env_t env, benchmark_params_t params, ccnt_t *ret1, ccnt_t *ret2)
{
    UNUSED int error;
    helper_thread_t client, server;

    timing_init();

    /* configure processes */
    init_client_config(env, &client, params.client_fn, params.client_prio);

    error = sel4utils_configure_process_custom(&client.process, &env->vka, &env->vspace, client.config);
    assert(error == 0);

    init_server_config(env, &server, params.server_fn, params.server_prio, &client, params.same_vspace);

    error = sel4utils_configure_process_custom(&server.process, &env->vka, &env->vspace, server.config);
    assert(error == 0);

    /* clone the text segment into the vspace - note that as we are only cloning the text
     * segment, you will not be able to use anything that relies on initialisation in benchmark
     * threads - like printf, (but seL4_Debug_PutChar is ok)
     */
    error = sel4utils_bootstrap_clone_into_vspace(&env->vspace, &client.process.vspace, env->region.reservation);
    assert(error == 0);

    if (!params.same_vspace) {
        error = sel4utils_bootstrap_clone_into_vspace(&env->vspace, &server.process.vspace, env->region.reservation);
        assert(error == 0);
    }

    /* copy endpoint cptrs into a and b's respective cspaces*/
    client.ep = sel4utils_copy_cap_to_process(&client.process, env->ep_path);
    client.result_ep = sel4utils_copy_cap_to_process(&client.process, env->result_ep_path);

    if (!params.same_vspace) {
        server.ep = sel4utils_copy_cap_to_process(&server.process, env->ep_path);
        server.result_ep = sel4utils_copy_cap_to_process(&server.process, env->result_ep_path);
    } else {
        server.ep = client.ep;
        server.result_ep = client.result_ep;
    }

    /* set up args */
    sel4utils_create_word_args(client.argv_strings, client.argv, NUM_ARGS, client.ep, client.result_ep);
    sel4utils_create_word_args(server.argv_strings, server.argv, NUM_ARGS, server.ep, server.result_ep);

    /* start processes */
    error = sel4utils_spawn_process(&client.process, &env->vka, &env->vspace, NUM_ARGS, client.argv, 1);
    assert(error == 0);

    error = sel4utils_spawn_process(&server.process, &env->vka, &env->vspace, NUM_ARGS, server.argv, 1);
    assert(error == 0);

    /* wait for results */
    *ret1 = get_result(env->result_ep.cptr);
    *ret2 = get_result(env->result_ep.cptr);

    /* clean up - clean b first in case it is sharing a's cspace and vspace */
    sel4utils_destroy_process(&server.process, &env->vka);
    sel4utils_destroy_process(&client.process, &env->vka);

    timing_destroy();
}

static void
print_all(ccnt_t *array)
{
    uint32_t i;
    for (i = 0; i < RUNS; i++) {
        printf("\t"CCNT_FORMAT"\n", array[i]);
    }
}

static int
check_overhead(struct bench_results *results)
{
    ccnt_t overhead[NOVERHEADBENCHMARKS];
    int i;
    for (i = 0; i < NOVERHEADBENCHMARKS; i++) {
        if (!results_stable(results->overhead_benchmarks[i])) {
            printf("Benchmarking overhead of a %s is not stable! Cannot continue\n", overhead_benchmark_params[i].name);
            print_all(results->overhead_benchmarks[i]);
#ifndef ALLOW_UNSTABLE_OVERHEAD
            return 0;
#endif
        }
        overhead[i] = results_min(results->overhead_benchmarks[i], RUNS);
    }
    /* Take the smallest overhead to be our benchmarking overhead */
    results->overheads[CALL_REPLY_WAIT_OVERHEAD] = MIN(overhead[CALL_OVERHEAD], overhead[REPLY_WAIT_OVERHEAD]);
    results->overheads[SEND_WAIT_OVERHEAD] = MIN(overhead[SEND_OVERHEAD], overhead[WAIT_OVERHEAD]);
    results->overheads[CALL_REPLY_WAIT_10_OVERHEAD] = MIN(overhead[CALL_10_OVERHEAD], overhead[REPLY_WAIT_10_OVERHEAD]);
    return 1;
}

static bench_result_t
process_result(ccnt_t *array, const char *error)
{
    bench_result_t result;

    if (!results_stable(array)) {
        printf("%s cycles are not stable\n", error);
        print_all(array);
    }

    result.min = results_min(array, RUNS);
    result.max = results_max(array, RUNS);
    result.mean = results_mean(array, RUNS);
    result.variance = results_variance(array, result.mean, RUNS);
    result.stddev = results_stddev(array, result.variance, RUNS);
    result.stddev_pc = (double) result.stddev / (double) result.mean * 100;

    return result;
}

static int
process_results(struct bench_results *results)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(results->results); i++) {
        results->results[i] = process_result(results->benchmarks[i], benchmark_params[i].name);
    }
    return 1;
}

static void
print_results(struct bench_results *results)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(results->results); i++) {
        printf("\t<result name = \"%s-min\">"CCNT_FORMAT"</result>\n",
               benchmark_params[i].name, results->results[i].min);

        printf("\t<result name = \"%s-max\">"CCNT_FORMAT"</result>\n",
               benchmark_params[i].name, results->results[i].max);

        printf("\t<result name = \"%s-variance\">%.2lf</result>\n",
               benchmark_params[i].name, results->results[i].variance);

        printf("\t<result name = \"%s-mean\">%.2lf</result>\n",
               benchmark_params[i].name, results->results[i].mean);

        printf("\t<result name = \"%s-stddev\">%.2lf</result>\n",
               benchmark_params[i].name, results->results[i].stddev);

        printf("\t<result name = \"%s-stddev%%\">%.0lf%%</result>\n",
               benchmark_params[i].name, results->results[i].stddev_pc);

    }
}

void
ipc_benchmarks_new(struct env* env)
{
    uint32_t i;
    struct bench_results results;
    ccnt_t start, end;
    measure_overhead(&results);
    if (!check_overhead(&results)) {
        return;
    }

    for (i = 0; i < RUNS; i++) {
        int j;
        printf("\tDoing iteration %d\n", i);
        for (j = 0; j < ARRAY_SIZE(benchmark_params); j++) {
            const struct benchmark_params* params = &benchmark_params[j];
            printf("Running %s\n", params->caption);
            run_bench(env, params, &end, &start);
            if (end > start) {
                results.benchmarks[j][i] = end - start;
            } else {
                results.benchmarks[j][i] = start - end;
            }
            results.benchmarks[j][i] -= results.overheads[params->overhead_id];
        }
    }
    if (!process_results(&results)) {
        return;
    }
    print_results(&results);
}

