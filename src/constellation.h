/*
 * Copyright 2010-2012 Free Software Foundation, Inc.
 *
 * Converted to C by Software Toolworks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <complex.h>

#define SQRT2 1.41421356237309504880

void createQPSKConstellation(void);
complex float *getQPSKConstellation(void);
complex float getQPSKQuadrant(unsigned int);
unsigned int qpskToDiBit(complex float);

