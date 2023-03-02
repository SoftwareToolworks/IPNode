/*
 * Copyright (C) 2018 James C. Ahlstrom
 * All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <complex.h>

#define NTAPS 129

struct quisk_cfFilter {        // Structure to hold the static data for FIR filters
    complex float *cpxCoefs;   // complex filter coefficients
    complex float *cSamples;   // storage for old samples
    complex float *ptcSamp;    // next available position in cSamples
    int nBuf;                  // dimension of cBuf
};

void quisk_filt_cfInit(void);
void quisk_filt_destroy(void);
void quisk_ccfFilter(complex float *, complex float *, int);

