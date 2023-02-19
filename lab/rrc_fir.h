/*
 * rrc_fir.h
 *
 * Header file for Raised Cosine Low Pass Filter (LPF)
 * 
 * Software Toolworks, January 2023
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <complex.h>
#include "qpsk-demo.h"

#define NTAPS 65
#define GAIN 1.55  // Experimentally derived

    void rrc_fir(complex float[], complex float[], int);
    void rrc_make(float, float, float);

#ifdef __cplusplus
}
#endif
