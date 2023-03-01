/*
 * Copyright (C) 2018 James C. Ahlstrom
 * All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#include "filter.h"
#include "filter_coef.h"
#include "ipnode.h"

extern const float filtP700S900[];

static struct quisk_cfFilter filter;

void quisk_filt_cfInit() {
    filter.dCoefs = filtP700S900;
    filter.cSamples = (complex float *)calloc(NTAPS, sizeof(complex float));
    filter.ptcSamp = filter.cSamples;
    filter.cBuf = NULL;
    filter.nBuf = 0;

    filter.cpxCoefs = (complex float *)calloc(NTAPS, sizeof(complex float));

    float tune = TAU * (CENTER / FS);
    float D = (NTAPS - 1.0f) / 2.0f;

    for (int i = 0; i < NTAPS; i++) {
        float tval = tune * (i - D);
        filter.cpxCoefs[i] = cmplx(tval) * filter.dCoefs[i];
    }
}

void quisk_filt_destroy() {
    if (filter.cSamples) {
        free(filter.cSamples);
        filter.cSamples = NULL;
    }

    if (filter.cBuf) {
        free(filter.cBuf);
        filter.cBuf = NULL;
    }

    if (filter.cpxCoefs) {
        free(filter.cpxCoefs);
        filter.cpxCoefs = NULL;
    }
}

void quisk_ccfFilter(complex float *inSamples, complex float *outSamples, int count) {
    complex float * ptSample;
    complex float * ptCoef;
    complex float accum;

    for (int i = 0; i < count; i++) {
        *filter.ptcSamp = inSamples[i];
        accum = 0.0f;
        ptSample = filter.ptcSamp;
        ptCoef = filter.cpxCoefs;

        for (int k = 0; k < NTAPS; k++, ptCoef++) {
            accum += *ptSample  *  *ptCoef;

            if (--ptSample < filter.cSamples)
                ptSample = filter.cSamples + NTAPS - 1;
        }

        outSamples[i] = accum;

        if (++filter.ptcSamp >= filter.cSamples + NTAPS)
            filter.ptcSamp = filter.cSamples;
    }
}
