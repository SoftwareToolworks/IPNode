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
#include "rrc_fir.h"
#include "constellation.h"

static complex float tx_filter[NTAPS];

complex float m_txPhase;
complex float m_txRect;
complex float *m_qpsk;

/*
 * Built-in multiply is slow because of
 * all the internal checking.
 * 
 * Skip all the checking during tight loops...
 */
static complex float fast_multiply(complex float a, complex float b)
{
    float ii = crealf(a) * crealf(b) - cimagf(a) * cimagf(b);
    float qq = crealf(a) * cimagf(b) + crealf(b) * cimagf(a);

    return CMPLXF(ii, qq); 
}

/*
 * Modulate and upsample symbols
 */
static void put_symbols(complex float symbols[], int symbolsCount)
{
    int outputSize = CYCLES * symbolsCount; // upsample 1200 to 9600

    complex float signal[outputSize]; // transmit signal

    /*
     * Use zero-insertion to change the
     * sample rate from 1200 to 9600.
     */
    for (int i = 0; i < symbolsCount; i++)
    {
        int index = (i * CYCLES); // compute once

        signal[index] = symbols[i];

        for (int j = 1; j < CYCLES; j++)
        {
            signal[index + j] = CMPLXF(0.0f, 0.0f);
        }
    }

#ifdef CLIP
    clip(signal, 1.9f, outputSize);
#endif

    /*
     * Root Cosine Filter baseband
     */
    rrc_fir(tx_filter, signal, outputSize);

    /*
     * Shift Baseband to Passband
     */
    for (int i = 0; i < outputSize; i++)
    {
        m_txPhase = fast_multiply(m_txPhase, m_txRect);
        signal[i] = fast_multiply(signal[i], m_txPhase) * 65535.0f; // Factor PCM amplitude
    }

    /*
     * Store PCM I and Q in audio output buffer
     */
    for (int i = 0; i < outputSize; i++)
    {
        short pcm = (short)(crealf(signal[i])); // I
        audio_put(pcm & 0xff);
        audio_put((pcm >> 8) & 0xff);

        pcm = (short)(cimagf(signal[i])); // Q
        audio_put(pcm & 0xff);
        audio_put((pcm >> 8) & 0xff);
    }
}

/*
 * Transmit octets
 */
static void put_frame_bits(unsigned char tx_bits[], int num_bits)
{
    int symbol_count = 0;
    int bit_count = 0;
    int save_bit;

    complex float tx_symbols[num_bits / 2];  // 2-Bits per symbol

    for (int i = 0; i < (num_bits / 2); i++)
    {
        tx_symbols[i] = CMPLXF(0.0f, 0.0f);
    }

    for (int i = 0; i < num_bits; i++)
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
 * Send flags which is 11001100 (0xCC) pattern
 */
void il2p_send_idle(int num_flags)
{
    int number_of_bits = 0;

    unsigned char tx_bits[num_flags * 8];

    for (int i = 0; i < (num_flags * 8); i++)
    {
        tx_bits[i] = 0U;
    }

    /*
     * Send byte to modulator MSB first
     */
    for (int i = 0; i < num_flags; i++)
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

