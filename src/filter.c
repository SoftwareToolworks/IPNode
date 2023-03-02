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

static struct quisk_cfFilter rx_filter;
static struct quisk_cfFilter tx_filter;

void quisk_filt_cfInit() {
    tx_filter.cSamples = (complex float *)calloc(NTAPS, sizeof(complex float));
    rx_filter.cSamples = (complex float *)calloc(NTAPS, sizeof(complex float));

    tx_filter.ptcSamp = tx_filter.cSamples;
    rx_filter.ptcSamp = rx_filter.cSamples;

    tx_filter.nBuf = 0;
    rx_filter.nBuf = 0;

    tx_filter.cpxCoefs = (complex float *)calloc(NTAPS, sizeof(complex float));
    rx_filter.cpxCoefs = (complex float *)calloc(NTAPS, sizeof(complex float));

    float tune = TAU * (CENTER / FS);
    float D = (NTAPS - 1.0f) / 2.0f;

    for (int i = 0; i < NTAPS; i++) {
        float tval1 = tune * (i - D);
        complex float tval2 = cmplx(tval1) * filtP700S900[i];
        tx_filter.cpxCoefs[i] = tval2; 
        rx_filter.cpxCoefs[i] = tval2;
    }
}

void quisk_filt_destroy() {
    if (rx_filter.cSamples) {
        free(rx_filter.cSamples);
        rx_filter.cSamples = NULL;
    }

    if (rx_filter.cpxCoefs) {
        free(rx_filter.cpxCoefs);
        rx_filter.cpxCoefs = NULL;
    }

    if (tx_filter.cSamples) {
        free(tx_filter.cSamples);
        tx_filter.cSamples = NULL;
    }

    if (tx_filter.cpxCoefs) {
        free(tx_filter.cpxCoefs);
        tx_filter.cpxCoefs = NULL;
    }
}

void quisk_ccfRXFilter(complex float *inSamples, complex float *outSamples, int count) {
    complex float *ptSample;
    complex float *ptCoef;
    complex float accum;

    for (int i = 0; i < count; i++) {
        *rx_filter.ptcSamp = inSamples[i];
        accum = 0.0f;
        ptSample = rx_filter.ptcSamp;
        ptCoef = rx_filter.cpxCoefs;

        for (int k = 0; k < NTAPS; k++, ptCoef++) {
            accum += *ptSample  *  *ptCoef;

            if (--ptSample < rx_filter.cSamples)
                ptSample = rx_filter.cSamples + NTAPS - 1;
        }

        outSamples[i] = accum;

        if (++rx_filter.ptcSamp >= rx_filter.cSamples + NTAPS)
            rx_filter.ptcSamp = rx_filter.cSamples;
    }
}

void quisk_ccfTXFilter(complex float *inSamples, complex float *outSamples, int count) {
    complex float *ptSample;
    complex float *ptCoef;
    complex float accum;

    for (int i = 0; i < count; i++) {
        *tx_filter.ptcSamp = inSamples[i];
        accum = 0.0f;
        ptSample = tx_filter.ptcSamp;
        ptCoef = tx_filter.cpxCoefs;

        for (int k = 0; k < NTAPS; k++, ptCoef++) {
            accum += *ptSample  *  *ptCoef;

            if (--ptSample < tx_filter.cSamples)
                ptSample = tx_filter.cSamples + NTAPS - 1;
        }

        outSamples[i] = accum;

        if (++tx_filter.ptcSamp >= tx_filter.cSamples + NTAPS)
            tx_filter.ptcSamp = tx_filter.cSamples;
    }
}

