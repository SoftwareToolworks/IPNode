/*
 * Copyright 2010-2012,2014,2018 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <complex.h>

#include "constellation.h"

static complex float d_qpsk[4];

void createQPSKConstellation()
{
    // Gray-coded
    d_qpsk[0] = CMPLXF(-SQRT2, -SQRT2);
    d_qpsk[1] = CMPLXF(SQRT2, -SQRT2);
    d_qpsk[2] = CMPLXF(-SQRT2, SQRT2);
    d_qpsk[3] = CMPLXF(SQRT2, SQRT2);
}

complex float *getQPSKConstellation()
{
    return d_qpsk;
}

complex float getQPSKQuadrant(unsigned char diBit)
{
    return d_qpsk[diBit];
}

unsigned char qpskToDiBit(complex float sample)
{
    // Real component determines small bit.
    // Imag component determines big bit.
    return 2 * (cimagf(sample) > 0.0f) + (crealf(sample) > 0.0f);
}
