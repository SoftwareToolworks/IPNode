/*
 * demod.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _POSIX_C_SOURCE 200112L

// Includes

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <complex.h>

#include "ipnode.h"
#include "demod.h"
#include "audio.h"
#include "rx.h"
#include "il2p.h"
#include "costas_loop.h"
#include "rrc_fir.h"
#include "ptt.h"

// Globals

static struct audio_s *save_audio_config_p;
static struct demodulator_state_s demodulator_state;

static complex float rx_filter[NTAPS];
static complex float m_rxPhase;
static complex float m_rxRect;

static complex float recvBlock[16]; // CYCLES * 2 double length receive sample buffer

float m_offset_freq;

static int dcdDetect;

static float cnormf(complex float val)
{
    float realf = crealf(val);
    float imagf = cimagf(val);

    return (realf * realf) + (imagf * imagf);
}

int dcd_detect()
{
    return dcdDetect;
}

#ifdef PREAMBLE
static void detect_preamble()
{
    /*
     * TODO - Detect preamble and postamble
     * and declare/clear DCD
     */

    ptt_set(OCTYPE_DCD, 1); // turn on the LED
    dcdDetect = 1;
    return 1;

    ptt_set(OCTYPE_DCD, 0); // turn off the LED
    dcdDetect = 0;
    return 0;
}
#endif

void demod_init(struct audio_s *pa)
{
    save_audio_config_p = pa;
    dcdDetect = 0;

    m_rxRect = cmplx((TAU * CENTER) / FS);
    m_rxPhase = cmplx(0.0f);

    struct demodulator_state_s *D = &demodulator_state;
    memset(D, 0, sizeof(struct demodulator_state_s));

    D->quick_attack = 0.080f * 0.2f;
    D->sluggish_decay = 0.00012f * 0.2f;
}

/*
 * Gray coded QPSK demodulation function
 */
static void qpskDemodulate(complex float symbol, int outputBits[])
{
    outputBits[0] = crealf(symbol) < 0.0f; // I < 0 ?
    outputBits[1] = cimagf(symbol) < 0.0f; // Q < 0 ?
}

bool demod_get_samples(complex float csamples[])
{
    signed short pcm_I, pcm_Q;
    int lsb, msb;

    /*
     * read in CYCLES worth of samples
     * which represents 9600 rate
     */
    for (int i = 0; i < CYCLES; i++)
    {
        lsb = audio_get(); // get I byte

        if (lsb < 0)
            return false;

        msb = audio_get(); // next byte

        if (msb < 0)
            return false;

        pcm_I = (msb << 8) | lsb;

        lsb = audio_get(); // get Q byte

        if (lsb < 0)
            return false;

        msb = audio_get(); // next byte

        if (msb < 0)
            return false;

        pcm_Q = (msb << 8) | lsb;

        csamples[i] = CMPLXF((float) pcm_I, (float) pcm_Q) / 16384.0;
    }

    return true;
}

int demod_get_audio_level(struct demodulator_state_s *D)
{
    // Take half of peak-to-peak for received audio level.

    return (int)((D->alevel_rec_peak - D->alevel_rec_valley) * 50.0f + 0.5f);
}

/*
 * QPSK Receive function
 *
 * Remove any frequency and timing offsets
 *
 * Process one 1200 Baud symbol at 9600 rate
 */
void processSymbols(complex float csamples[])
{
    int diBits[2];

    struct demodulator_state_s *D = &demodulator_state;

    /*
     * Convert 9600 rate complex samples to baseband.
     */
    for (int i = 0; i < CYCLES; i++)
    {
        int extended = (CYCLES + i); // compute once
        m_rxPhase *= m_rxRect;

        recvBlock[i] = recvBlock[extended];
        recvBlock[extended] = csamples[i] * m_rxPhase;
    }

    m_rxPhase /= cabsf(m_rxPhase); // normalize oscillator as magnitude can drift

    /*
     * Baseband Root Cosine low-pass Filter
     * Using the previous sample block.
     *
     * We're still at 9600 sample rate
     */
    rrc_fir(rx_filter, recvBlock, CYCLES);

    /*
     * Now decimate the 9600 rate sample to the 1200 rate.
     */
    complex float decimatedSymbol = recvBlock[0];

    float fsam = cnormf(decimatedSymbol);

    if (fsam >= D->alevel_rec_peak)
    {
        D->alevel_rec_peak = fsam * D->quick_attack + D->alevel_rec_peak * (1.0f - D->quick_attack);
    }
    else
    {
        D->alevel_rec_peak = fsam * D->sluggish_decay + D->alevel_rec_peak * (1.0f - D->sluggish_decay);
    }

    if (fsam <= D->alevel_rec_valley)
    {
        D->alevel_rec_valley = fsam * D->quick_attack + D->alevel_rec_valley * (1.0f - D->quick_attack);
    }
    else
    {
        D->alevel_rec_valley = fsam * D->sluggish_decay + D->alevel_rec_valley * (1.0f - D->sluggish_decay);
    }

    if (get_costas_enable() == true)
    {
        complex float costasSymbol = decimatedSymbol * cmplxconj(get_phase());

        /*
         * The constellation gets rotated +45 degrees (rectangular)
         * from what was transmitted (diamond) with costas enabled
         */
        float d_error = phase_detector(costasSymbol);

        advance_loop(d_error);
        phase_wrap();
        frequency_limit();

        qpskDemodulate(costasSymbol, diBits);
    }
    else
    {
        /*
         * Rotate constellation from diamond to rectangular.
         * This makes easier quadrant detection possible.
         */
        complex float decodedSymbol = decimatedSymbol * cmplxconj(ROTATE45);

        qpskDemodulate(decodedSymbol, diBits);
    }

    /*
     * Detected frequency error
     */
    m_offset_freq = roundf(get_frequency() * CENTER / TAU); // convert radians to freq at symbol rate

    /*
     * TODO Declare EOF when +/- 100 Hz error
     */
    if ((m_offset_freq > (CENTER + 99.0f)) || (m_offset_freq < (CENTER - 99.0f)))
    {
        m_offset_freq = 9999.0f; // EOF
        return;
    }

    /*
     * Add to the output stream MSB first
     */
    il2p_rec_bit((diBits[0] >> 1) & 0x1);
    il2p_rec_bit(diBits[1] & 0x1);
}
