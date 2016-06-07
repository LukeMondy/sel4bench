/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */
/* This is very much a work in progress IPC benchmarking set. Goal is
   to eventually use this to replace the rest of the random benchmarking
   happening in this app with just what we need */

#include <autoconf.h>
#include <allocman/vka.h>
#include <allocman/bootstrap.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <sel4/sel4.h>
#include <sel4bench/sel4bench.h>
#include <sel4utils/process.h>
#include <string.h>
#include <utils/util.h>
#include <vka/vka.h>

#include <benchmark.h>
#include <ipc.h>

/* arch/ipc.h requires these defines */
#define __SWINUM(x) ((x) & 0x00ffffff)
#define NOPS ""

#include <arch/ipc.h>

#define NUM_ARGS 2
#define WARMUPS RUNS
#define OVERHEAD_RETRIES 4

typedef struct helper_thread {
    sel4utils_process_t process;
    seL4_CPtr ep;
    seL4_CPtr result_ep;
    char *argv[NUM_ARGS];
    char argv_strings[NUM_ARGS][WORD_STRING_SIZE];
} helper_thread_t;

void
abort(void)
{
    benchmark_finished(EXIT_FAILURE);
}

void
__arch_putchar(int c)
{
    benchmark_putchar(c);
}

static void
timing_init(void)
{
    sel4bench_init();

    assert(sel4bench_get_num_counters() >= 2);
    sel4bench_set_count_event(0, SEL4BENCH_EVENT_CACHE_L1D_MISS);
    sel4bench_set_count_event(1, SEL4BENCH_EVENT_CACHE_L1I_MISS);
    sel4bench_start_counters(0x3);
    sel4bench_reset_counters(0x3);
}

void
timing_destroy(void)
{
    sel4bench_destroy();
}

static void
send_result(seL4_CPtr ep, ccnt_t result)
{
    int length = sizeof(ccnt_t) / sizeof(seL4_Word);
    unsigned int shift = length > 1u ? seL4_WordBits : 0;
    for (int i = length - 1; i >= 0; i--) {
        seL4_SetMR(i, (seL4_Word) result);
        result = result >> shift;
    }

    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length);
    seL4_Send(ep, tag);
}

static ccnt_t
get_result(seL4_CPtr ep)
{
    ccnt_t result = 0;
    seL4_MessageInfo_t tag = seL4_Recv(ep, NULL);
    int length = seL4_MessageInfo_get_length(tag);
    unsigned int shift = length > 1u ? seL4_WordBits : 0;

    for (int i = 0; i < length; i++) {
        result = result << shift;
        result += seL4_GetMR(i);
    }

    return result;
}

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
dummy_seL4_Recv(seL4_CPtr ep, void *badge)
{
    (void)ep;
    (void)badge;
}

static inline void
dummy_seL4_Reply(seL4_MessageInfo_t tag)
{
    (void)tag;
}

uint32_t ipc_call_func(int argc, char *argv[]);
uint32_t ipc_call_func2(int argc, char *argv[]);
uint32_t ipc_call_10_func(int argc, char *argv[]);
uint32_t ipc_call_10_func2(int argc, char *argv[]);
uint32_t ipc_replyrecv_func2(int argc, char *argv[]);
uint32_t ipc_replyrecv_func(int argc, char *argv[]);
uint32_t ipc_replyrecv_10_func2(int argc, char *argv[]);
uint32_t ipc_replyrecv_10_func(int argc, char *argv[]);
uint32_t ipc_send_func(int argc, char *argv[]);
uint32_t ipc_recv_func(int argc, char *argv[]);

static helper_func_t bench_funcs[] = {
    ipc_call_func,
    ipc_call_func2,
    ipc_call_10_func,
    ipc_call_10_func2,
    ipc_replyrecv_func2,
    ipc_replyrecv_func,
    ipc_replyrecv_10_func2,
    ipc_replyrecv_10_func,
    ipc_send_func,
    ipc_recv_func
};

#define IPC_CALL_FUNC(name, bench_func, send_func, call_func, send_start_end, length) \
    uint32_t name(int argc, char *argv[]) { \
    uint32_t i; \
    ccnt_t start UNUSED, end UNUSED; \
    seL4_CPtr ep = atoi(argv[0]);\
    seL4_CPtr result_ep = atoi(argv[1]);\
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length); \
    call_func(ep, tag); \
    SEL4BENCH_FENCE(); \
    for (i = 0; i < WARMUPS; i++) { \
        READ_COUNTER_BEFORE(start); \
        bench_func(ep, tag); \
        READ_COUNTER_AFTER(end); \
    } \
    SEL4BENCH_FENCE(); \
    send_result(result_ep, send_start_end); \
    send_func(ep, tag); \
    seL4_Recv(ep, NULL);/* block so we don't run off the stack */ \
    return 0; \
}

IPC_CALL_FUNC(ipc_call_func, DO_REAL_CALL, seL4_Send, dummy_seL4_Call, end, 0)
IPC_CALL_FUNC(ipc_call_func2, DO_REAL_CALL, dummy_seL4_Send, seL4_Call, start, 0)
IPC_CALL_FUNC(ipc_call_10_func, DO_REAL_CALL_10, seL4_Send, dummy_seL4_Call, end, 10)
IPC_CALL_FUNC(ipc_call_10_func2, DO_REAL_CALL_10, dummy_seL4_Send, seL4_Call, start, 10)

#define IPC_REPLY_RECV_FUNC(name, bench_func, reply_func, recv_func, send_start_end, length) \
uint32_t name(int argc, char *argv[]) { \
    uint32_t i; \
    ccnt_t start UNUSED, end UNUSED; \
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, length); \
    seL4_CPtr ep = atoi(argv[0]);\
    seL4_CPtr result_ep = atoi(argv[1]);\
    seL4_SignalRecv(ep, ep, NULL);\
    SEL4BENCH_FENCE(); \
    for (i = 0; i < WARMUPS; i++) { \
        READ_COUNTER_BEFORE(start); \
        bench_func(ep, tag); \
        READ_COUNTER_AFTER(end); \
    } \
    SEL4BENCH_FENCE(); \
    send_result(result_ep, send_start_end); \
    reply_func(tag); \
    seL4_Recv(ep, NULL); /* block so we don't run off the stack */ \
    return 0; \
}

IPC_REPLY_RECV_FUNC(ipc_replyrecv_func2, DO_REAL_REPLY_RECV, seL4_Reply, seL4_Recv, end, 0)
IPC_REPLY_RECV_FUNC(ipc_replyrecv_func, DO_REAL_REPLY_RECV, dummy_seL4_Reply, seL4_Recv, start, 0)
IPC_REPLY_RECV_FUNC(ipc_replyrecv_10_func2, DO_REAL_REPLY_RECV_10, seL4_Reply, seL4_Recv, end, 10)
IPC_REPLY_RECV_FUNC(ipc_replyrecv_10_func, DO_REAL_REPLY_RECV_10, dummy_seL4_Reply, seL4_Recv, start, 10)

uint32_t
ipc_recv_func(int argc, char *argv[])
{
    uint32_t i;
    ccnt_t start UNUSED, end UNUSED;
    seL4_CPtr ep = atoi(argv[0]);
    seL4_CPtr result_ep = atoi(argv[1]);
    seL4_Send(ep, seL4_MessageInfo_new(0, 0, 0, 0));
    SEL4BENCH_FENCE();
    for (i = 0; i < WARMUPS; i++) {
        READ_COUNTER_BEFORE(start);
        DO_REAL_RECV(ep);
        READ_COUNTER_AFTER(end);
    }
    SEL4BENCH_FENCE();
    DO_REAL_RECV(ep);
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
    SEL4BENCH_FENCE();
    for (i = 0; i < WARMUPS; i++) {
        READ_COUNTER_BEFORE(start);
        DO_REAL_SEND(ep, tag);
        READ_COUNTER_AFTER(end);
    }
    SEL4BENCH_FENCE();
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
            SEL4BENCH_FENCE(); \
            for (k = 0; k < WARMUPS; k++) { \
                READ_COUNTER_BEFORE(start); \
                op; \
                READ_COUNTER_AFTER(end); \
            } \
            SEL4BENCH_FENCE(); \
            dest[j] = end - start; \
        } \
        if (results_stable(dest, RUNS)) break; \
    } \
    timing_destroy(); \
} while(0)

static void
measure_overhead(ipc_results_t *results)
{
    MEASURE_OVERHEAD(DO_NOP_CALL(0, tag),
                     results->overhead_benchmarks[CALL_OVERHEAD],
                     seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0));
    MEASURE_OVERHEAD(DO_NOP_REPLY_RECV(0, tag),
                     results->overhead_benchmarks[REPLY_RECV_OVERHEAD],
                     seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0));
    MEASURE_OVERHEAD(DO_NOP_SEND(0, tag),
                     results->overhead_benchmarks[SEND_OVERHEAD],
                     seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 0));
    MEASURE_OVERHEAD(DO_NOP_RECV(0),
                     results->overhead_benchmarks[RECV_OVERHEAD],
                     {});
    MEASURE_OVERHEAD(DO_NOP_CALL_10(0, tag10),
                     results->overhead_benchmarks[CALL_10_OVERHEAD],
                     seL4_MessageInfo_t tag10 = seL4_MessageInfo_new(0, 0, 0, 10));
    MEASURE_OVERHEAD(DO_NOP_REPLY_RECV_10(0, tag10),
                     results->overhead_benchmarks[REPLY_RECV_10_OVERHEAD],
                     seL4_MessageInfo_t tag10 = seL4_MessageInfo_new(0, 0, 0, 10));
}

/* this function is never exeucted, it just lives in the scheduler queue */
static NORETURN seL4_Word
dummy_fn(int argc, char *argv[])
{
    while (1);
}

void
run_bench(env_t *env, cspacepath_t ep_path, cspacepath_t result_ep_path,
          const benchmark_params_t *params,
          ccnt_t *ret1, ccnt_t *ret2)
{
    helper_thread_t client, server, dummy;

    timing_init();

    benchmark_shallow_clone_process(env, &client.process, params->client_prio,
                                            bench_funcs[params->client_fn], "client");

    if (params->same_vspace) {
        benchmark_configure_thread_in_process(env, &client.process, &server.process, params->server_prio,
                                              bench_funcs[params->server_fn], "server");
    } else {
        benchmark_shallow_clone_process(env, &server.process, params->server_prio,
                                        bench_funcs[params->server_fn], "server");
    }

    if (params->dummy_thread) {
        benchmark_shallow_clone_process(env, &dummy.process, params->dummy_prio, dummy_fn, "dummy");
        if (sel4utils_spawn_process(&dummy.process, &env->vka, &env->vspace, 0, NULL, 1)) {
            ZF_LOGF("Failed to spawn dummy process\n");
        }
    }

    /* copy endpoint cptrs into a and b's respective cspaces*/
    client.ep = sel4utils_copy_cap_to_process(&client.process, ep_path);
    client.result_ep = sel4utils_copy_cap_to_process(&client.process, result_ep_path);

    if (!params->same_vspace) {
        server.ep = sel4utils_copy_cap_to_process(&server.process, ep_path);
        server.result_ep = sel4utils_copy_cap_to_process(&server.process, result_ep_path);
    } else {
        server.ep = client.ep;
        server.result_ep = client.result_ep;
    }

    /* set up args */
    sel4utils_create_word_args(client.argv_strings, client.argv, NUM_ARGS, client.ep, client.result_ep);
    sel4utils_create_word_args(server.argv_strings, server.argv, NUM_ARGS, server.ep, server.result_ep);

    /* spawn server */
    if (sel4utils_spawn_process(&server.process, &env->vka, &env->vspace, NUM_ARGS, server.argv, 1)) {
        ZF_LOGF("Failed to spawn server\n");
    }

    /* wait for server to tell us it's been initialised */
    seL4_Recv(ep_path.capPtr, NULL);

    if (params->same_sc) {
        /* now remove server's scheduling context */
        if (seL4_SchedContext_Unbind(server.process.thread.sched_context.cptr)) {
            ZF_LOGF("Failed to clear server sched context\n");
        }
    }

    /* start the client */
    if (sel4utils_spawn_process(&client.process, &env->vka, &env->vspace, NUM_ARGS, client.argv, 1)) {
        ZF_LOGF("Failed to spawn client\n");
    }

    /* make sure client runs first */
    seL4_SchedContext_YieldTo_t r = seL4_SchedContext_YieldTo(client.process.thread.sched_context.cptr);
    if (r.error != seL4_NoError) {
          ZF_LOGF("Failed to yieldTo client\n");
    }

    /* recv client result */
    *ret1 = get_result(result_ep_path.capPtr);

    if (params->same_sc) {
        /* give the server back its scheduling context to it can reply to us */
        if (seL4_SchedContext_Bind(server.process.thread.sched_context.cptr,
                                   server.process.thread.tcb.cptr)) {
            ZF_LOGF("Failed to set sched context for server\n");
        }
    }

    /* recv server result */
    *ret2 = get_result(result_ep_path.capPtr);

    /* clean up - clean server first in case it is sharing the client's cspace and vspace */
    sel4utils_destroy_process(&server.process, &env->vka);
    sel4utils_destroy_process(&client.process, &env->vka);
    if (params->dummy_thread) {
        sel4utils_destroy_process(&dummy.process, &env->vka);
    }

    timing_destroy();
}

int
main(int argc, char **argv)
{
    env_t *env;
    vka_object_t ep, result_ep;
    cspacepath_t ep_path, result_ep_path;

    env = benchmark_get_env(argc, argv, sizeof(ipc_results_t));
    ipc_results_t *results = (ipc_results_t *) env->results;

    /* allocate benchmark endpoint - the IPC's that we benchmark
       will be sent over this ep */
    if (vka_alloc_endpoint(&env->vka, &ep) != 0) {
        ZF_LOGF("Failed to allocate endpoint");
    }
    vka_cspace_make_path(&env->vka, ep.cptr, &ep_path);

    /* allocate result ep - the IPC threads will send their timestamps
       to this ep */
    if (vka_alloc_endpoint(&env->vka, &result_ep) != 0) {
        ZF_LOGF("Failed to allocate endpoint");
    }
    vka_cspace_make_path(&env->vka, result_ep.cptr, &result_ep_path);

    /* measure benchmarking overhead */
    measure_overhead(results);

    /* run the benchmark */
    ccnt_t start, end;
    for (int i = 0; i < RUNS; i++) {
        int j;
        ZF_LOGI("--------------------------------------------------\n");
        ZF_LOGI("Doing iteration %d\n", i);
        ZF_LOGI("--------------------------------------------------\n");
        for (j = 0; j < ARRAY_SIZE(benchmark_params); j++) {
            const struct benchmark_params* params = &benchmark_params[j];
            ZF_LOGI("%s\t: IPC duration (%s), client prio: %3d server prio %3d, %s vspace, %s sched context, length %2d\n",
                    params->name,
                    params->direction == DIR_TO ? "client --> server" : "server --> client",
                    params->client_prio, params->server_prio,
                    params->same_vspace ? "same" : "diff",
                    params->same_sc ? "same" : "diff", params->length);

            run_bench(env, ep_path, result_ep_path, params, &end, &start);
            if (end > start) {
                results->benchmarks[j][i] = end - start;
            } else {
                results->benchmarks[j][i] = start - end;
            }
            results->benchmarks[j][i] -= results->overheads[params->overhead_id];
        }
    }

    /* done -> results are stored in shared memory so we can now return */
    benchmark_finished(EXIT_SUCCESS);
    return 0;
}

