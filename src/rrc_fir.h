/*
 * rrc_fir.h
 *
 * IP Node Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <complex.h>

#define NTAPS 127 // lower bauds need more taps
#define GAIN 1.85

    void rrc_fir(complex float *, complex float *, int);
    void rrc_make(float, float, float);

#ifdef __cplusplus
}
#endif
