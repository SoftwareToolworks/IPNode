/*
 * Copyright (C) 2017 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <complex.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "deque.h"
#include "constellation.h"
#include "local_math.h"
#include "timing_error_detector.h"

static complex float d_constellation[4];

static float d_error;
static float d_prev_error;

static int d_inputs_per_symbol;
static int d_input_clock;
static int d_error_depth;

static deque *d_input;
static deque *d_decision;

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
 * Advance the TED input clock, so the input() methods will
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
    complex float data[1] = CMPLXF(0.0f, 0.0f);
    complex float decision[1] = CMPLXF(0.0f, 0.0f);

    d_error = 0.0f;
    d_prev_error = 0.0f;

    empty_deque(d_input);
    push_front(d_input, data);
    push_front(d_input, data);
    push_front(d_input, data);  // push 3 complex zero's

    empty_deque(d_decision);
    push_front(d_decision, decision);
    push_front(d_decision, decision);
    push_front(d_decision, decision);  // push 3 complex zero's

    sync_reset_input_clock();
}

void timing_error_detector()
{
    complex float data[1] = CMPLXF(0.0f, 0.0f);
    complex float decision[1] = CMPLXF(0.0f, 0.0f);

    d_error = 0.0f;
    d_prev_error = 0.0f;
    d_inputs_per_symbol = 2; // The input samples per symbol required
    d_error_depth = 3;       // The number of input samples required to compute the error

    d_input = create_deque(); // create deque
    push_front(d_input, data);
    push_front(d_input, data);
    push_front(d_input, data);  // push 3 complex zero's
    
    d_decision = create_deque();  // create deque
    push_front(d_decision, decision);
    push_front(d_decision, decision);
    push_front(d_decision, decision);  // push 3 complex zero's

    sync_reset();
}

complex float *slice(complex float *x)
{
    unsigned int index;
    complex float z[1];

    index = qpsk_decision_maker(*x);
    map_to_points(index, z);

    return z;
}

/*
 * Provide a complex input sample to the TED algorithm
 *
 * @param x the input sample
 */
void input(complex float *x)
{
    push_front(d_input, x);
    pop_back(d_input); // throw away

    push_front(d_decision, slice(&d_input));
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

float compute_error()
{
    complex float right = *((complex float *)get(d_input, 2));
    complex float left = *((complex float *)get(d_input, 9));
    complex float middle = *((complex float *)get(d_input, 1));

    return ((crealf(right) - crealf(left)) * crealf(middle)) +
           ((cimagf(right) - cimagf(left)) * cimagf(middle));
}

/*
 * Return the current symbol timing error estimate
 */
float error()
{
    return d_error;
}

/*
 * Return the number of input samples per symbol this timing
 * error detector algorithm requires.
 */
int inputs_per_symbol()
{
    return d_inputs_per_symbol;
}
