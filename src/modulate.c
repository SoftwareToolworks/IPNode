/*
 * modulate.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <complex.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "ipnode.h"
#include "audio.h"
#include "rrc_fir.h"
#include "modulate.h"
#include "constellation.h"

// Properties of the digitized sound stream & modem.

static int bit_count;
static int save_bit;

static complex float tx_filter[NTAPS];

static complex float m_txPhase;
static complex float m_txRect;

static complex float *m_qpsk;

void modulate_init()
{
    bit_count = 0;
    save_bit = 0;

    // Center Frequency is 1000 Hz

    m_txRect = cmplx((TAU * CENTER) / FS);
    m_txPhase = cmplx(0.0f);
    
    m_qpsk = getQPSKConstellation();
}

/*
 * Modulate and upsample one symbol
 */
static void tx_symbol(complex float symbol)
{
    complex float signal[CYCLES];

    /*
     * Input symbol at RS baud
     *
     * Upsample to FS by zero padding
     */
    signal[0] = symbol;

    for (int i = 1; i < CYCLES; i++)
    {
        signal[i] = 0.0f;
    }

    /*
     * Root Cosine Filter
     */
    rrc_fir(tx_filter, signal, CYCLES);

    /*
     * Shift Baseband to Center Frequency
     */

    for (int i = 0; i < CYCLES; i++)
    {
        m_txPhase *= m_txRect;
        signal[i] = (signal[i] * m_txPhase) * 8192.0f; // Factor PCM amplitude
    }

    m_txPhase /= cabsf(m_txPhase); // normalize as magnitude can drift

    /*
     * Store PCM I and Q in audio output buffer
     */
    for (int i = 0; i < CYCLES; i++)
    {
        signed short pcm = (signed short)(crealf(signal[i])); // I
        audio_put(pcm & 0xff);
        audio_put((pcm >> 8) & 0xff);

        pcm = (signed short)(cimagf(signal[i])); // Q
        audio_put(pcm & 0xff);
        audio_put((pcm >> 8) & 0xff);
    }
}

void put_bit(unsigned char bit)
{
    if (bit_count == 0) // wait for 2 bits
    { 
        save_bit = bit;
        bit_count++;

        return;
    }

    unsigned char dibit = (save_bit << 1) | bit;

    tx_symbol(getQPSKQuadrant(dibit));

    save_bit = 0; // reset for next bits
    bit_count = 0;
}
