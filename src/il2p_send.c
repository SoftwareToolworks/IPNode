/*
 * il2p_send.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <complex.h>
#include <stdbool.h>
#include <string.h>

#include "ipnode.h"
#include "tx.h"
#include "il2p.h"
#include "audio.h"
#include "filter.h"
#include "constellation.h"

complex float m_txPhase;
complex float m_txRect;
complex float *m_qpsk;

static complex float interpolate(complex float a, complex float b, float x)
{
    return a + (b - a) * x / (float)CYCLES;
}

static void resampler(complex float out[], complex float in[], int length)
{
    for (int i = 0; i < (length - 1); i++)
    {
        for (int j = 0; j < CYCLES; j++)
        {
            out[(i * CYCLES) + j] = interpolate(in[i], in[i + 1], (float)j);
        }
    }

    for (int i = 0; i < CYCLES; i++)
    {
        out[((length - 1) * CYCLES) + i] = interpolate(in[length - 2], in[length - 1], (float)(i * CYCLES));
    }
}

static void clip(complex float samples[], float thresh, int length)
{
    for (int i = 0; i < length; i++)
    {
        complex float sam = samples[i];
        float mag = cabsf(sam);

        if (mag > thresh)
        {
            sam *= thresh / mag;
        }

        samples[i] = sam;
    }
}

/*
 * Modulate and upsample symbols
 */
static void put_symbols(complex float symbols[], int symbolsCount)
{
    int outputSize = CYCLES * symbolsCount; // upsample 1200 to 9600

    complex float signal[outputSize]; // transmit signal

    /*
     * Use linear interpolation to change the
     * sample rate from 1200 to 9600.
     */
    resampler(signal, symbols, symbolsCount);

    txFilter(signal, signal, symbolsCount);
    
    clip(signal, 2.0f, outputSize);

    /*
     * Shift Baseband to Passband
     */

    for (int i = 0; i < outputSize; i++)
    {
        m_txPhase *= m_txRect;
        signal[i] = (signal[i] * m_txPhase) * 8192.0f; // Factor PCM amplitude
    }

    m_txPhase /= cabsf(m_txPhase); // normalize as magnitude can drift

    /*
     * Store PCM I and Q in audio output buffer
     */
    for (int i = 0; i < outputSize; i++)
    {
        signed short pcm = (signed short)(crealf(signal[i])); // I
        audio_put(pcm & 0xff);
        audio_put((pcm >> 8) & 0xff);

        pcm = (signed short)(cimagf(signal[i])); // Q
        audio_put(pcm & 0xff);
        audio_put((pcm >> 8) & 0xff);
    }
}

/*
 * Transmit octets
 */
static void put_frame_bits(unsigned char tx_bits[], int length)
{
    int symbol_count = 0;
    int bit_count = 0;
    int save_bit;

    complex float tx_symbols[length / 2];  // 2-Bits per symbol

    for (int i = 0; i < (length / 2); i++)
    {
        tx_symbols[i] = CMPLXF(0.0f, 0.0f);
    }

    for (int i = 0; i < length; i++)
    {
        if (bit_count == 0) // wait for 2 bits
        {
            save_bit = tx_bits[i];
            bit_count++;

            continue;
        }

        unsigned char dibit = (save_bit << 1) | tx_bits[i];

        tx_symbols[symbol_count++] = getQPSKQuadrant(dibit);

        save_bit = 0; // reset for next bits
        bit_count = 0;
    }

    put_symbols(tx_symbols, symbol_count);
}

/*
 * Transmit bits are stored in tx_bits array
 */
int il2p_send_frame(packet_t pp)
{
    unsigned char encoded[IL2P_MAX_PACKET_SIZE];

    encoded[0] = (IL2P_SYNC_WORD >> 16) & 0xff;
    encoded[1] = (IL2P_SYNC_WORD >> 8) & 0xff;
    encoded[2] = (IL2P_SYNC_WORD)&0xff;

    int elen = il2p_encode_frame(pp, encoded + IL2P_SYNC_WORD_SIZE);

    if (elen == -1)
    {
        fprintf(stderr, "Fatal: IL2P: Unable to encode frame into IL2P\n");
        return -1;
    }

    elen += IL2P_SYNC_WORD_SIZE;

    unsigned char tx_bits[elen * 8];

    for (int i = 0; i < (elen * 8); i++)
    {
        tx_bits[i] = 0U;
    }

    int number_of_bits = 0;

    /*
     * Send byte to modulator MSB first
     */
    for (int j = 0; j < elen; j++)
    {
        unsigned char x = encoded[j];

        for (int k = 0; k < 8; k++)
        {
            tx_bits[(j * 8) + k] = (x & 0x80) != 0;
            x <<= 1;
        }

        number_of_bits += 8;
    }

    put_frame_bits(tx_bits, number_of_bits);

    return number_of_bits;
}

/*
 * Send txdelay and txtail symbols to modulator
 * Send a flag which is 11001100 (0xCC) pattern
 */
void il2p_send_idle(int flags)
{
    int number_of_bits = 0;

    unsigned char tx_bits[flags * 8];

    for (int i = 0; i < (flags * 8); i++)
    {
        tx_bits[i] = 0U;
    }

    /*
     * Send byte to modulator MSB first
     */
    for (int i = 0; i < flags; i++)
    {
        unsigned char x = FLAG;

        for (int j = 0; j < 8; j++)
        {
            tx_bits[(i * 8) + j] = (x & 0x80) != 0;
            x <<= 1;
        }

        number_of_bits += 8;
    }

    put_frame_bits(tx_bits, number_of_bits);
}

