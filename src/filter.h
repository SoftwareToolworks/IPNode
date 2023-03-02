/*
 * Copyright (C) 2018 James C. Ahlstrom
 * All rights reserved.
 *
 * Modified by Software Toolworks
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <complex.h>

// NTAPS must be Odd
#define NTAPS 129

struct firFilter {        // Structure to hold the static data for FIR filters
    complex float *cpxCoefs;   // complex filter coefficients
    complex float *cSamples;   // storage for old samples
    complex float *ptcSamp;    // next available position in cSamples
    int nBuf;                  // dimension of cBuf
};

void filterCreate(void);
void filterDestroy(void);
void rxFilter(complex float *, complex float *, int);
void txFilter(complex float *, complex float *, int);
