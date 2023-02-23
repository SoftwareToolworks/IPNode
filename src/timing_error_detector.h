/*
 * Copyright (C) 2017 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <complex.h>
#include <stdbool.h>

void revert_input_clock(void);
void sync_reset_input_clock(void);
void sync_reset(void);
void create_timing_error_detector(void);
void destroy_timing_error_detector(void);
void ted_input(complex float *);
void revert(bool);
complex float getMiddleSample(void);
float get_error(void);
int get_inputs_per_symbol(void);

#ifdef __cplusplus
}
#endif

