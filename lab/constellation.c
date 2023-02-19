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
#include "qpsk-demo.h"

complex float d_constellation[4];

void create_constellation_qpsk()
{
    // Gray-coded
    d_constellation[0] = CMPLXF(-M_SQRT2, -M_SQRT2);
    d_constellation[1] = CMPLXF(M_SQRT2, -M_SQRT2);
    d_constellation[2] = CMPLXF(-M_SQRT2, M_SQRT2);
    d_constellation[3] = CMPLXF(M_SQRT2, M_SQRT2);
}

void map_to_points(unsigned int index, complex float *points)
{
    *points = d_constellation[index];
}

unsigned int qpsk_decision_maker(complex float sample)
{
    // Real component determines small bit.
    // Imag component determines big bit.
    return 2 * (cimagf(sample) > 0.0f) + (crealf(sample) > 0.0f);
}
