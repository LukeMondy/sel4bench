/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(NICTA_GPL)
 */
#include <autoconf.h>
#include <ipc.h>
#include <jansson.h>
#include <sel4bench/sel4bench.h>

#include <utils/util.h>

#include "benchmark.h"
#include "json.h"
#include "math.h"
#include "printing.h"
#include "processing.h"

static json_t *
process_ipc_results(void *r)
{
    ipc_results_t *raw_results = r;

    /* check overheads */
    for (int i = 0; i < NUM_OVERHEAD_BENCHMARKS; i++) {
        if (!results_stable(&raw_results->overhead_benchmarks[i][1] , RUNS - 1)) {
            if (!config_set(CONFIG_ALLOW_UNSTABLE_OVERHEAD)) {
                printf("Benchmarking overhead of a %s is not stable! Cannot continue\n",
                        overhead_benchmark_params[i].name);
                print_all(RUNS, raw_results->overhead_benchmarks[i]);
                return NULL;
            }
        }
    }

    int n = ARRAY_SIZE(benchmark_params);
    char *functions[n];
    char *directions[n];
    json_int_t client_prios[n];
    json_int_t server_prios[n];
    bool same_vspace[n];
    bool dummy_thread[n];
    bool same_sc[n];
    json_int_t dummy_thread_prio[n];
    json_int_t length[n];

    column_t extra_cols[] = {
        {
            .header = "Function",
            .type = JSON_STRING,
            .string_array = &functions[0]
        },
        {
            .header = "Direction",
            .type = JSON_STRING,
            .string_array = &directions[0],
        },
        {
            .header = "Client Prio",
            .type = JSON_INTEGER,
            .integer_array = &client_prios[0]
        },
        {
            .header = "Server Prio",
            .type = JSON_INTEGER,
            .integer_array = &server_prios[0]
        },
        {
            .header = "Same vspace?",
            .type = JSON_TRUE,
            .bool_array = &same_vspace[0]
        },
        {
            .header = "Same sched context?",
            .type = JSON_TRUE,
            .bool_array = &same_sc[0]
        },
        {
            .header = "Dummy thread?",
            .type = JSON_TRUE,
            .bool_array = &dummy_thread[0]
        },
        {
            .header = "Dummy prio",
            .type = JSON_INTEGER,
            .integer_array = &dummy_thread_prio[0]
        },
        {
            .header = "IPC length",
            .type = JSON_INTEGER,
            .integer_array = &length[0]
        }
    };

    result_t results[n];

    result_set_t result_set = {
        .name = "One way IPC microbenchmarks",
        .extra_cols = extra_cols,
        .n_extra_cols = ARRAY_SIZE(extra_cols),
        .results = results,
        .n_results = n,
    };

    /* now calculate the results (overheads already taken into account) */
    for (int i = 0; i < n; i++) {
        result_desc_t desc = {
            .name = benchmark_params[i].name
        };

        functions[i] = (char *) benchmark_params[i].name,
        directions[i] = benchmark_params[i].direction == DIR_TO ? "client->server" :
                                                                  "server->client";
        client_prios[i] = benchmark_params[i].client_prio;
        server_prios[i] = benchmark_params[i].server_prio;
        same_vspace[i] = benchmark_params[i].same_vspace;
        same_sc[i] = benchmark_params[i].same_sc;
        dummy_thread[i] = benchmark_params[i].dummy_thread;
        dummy_thread_prio[i] = benchmark_params[i].dummy_prio;
        length[i] = benchmark_params[i].length;

        results[i] = process_result(RUNS, raw_results->benchmarks[i], desc);
    }

    json_t *array = json_array();
    json_array_append_new(array, result_set_to_json(result_set));
    return array;
}

static benchmark_t ipc_benchmark = {
    .name = "ipc",
    .enabled = config_set(CONFIG_APP_IPCBENCH),
    .results_pages = BYTES_TO_SIZE_BITS_PAGES(sizeof(ipc_results_t), seL4_PageBits),
    .process = process_ipc_results,
    .init = blank_init
};

benchmark_t *
ipc_benchmark_new(void)
{
   return &ipc_benchmark;
}

