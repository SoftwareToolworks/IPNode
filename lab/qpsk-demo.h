/*
 * qpsk-demo.h
 *
 * Header file for QPSK project
 *
 * Software Toolworks, January 2023
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#define TX_FILENAME "/tmp/spectrum.raw"

#define FS 9600.0f
#define RS 1200.0f
#define RATE (int)(FS / RS)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAU (2.0 * M_PI)
#define ROTATE45 (M_PI / 4.0)

/*
 * This method is much faster than using cexpf()
 */
#define cmplx(value) (cosf(value) + sinf(value) * I)
#define cmplxconj(value) (cosf(value) + sinf(value) * -I)

#ifdef __cplusplus
}
#endif
