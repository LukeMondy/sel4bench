/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(NICTA_GPL)
 */
#include "benchmark.h"
#include "processing.h"
#include "json.h"

#include <signal.h>
#include <stdio.h>

static json_t *
signal_process(void *results) {
    signal_results_t *raw_results = results;

    result_desc_t desc = {
        .stable = true,
        .name = "signal overhead",
        .ignored = N_IGNORED
    };

    result_t result = process_result(N_RUNS, raw_results->overhead, desc);

    result_set_t set = {
        .name = "Signal overhead",
        .n_results = 1,
        .n_extra_cols = 0,
        .results = &result
    };

    json_t *array = json_array();
    json_array_append_new(array, result_set_to_json(set));

    desc.stable = false;
    desc.overhead = result.min;

    result = process_result(N_RUNS, raw_results->lo_prio_results, desc);
    set.name = "Signal to high prio thread";
    json_array_append_new(array, result_set_to_json(set));

    result = process_result(N_RUNS, raw_results->hi_prio_results, desc);
    set.name = "Signal to low prio thread";
    json_array_append_new(array, result_set_to_json(set));

    return array;
}

static benchmark_t signal_benchmark = {
    .name = "signal",
    .enabled = config_set(CONFIG_APP_SIGNALBENCH),
    .results_pages = BYTES_TO_SIZE_BITS_PAGES(sizeof(signal_results_t), seL4_PageBits),
    .process = signal_process,
    .init = blank_init
};

benchmark_t *
signal_benchmark_new(void) 
{
    return &signal_benchmark;
}

