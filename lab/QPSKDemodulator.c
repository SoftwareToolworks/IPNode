/*
 * Copyright (C) 2015 Dennis Sheirer
 * Copyright (C) 2002,2012 Free Software Foundation, Inc.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Translated from Java by Software Toolworks
 */

#include <stdlib.h>
#include <complex.h>
#include <math.h>

#include "qpsk-demo.h"
#include "interp.h"
#include "costas_loop.h"

static complex float mPreviousCurrentSample;
static complex float mPreviousMiddleSample;
static complex float mMiddleSymbol;
static complex float mCurrentSymbol;
static complex float mReceivedSample;

/*
 * Implements a Differential QPSK demodulator
 * Uses Costas Loop and Gardner timing error detector
 */
void create_QPSKDemodulator(float sampleCounterGain)
{
    create_symbolEvaluator();
    create_interpolatingSampleBuffer(sampleCounterGain);

    mPreviousCurrentSample = 0.0;
    mPreviousMiddleSample = 0.0;
    mMiddleSymbol = 0.0;
    mCurrentSymbol = 0.0;
    mReceivedSample = 0.0;
}

static complex float cnormalize(complex float a)
{
    float mag = cabs(a);

    if (mag != 0.0)
    {
        return a / mag;
    }
    else
    {
        return a;
    }
}

/*
 * Note: the interpolating sample buffer holds 2 symbols worth of samples
 * and the current sample method points to the sample at the mid-point
 * between those 2 symbol periods and the middle sample method points to the
 * sample that is half a symbol period after the current sample.
 */
static Dibit calculateSymbol()
{
    /*
     * Since we need a middle sample and a current symbol sample for the gardner
     * calculation, we'll treat the interpolating buffer's current sample as the
     * gardner mid-point and we'll treat the interpolating buffer's mid-point
     * sample as the current symbol sample (ie flip-flopped)
     */
    complex float middleSample = getCurrentSample();
    complex float currentSample = getMiddleSample();

    if ((middleSample == -10000.0) || (currentSample == -10000.0))
    {
        return D99;
    }

    // Differential decode middle and current symbols by calculating the angular rotation between the previous and
    // current samples (current sample x complex conjugate of previous sample).
    mMiddleSymbol = middleSample * conj(mPreviousMiddleSample);
    mCurrentSymbol = middleSample * conj(mPreviousCurrentSample);

    // Set gain to unity before we calculate the error value
    mMiddleSymbol = cnormalize(mMiddleSymbol);
    mCurrentSymbol = cnormalize(mCurrentSymbol);

    // Pass symbols to evaluator to determine timing and phase error and make symbol decision
    setSymbols(mMiddleSymbol, mCurrentSymbol);

    // Update symbol timing error in Interpolator
    resetAndAdjust(getTimingError());

    // Update Costas phase error from symbolEvaluator
    float d_error = phase_detector(mReceivedSample);

    advance_loop(d_error);
    phase_wrap();
    frequency_limit();

    // Store current samples/symbols for next symbol calculation
    mPreviousMiddleSample = middleSample;
    mPreviousCurrentSample = currentSample;

    return getSymbolDecision();
}

/*
 * Processes a complex sample for decoding. Once sufficient samples are
 * buffered, a symbol decision is made.
 */
Dibit demod_receive(complex float sample)
{
    // Update current sample and Mix with costas loop
    // to remove any rotation from frequency error
    mReceivedSample = sample * cmplxconj(get_phase());

    // Store the sample in the interpolating buffer
    interp_receive(mReceivedSample);

    // Calculate the symbol once we've stored enough samples
    if (hasSymbol())
    {
        return calculateSymbol();
    }

    return D99;
}

complex float getReceivedSample()
{
    return mReceivedSample;
}
