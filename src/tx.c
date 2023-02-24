/*
 * tx.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stddef.h>
#include <complex.h>

#include "ipnode.h"
#include "ax25_link.h"
#include "ax25_pad.h"
#include "audio.h"
#include "il2p.h"
#include "demod.h"
#include "tq.h"
#include "tx.h"
#include "ptt.h"
#include "dlq.h"
#include "rrc_fir.h"
#include "constellation.h"

#define WAIT_TIMEOUT_MS (60 * 1000)
#define WAIT_CHECK_EVERY_MS 10

static int tx_slottime;
static int tx_persist;
static int tx_txdelay;
static int tx_txtail;
static bool tx_fulldup;
static int tx_bits_per_sec;

#define BITS_TO_MS(b) (((b)*1000) / tx_bits_per_sec)
#define MS_TO_BITS(ms) (((ms)*tx_bits_per_sec) / 1000)

static void *tx_thread(void *);
static bool wait_for_clear_channel(int, int, bool);
static void tx_frames(int, packet_t);
static int send_one_frame(packet_t);
static void tx_symbol(complex float);

static pthread_t tx_tid;
static pthread_mutex_t audio_out_dev_mutex;
static struct audio_s *save_audio_config_p;

// Properties of the digitized sound stream & modem.

static int bit_count;
static int save_bit;

static complex float tx_filter[NTAPS];

static complex float m_txPhase;
static complex float m_txRect;

static complex float *m_qpsk;

void tx_init(struct audio_s *p_modem)
{
    save_audio_config_p = p_modem;

    tx_slottime = p_modem->slottime;
    tx_persist = p_modem->persist;
    tx_txdelay = p_modem->txdelay;
    tx_txtail = p_modem->txtail;
    tx_fulldup = p_modem->fulldup;
    tx_bits_per_sec = p_modem->baud * 2;

    tq_init(p_modem);

    il2p_mutex_init(&audio_out_dev_mutex);

    int e = pthread_create(&tx_tid, NULL, tx_thread, (void *)NULL);

    if (e != 0)
    {
        fprintf(stderr, "Fatal: Could not create transmitter thread for modem\n");
        exit(1);
    }

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

static bool wait_for_clear_channel(int slottime, int persist, bool fulldup)
{
    int n = 0;

    if (fulldup == false)
    {

    start_over_again:

        while (dcd_detect())
        {
            SLEEP_MS(WAIT_CHECK_EVERY_MS);

            n++;

            if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS))
            {
                return false;
            }
        }

        if (save_audio_config_p->dwait > 0)
        {
            SLEEP_MS(save_audio_config_p->dwait * 10);
        }

        if (dcd_detect())
        {
            goto start_over_again;
        }

        while (tq_peek(TQ_PRIO_0_HI) == NULL)
        {
            SLEEP_MS(slottime * 10);

            if (dcd_detect())
            {
                goto start_over_again;
            }

            int r = rand() & 0xff;

            if (r <= persist)
            {
                break;
            }
        }
    }

    while (!il2p_mutex_try_lock(&audio_out_dev_mutex))
    {
        SLEEP_MS(WAIT_CHECK_EVERY_MS);

        n++;

        if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS))
        {
            return false;
        }
    }

    return true;
}

static void *tx_thread(void *arg)
{
    while (1)
    {

        tq_wait_while_empty();

        while (tq_peek(TQ_PRIO_0_HI) != NULL || tq_peek(TQ_PRIO_1_LO) != NULL)
        {
            bool ok = wait_for_clear_channel(tx_slottime, tx_persist, tx_fulldup);

            int prio = TQ_PRIO_1_LO;
            packet_t pp = tq_remove(TQ_PRIO_0_HI);

            if (pp != NULL)
            {
                prio = TQ_PRIO_0_HI;
            }
            else
            {
                pp = tq_remove(TQ_PRIO_1_LO);
            }

            if (pp != NULL)
            {
                if (ok == true)
                {
                    tx_frames(prio, pp);
                    il2p_mutex_unlock(&audio_out_dev_mutex);
                }
                else
                {
                    ax25_delete(pp);
                }
            }
        }
    }

    return 0;
}

static int send_one_frame(packet_t pp)
{
    int ret = 0;

    if (ax25_is_null_frame(pp))
    {
        dlq_seize_confirm();

        SLEEP_MS(10);

        return 0;
    }

    ret = il2p_send_frame(pp);

    return ret;
}

static void tx_frames(int prio, packet_t pp)
{
    int numframe = 0;
    int num_bits = 0;
    int nb;
    int flags;
    bool done;

    double time_ptt = dtime_now();

    ptt_set(OCTYPE_PTT, 1);

    dlq_seize_confirm();

    flags = MS_TO_BITS(tx_txdelay * 10);

    il2p_send_idle(flags);

    /*
     * Give other threads some time
     */
    SLEEP_MS(10);

    num_bits += flags;

    /*
     * Send the frame called with
     */
    nb = send_one_frame(pp);

    if (nb > 0)
    {
        num_bits += nb;
        numframe++;
    }

    ax25_delete(pp);

    /*
     * Now while we are here, send any other
     * waiting frames.
     */
    done = false;

    while (numframe < 256 && (done == false))
    {
        prio = TQ_PRIO_1_LO;
        pp = tq_peek(TQ_PRIO_0_HI);

        if (pp != NULL)
        {
            prio = TQ_PRIO_0_HI;
        }
        else
        {
            pp = tq_peek(TQ_PRIO_1_LO);
        }

        if (pp != NULL)
        {
            pp = tq_remove(prio);

            nb = send_one_frame(pp);

            if (nb > 0)
            {
                num_bits += nb;
                numframe++;
            }

            ax25_delete(pp);
        }
        else
        {
            done = true;
        }
    }

    /*
     * Now send the tx_tail
     */
    flags = MS_TO_BITS(tx_txtail * 10);

    il2p_send_idle(flags);
    num_bits += flags;

    /*
     * Get the souncard pushing
     */
    audio_flush();
    audio_wait();

    int duration = BITS_TO_MS(num_bits);

    double time_now = dtime_now();

    int already = (int)((time_now - time_ptt) * 1000.0);
    int wait_more = (duration - already);

    if (wait_more > 0)
    {
        SLEEP_MS(wait_more);
    }

    ptt_set(OCTYPE_PTT, 0);
}
