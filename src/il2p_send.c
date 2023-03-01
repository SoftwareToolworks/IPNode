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
#include "constellation.h"

extern int m_bit_count;
extern int m_save_bit;
extern complex float m_txPhase;
extern complex float m_txRect;

static complex float *tx_symbols;
static unsigned char *tx_bits;

static int m_number_of_bits_sent;

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
 * Modulate and upsample one symbol
 */
static void put_symbols()
{
    int symbolsCount = m_number_of_bits_sent / 2;
    int outputSize = CYCLES * symbolsCount;

    complex float signal[outputSize]; // transmit signal

    /*
     * Use linear interpolation to change the
     * sample rate from 1200 to 9600.
     */
    resampler(signal, tx_symbols, symbolsCount);

/////////////// PUT FILTER HERE and maybe delete clip() /////////////////////////////////

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
 * Transmit bits
 *
 * This routine will wait for two bits which
 * makes the dibit index for QPSK quadrant
 */
static void put_frame_bits()
{
    int symbol_count = 0;

    tx_symbols = (complex float *)calloc(m_number_of_bits_sent / 2, sizeof(complex float));

    for (int i = 0; i < m_number_of_bits_sent; i++)
    {
        if (m_bit_count == 0) // wait for 2 bits
        {
            m_save_bit = tx_bits[i];
            m_bit_count++;

            return;
        }

        unsigned char dibit = (m_save_bit << 1) | tx_bits[i];

        tx_symbols[symbol_count++] = getQPSKQuadrant(dibit);

        m_save_bit = 0; // reset for next bits
        m_bit_count = 0;
    }

    put_symbols();

    free(tx_symbols);
    free(tx_bits);
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

    tx_bits = (unsigned char *)calloc(elen, sizeof(unsigned char));

    m_number_of_bits_sent = 0; // incremented in send_bit

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

        m_number_of_bits_sent += 8;
    }

    put_frame_bits();

    return m_number_of_bits_sent;
}

/*
 * Send txdelay and txtail symbols to modulator
 */
void il2p_send_idle(int nsymbols)
{
    if ((nsymbols % 2) != 0)
    {
        nsymbols++;  // make it even
    }

    for (int i = 0; i < (nsymbols / 2); i++)
    {
        put_bit(1); // +BPSK
        put_bit(1);

        put_bit(0); // -BPSK
        put_bit(0);
    }

    m_number_of_bits_sent += nsymbols;
}
