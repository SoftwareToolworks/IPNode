/*
 * Copyright (C) 2017 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <complex.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <float.h>

#include "deque.h"
#include "constellation.h"
#include "timing_error_detector.h"

// BSS Storage

static float d_error;
static float d_prev_error;

static int d_inputs_per_symbol;
static int d_input_clock;
static int d_error_depth;

deque *d_input;
deque *d_decision;

// Prototypes

static void slice(complex float *, complex float *);
static float compute_error(void);
static void advance_input_clock(void);

// Functions

/*
 * Revert the TED input clock one step
 */
void revert_input_clock()
{
    if (d_input_clock == 0)
        d_input_clock = d_inputs_per_symbol - 1;
    else
        d_input_clock--;
}

/*
 * Reset the TED input clock, so the next input clock advance
 * corresponds to a symbol sampling instant.
 */
void sync_reset_input_clock()
{
    d_input_clock = d_inputs_per_symbol - 1;
}

/*
 * Advance the TED input clock, so the input() function will
 * compute the TED error term at the proper symbol sampling instant.
 */
static void advance_input_clock()
{
    d_input_clock = (d_input_clock + 1) % d_inputs_per_symbol;
}

/*
 * Reset the timing error detector
 */
void sync_reset()
{
    complex float data[1] = { CMPLXF(0.0f, 0.0f) };
    complex float decision[1] = { CMPLXF(0.0f, 0.0f) };

    d_error = 0.0f;
    d_prev_error = 0.0f;

    empty_deque(d_input);
    push_front(d_input, data);
    push_front(d_input, data);
    push_front(d_input, data);  // push 3 values (previous, current, middle)

    empty_deque(d_decision);
    push_front(d_decision, decision);
    push_front(d_decision, decision);
    push_front(d_decision, decision);  // push 3 values (previous, current, middle)

    sync_reset_input_clock();
}

void create_timing_error_detector()
{
    complex float data[1] = { CMPLXF(0.0f, 0.0f) };
    complex float decision[1] = { CMPLXF(0.0f, 0.0f) };

    d_error = 0.0f;
    d_prev_error = 0.0f;
    d_inputs_per_symbol = 2; // The input samples per symbol required
    d_error_depth = 3;       // The number of input samples required to compute the error (not used)

    d_input = create_deque(); // create deque
    push_front(d_input, data);
    push_front(d_input, data);
    push_front(d_input, data);  // push 3 values (previous, current, middle)
    
    d_decision = create_deque();  // create deque
    push_front(d_decision, decision);
    push_front(d_decision, decision);
    push_front(d_decision, decision);  // push 3 values (previous, current, middle)

    sync_reset_input_clock();
}

void destroy_timing_error_detector()
{
    // destroy the deque's
    free(d_decision);
    free(d_input);
}

static void slice(complex float *z, complex float *x)
{
    unsigned int index = qpsk_decision_maker(*x);
    map_to_points(index, z);
}

/*
 * Provide a complex input sample to the TED algorithm
 *
 * @param x is pointer to the input sample
 */
void ted_input(complex float *x)
{
    complex float z[1];

    push_front(d_input, x);
    pop_back(d_input); // throw away

    slice(z, get(d_input, 0));

    push_front(d_decision, z);
    pop_back(d_decision); // throw away

    advance_input_clock();

    if (d_input_clock == 0)
    {
        d_prev_error = d_error;
        d_error = compute_error();
    }
}

/*
 * Revert the timing error detector processing state back one step
 *
 * @param preserve_error If true, don't revert the error estimate.
 */
void revert(bool preserve_error)
{
    if (d_input_clock == 0 && preserve_error != true)
        d_error = d_prev_error;

    revert_input_clock();

    push_back(d_decision, back(d_decision));
    pop_front(d_decision);  // throw away

    push_back(d_input, back(d_input));
    pop_front(d_input);  // throw away
}

/*
 * Constrains timing error to +/- the maximum value and corrects any
 * floating point invalid numbers
 */
static float enormalize(float error, float maximum)
{
    if (isnan(error) || isinf(error))
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
 * The error value indicates if the symbol was sampled early (-)
 * or late (+) relative to the reference symbol
 */
static float compute_error()
{
    complex float current =   *((complex float *)get(d_input, 0));
    complex float middle = *((complex float *)get(d_input, 1));
    complex float previous =  *((complex float *)get(d_input, 2));

    float errorInphase = (crealf(previous) - crealf(current)) * crealf(middle);
    float errorQuadrature = (cimagf(previous) - cimagf(current)) * cimagf(middle);

    return enormalize(errorInphase + errorQuadrature, 0.3f);
}

/*
 * Return the current symbol timing error estimate
 */
float get_error()
{
    return d_error;
}

/*
 * Return the number of input samples per symbol this timing
 * error detector algorithm requires.
 */
int get_inputs_per_symbol()
{
    return d_inputs_per_symbol;
}
