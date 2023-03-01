/*
 * Copyright (C) 2018 James C. Ahlstrom
 * All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <complex.h>

struct quisk_cfFilter {        // Structure to hold the static data for FIR filters
    float * dCoefs;            // real filter coefficients
    complex float * cpxCoefs;  // complex filter coefficients
    int nBuf;                  // dimension of cBuf
    int nTaps;                 // dimension of dSamples, cSamples, dCoefs
    complex float * cSamples;  // storage for old samples
    complex float * ptcSamp;   // next available position in cSamples
    complex float * cBuf;      // auxiliary buffer for interpolation
} ;

void quisk_filt_cfInit(struct quisk_cfFilter *, float *, int);
void quisk_filt_destroy(struct quisk_cfFilter *);
void quisk_cfTune(struct quisk_cfFilter *, float);
void quisk_ccfFilter(complex float *, complex float *, int, struct quisk_cfFilter *);

extern const float filtP700S900[];

