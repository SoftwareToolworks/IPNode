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
#include "filter.h"
#include "constellation.h"

extern bool node_shutdown;
extern complex float m_txPhase;
extern complex float m_txRect;
extern complex float *m_qpsk;
extern int m_bit_count;
extern int m_save_bit;

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

static pthread_t tx_tid;
static pthread_mutex_t audio_out_dev_mutex;
static struct audio_s *save_audio_config_p;

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

    // Dibit control variables
    m_bit_count = 0;
    m_save_bit = 0;

    // Passband Center Frequency is 1000 Hz

    m_txRect = cmplx((TAU * CENTER) / FS);
    m_txPhase = cmplx(0.0f);

    m_qpsk = getQPSKConstellation();
}

static void *tx_thread(void *arg)
{
    while (node_shutdown == false)
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

static bool wait_for_clear_channel(int slottime, int persist, bool fulldup)
{
    int n = 0;

    if (fulldup == false)
    {

    start_over_again:

        while (dcd_detect() == true)
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

        if (dcd_detect() == true)
        {
            goto start_over_again;
        }

        while (tq_peek(TQ_PRIO_0_HI) == NULL)
        {
            SLEEP_MS(slottime * 10);

            if (dcd_detect() == true)
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

static int send_one_frame(packet_t pp)
{
    if (ax25_is_null_frame(pp))
    {
        dlq_seize_confirm();

        SLEEP_MS(10);

        return 0;
    }

    return il2p_send_frame(pp);
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

    num_bits += (flags * 2);

    /*
     * Send the frame
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
    num_bits += (flags * 2);

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
