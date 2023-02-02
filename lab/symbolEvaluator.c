/*
 * Copyright (C) 2015 Dennis Sheirer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Translated from Java by Software Toolworks
 */

#include <math.h>
#include <complex.h>

#include "qpsk-demo.h"
#include "interp.h"

static float mPhaseError;
static float mTimingError;

static Dibit mSymbolDecision;

static complex float mPreviousSymbol;
static complex float mEvaluationSymbol;

/*
 * Differential QPSK Decision-directed symbol phase and timing error
 * detector and symbol decision slicer.
 *
 * Symbol decision is based on the closest reference quadrant for the
 * sampled symbol.
 *
 * Phase error is calculated as the angular distance of the sampled symbol
 * from the reference symbol.
 *
 * Timing error is calculated using the Gardner method by comparing the
 * previous symbol to the current symbol and amplifying the delta between
 * the two using the intra-symbol sample to form the timing error.
 */
void create_symbolEvaluator()
{
    mPhaseError = 0.0f;
    mTimingError = 0.0f;

    mSymbolDecision = D00;

    mPreviousSymbol = 0.0f;
    mEvaluationSymbol = 0.0f;
}

/*
 * Constrains timing error to +/- the maximum value and corrects any
 * floating point invalid numbers
 */
static float enormalize(float error, float maximum)
{
    if (isnan(error))
    {
        return 0.0f;
    }

    // clip - Constrains value to the range of ( -maximum <> maximum )

    if (error > maximum)
    {
        return maximum;
    }
    else if (error < -maximum)
    {
        return -maximum;
    }

    return error;
}

/*
 * Sets the middle and current symbols to be evaluated for phase and timing
 * errors and to determine the transmitted symbol relative to the closest
 * reference symbol.
 * 
 * After invoking this function, you can access the phase
 * and timing errors and the symbol decision.
 *
 * Phase and timing error values are calculated by first determining the
 * symbol and then calculating the phase and timing errors relative to the
 * reference symbol.
 * 
 * The timing error is corrected with the appropriate sign
 * relative to the angular vector rotation so that the error value indicates
 * the correct error direction.
 *
 * @param middle interpolated differentially-decoded sample that falls
 *        midway between previous/current symbols
 * @param current interpolated differentially-decoded symbol
 */
void setSymbols(complex float middle, complex float current)
{
    // Gardner timing error calculation
    float errorInphase = (creal(mPreviousSymbol) - creal(current)) * creal(middle);
    float errorQuadrature = (cimag(mPreviousSymbol) - cimag(current)) * cimag(middle);

    mTimingError = enormalize(errorInphase + errorQuadrature, 0.3f);

    // Store the current symbol to use in the next symbol calculation
    mPreviousSymbol = current;

    // Phase error and symbol decision calculations ...
    mEvaluationSymbol = current;

    if (cimag(mEvaluationSymbol) > 0.0f)
    {
        if (creal(mEvaluationSymbol) > 0.0f)
        {
            mSymbolDecision = D00;
            mEvaluationSymbol *= cmplx(ROTATE_FROM_PLUS_45);
        }
        else
        {
            mSymbolDecision = D01;
            mEvaluationSymbol *= cmplx(ROTATE_FROM_PLUS_135);
        }
    }
    else
    {
        if (creal(mEvaluationSymbol) > 0.0f)
        {
            mSymbolDecision = D10;
            mEvaluationSymbol *= cmplx(ROTATE_FROM_MINUS_45);
        }
        else
        {
            mSymbolDecision = D11;
            mEvaluationSymbol *= cmplx(ROTATE_FROM_MINUS_135);
        }
    }

    /*
     * Since we've rotated the error symbol back to 0 radians,
     * the Imaginary value closely approximates the arctan of
     * the error angle, relative to 0 radians, and this
     * provides our error value
     */
    mPhaseError = enormalize(cimag(conj(mEvaluationSymbol)), 0.3f);
}

/*
 * Phase error of the symbol relative to the nearest reference symbol.
 *
 * @return phase error in radians of distance from the reference symbol.
 */
float getPhaseError()
{
    return mPhaseError;
}

/*
 * Timing error of the symbol relative to the nearest reference symbol.
 *
 * @return timing error in radians of angular distance from the reference
 * symbol recognizing that the symbol originates at zero radians and rotates
 * toward the intended reference symbol, therefore the error value indicates
 * if the symbol was sampled early (-) or late (+) relative to the reference
 * symbol.
 */
float getTimingError()
{
    return mTimingError;
}

/*
 * Reference symbol that is closest to the transmitted/sampled symbol.
 */
Dibit getSymbolDecision()
{
    return mSymbolDecision;
}
