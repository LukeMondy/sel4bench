/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(NICTA_GPL)
 */
#ifndef _SEL4BENCH_TIMING_H_
#define _SEL4BENCH_TIMING_H_

#ifndef TIMING_MULTIPLE_PMCS
#define TIMING_MULTIPLE_PMCS true
#endif /* TIMING_MULTIPLE_PMCS */

/* Initalises timing system. Automatically called by first list creation if
 * uninitialised. */
void
timing_init(void);

/* Destroy timing system. Safe to print after destroyed. */
void
timing_destroy(void);

#endif /* _SEL4TEST_TIMING_H_ */
