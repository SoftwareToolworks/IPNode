/*
 * Copyright 2010-2012 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <complex.h>

void createQPSKConstellation(void);
complex float *getQPSKConstellation(void);
complex float getQPSKQuadrant(unsigned char);
unsigned char qpskToDiBit(complex float);

#ifdef __cplusplus
}
#endif

