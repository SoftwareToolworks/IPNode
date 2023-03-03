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
#include "constellation.h"
#include "timing_error_detector.h"

// Globals

static struct audio_s *save_audio_config_p;
static struct demodulator_state_s demodulator_state;

static complex float rx_filter[NTAPS];
static complex float m_rxPhase;
static complex float m_rxRect;
static complex float recvBlock[8]; // 8 CYCLES per symbol

static float m_offset_freq;

static bool dcdDetect;

static float cnormf(complex float val)
{
    float realf = crealf(val);
    float imagf = cimagf(val);

    return (realf * realf) + (imagf * imagf);
}

bool dcd_detect()
{
    return dcdDetect;
}

void demod_init(struct audio_s *pa)
{
    save_audio_config_p = pa;
    dcdDetect = false;

    m_rxRect = cmplxconj((TAU * CENTER) / FS);
    m_rxPhase = cmplx(0.0f);

    struct demodulator_state_s *D = &demodulator_state;
    memset(D, 0, sizeof(struct demodulator_state_s));

    D->quick_attack = 0.080f * 0.2f;
    D->sluggish_decay = 0.00012f * 0.2f;
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

        csamples[i] = CMPLXF((float) pcm_I, (float) pcm_Q) / 32768.0;
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
 *                                                             THIS IS A MESS
 * Process one 1200 Baud symbol at 9600 rate
 */
void processSymbols(complex float csamples[])
{
    unsigned char diBits;

    struct demodulator_state_s *D = &demodulator_state;

    /*
     * Convert 9600 rate complex samples to baseband.
     */
    for (int i = 0; i < CYCLES; i++)
    {
        m_rxPhase *= m_rxRect;

        recvBlock[i] = csamples[i] * m_rxPhase;
    }

    rrc_fir(rx_filter, recvBlock, CYCLES);

    /*
     * Decimate by 4 for TED calculation (two samples per symbol)
     */
    for (int i = 0; i < CYCLES; i += 4)
    {
        ted_input(&recvBlock[i]);
    }

    complex float decision = getMiddleSample(); // use middle TED sample

    float fsam = cnormf(decision);

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
        complex float costasSymbol = decision * cmplxconj(get_phase());

        diBits = qpskToDiBit(costasSymbol);

        /*
         * The constellation gets rotated +45 degrees (rectangular)
         * from what was transmitted (diamond) with costas enabled
         */
        float d_error = phase_detector(costasSymbol);

        advance_loop(d_error);
        phase_wrap();
        frequency_limit();
    }
    else
    {
        /*
         * Rotate constellation from diamond to rectangular.
         * This makes easier quadrant detection possible.
         */
        complex float decodedSymbol = decision * cmplxconj(ROTATE45);

        diBits = qpskToDiBit(decodedSymbol);
    }

    /*
     * Detected frequency error
     */
    m_offset_freq = (get_frequency() * RS / TAU); // convert radians to freq at symbol rate

    /*
     * Add to the output stream MSB first
     */
    il2p_rec_bit((diBits >> 1) & 0x1);
    il2p_rec_bit(diBits & 0x1);
}

float get_offset_freq()
{
    return m_offset_freq;
}

