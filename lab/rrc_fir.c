/*
 * rrc_fir.c
 *
 * Raised Cosine Low Pass Filter (LPF)
 * 
 * Software Toolworks, January 2023
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdint.h>
#include <math.h>
#include <string.h>

#include "rrc_fir.h"

float coeffs[NTAPS];

/*
 * FIR Filter with specified impulse length
 */
void rrc_fir(complex float memory[], complex float in[], int length)
{
    for (int j = 0; j < length; j++)
    {
        memmove(&memory[0], &memory[1], (NTAPS - 1) * sizeof(complex float));
        memory[(NTAPS - 1)] = in[j];

        complex float y = 0.0f;

        for (int i = 0; i < NTAPS; i++)
        {
            y += (memory[i] * coeffs[i]);
        }

        in[j] = y * GAIN;
    }
}

void rrc_make(float fs, float rs, float alpha)
{
    float num, den;
    float spb = fs / rs; // samples per bit/symbol

    float scale = 0.f;

    for (int i = 0; i < NTAPS; i++)
    {
        float xindx = i - NTAPS / 2;
        float x1 = M_PI * xindx / spb;
        float x2 = 4.f * alpha * xindx / spb;
        float x3 = x2 * x2 - 1.f;

        if (fabsf(x3) >= 0.000001f) // Avoid Rounding errors...
        { 
            if (i != NTAPS / 2)
                num = cosf((1.f + alpha) * x1) +
                      sinf((1.f - alpha) * x1) / (4.f * alpha * xindx / spb);
            else
                num = cosf((1.f + alpha) * x1) + (1.f - alpha) * M_PI / (4.f * alpha);

            den = x3 * M_PI;
        }
        else
        {
            if (alpha == 1.f)
            {
                coeffs[i] = -1.f;
                scale += coeffs[i];
                continue;
            }

            x3 = (1.f - alpha) * x1;
            x2 = (1.f + alpha) * x1;

            num = (sinf(x2) * (1.f + alpha) * M_PI -
                   cosf(x3) * ((1.f - alpha) * M_PI * spb) / (4.f * alpha * xindx) +
                   sinf(x3) * spb * spb / (4.f * alpha * xindx * xindx));

            den = -32.f * M_PI * alpha * alpha * xindx / spb;
        }

        coeffs[i] = 4.f * alpha * num / den;
        scale += coeffs[i];
    }

    for (int i = 0; i < NTAPS; i++)
    {
        coeffs[i] = (coeffs[i] * GAIN) / scale;
    }
}
