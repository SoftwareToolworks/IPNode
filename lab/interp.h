/*
 * Copyright (C) 2015 Dennis Sheirer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Translated from Java by Software Toolworks
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <complex.h>
#include <stdbool.h>
#include <math.h>

#include "qpsk-demo.h"

#define DEGREE45 (M_PI / 4.0)

#define ROTATE_FROM_PLUS_135 (-3.0 * DEGREE45)
#define ROTATE_FROM_PLUS_45 (-DEGREE45)
#define ROTATE_FROM_MINUS_45 (DEGREE45)
#define ROTATE_FROM_MINUS_135 (3.0 * DEGREE45)

typedef enum
{
    D01 = 0b01,
    D00 = 0b00,
    D10 = 0b10,
    D11 = 0b11,
    D99 = 99 // punt
} Dibit;

void create_interpolatingSampleBuffer(float);
void destroy_interpolatingSampleBuffer(void);
void interp_receive(complex float);
void resetAndAdjust(float);

float getSamplingPoint(void);

bool hasSymbol(void);

complex float getPrecedingSample(void);
complex float getCurrentSample(void);
complex float getMiddleSample(void);

void create_symbolEvaluator(void);
void setSymbols(complex float, complex float);
float getPhaseError(void);
float getTimingError(void);
Dibit getSymbolDecision(void);
complex float getReceivedSample(void);

void create_QPSKDemodulator(float);
Dibit demod_receive(complex float);

#ifdef __cplusplus
}
#endif
