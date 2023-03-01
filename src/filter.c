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

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: quisk_filt_cfInit
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 27 August 2015
  MODIFIED: 4 June 2018

  Initialize a FIR filter that has complex samples, and either real or complex coefficients.

\*---------------------------------------------------------------------------*/

void quisk_filt_cfInit(struct quisk_cfFilter * filter, float * coefs, int taps) {
    // Prepare a new filter using coefs and taps.  Samples are complex. Coefficients can
    // be real or complex.
    filter->dCoefs = coefs;
    filter->cpxCoefs = NULL;
    filter->cSamples = (complex float *)MALLOC(taps * sizeof(complex float));
    memset(filter->cSamples, 0, taps * sizeof(complex float));
    filter->ptcSamp = filter->cSamples;
    filter->nTaps = taps;
    filter->cBuf = NULL;
    filter->nBuf = 0;
}

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: quisk_filt_destroy
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 27 August 2015
  MODIFIED: 4 June 2018

  Destroy the FIR filter and free all resources.

\*---------------------------------------------------------------------------*/

void quisk_filt_destroy(struct quisk_cfFilter * filter) {
    if (filter->cSamples) {
        FREE(filter->cSamples);
        filter->cSamples = NULL;
    }

    if (filter->cBuf) {
        FREE(filter->cBuf);
        filter->cBuf = NULL;
    }

    if (filter->cpxCoefs) {
        FREE(filter->cpxCoefs);
        filter->cpxCoefs = NULL;
    }
}

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: quisk_cfTune
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 4 June 2018

  Tune a low pass filter with float coefficients into an analytic I/Q bandpass filter
  with complex coefficients.  The "freq" is the center frequency / sample rate.
  If the float coefs represent a low pass filter with bandwidth 1 kHz, the new bandpass
  filter has width 2 kHz. The filter can be re-tuned repeatedly.

\*---------------------------------------------------------------------------*/

void quisk_cfTune(struct quisk_cfFilter * filter, float freq) {
    float D, tune;
    int i;

    if ( ! filter->cpxCoefs)
        filter->cpxCoefs = (complex float *)MALLOC(filter->nTaps * sizeof(complex float));

    tune = 2.0 * M_PI * freq;
    D = (filter->nTaps - 1.0) / 2.0;

    for (i = 0; i < filter->nTaps; i++) {
        float tval = tune * (i - D);
        filter->cpxCoefs[i] = cmplx(tval) * filter->dCoefs[i];
    }
}

/*---------------------------------------------------------------------------*\

  FUNCTIONS...: quisk_ccfFilter
  AUTHOR......: Jim Ahlstrom
  DATE CREATED: 4 June 2018

  Filter complex samples using complex coefficients. The inSamples and outSamples may be
  the same array. The loop runs forward over coefficients but backwards over samples.
  Therefore, the coefficients must be reversed unless they are created by quisk_cfTune.
  Low pass filter coefficients are symmetrical, so this does not usually matter.

\*---------------------------------------------------------------------------*/

void quisk_ccfFilter(complex float * inSamples, complex float * outSamples, int count, struct quisk_cfFilter * filter) {
    int i, k;
    complex float * ptSample;
    complex float * ptCoef;
    complex float accum;

    for (i = 0; i < count; i++) {
        *filter->ptcSamp = inSamples[i];
        accum = 0;
        ptSample = filter->ptcSamp;
        ptCoef = filter->cpxCoefs;

        for (k = 0; k < filter->nTaps; k++, ptCoef++) {
            accum += *ptSample  *  *ptCoef;

            if (--ptSample < filter->cSamples)
                ptSample = filter->cSamples + filter->nTaps - 1;
        }

        outSamples[i] = accum;

        if (++filter->ptcSamp >= filter->cSamples + filter->nTaps)
            filter->ptcSamp = filter->cSamples;
    }
}

