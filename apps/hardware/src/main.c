/*
 * Copyright 2016, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(D61_BSD)
 */

#include <autoconf.h>
#include <stdio.h>

#include <sel4/sel4.h>
#include <sel4bench/arch/sel4bench.h>

#include <benchmark.h>
#include <hardware.h>


#define NOPS ""

#include <arch/hardware.h>

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

void
measure_nullsyscall_overhead(ccnt_t *results)
{
    ccnt_t start, end;
    for (int i = 0; i < N_RUNS; i++) {
        SEL4BENCH_READ_CCNT(start);
        DO_NOP_NULLSYSCALL();
        SEL4BENCH_READ_CCNT(end);
        results[i] = (end - start);
    }
}

void measure_nullsyscall(ccnt_t *results)
{
    ccnt_t start, end;
    for (int i = 0; i < N_RUNS; i++) {
        SEL4BENCH_READ_CCNT(start);
        DO_REAL_NULLSYSCALL();
        SEL4BENCH_READ_CCNT(end);
        results[i] = (end - start);
    }

}

int
main(int argc, char **argv)
{
    env_t *env;
    UNUSED int error;
    hardware_results_t *results;

    env = benchmark_get_env(argc, argv, sizeof(hardware_results_t));
    results = (hardware_results_t *) env->results;

    sel4bench_init();

    /* measure overhead */
    measure_nullsyscall_overhead(results->nullSyscall_overhead);
    measure_nullsyscall(results->nullSyscall_results);

    /* done -> results are stored in shared memory so we can now return */
    benchmark_finished(EXIT_SUCCESS);
    return 0;
}
