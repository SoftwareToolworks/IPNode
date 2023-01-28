/*
 * tq.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "audio.h"
#include "tq.h"

static packet_t queue_head[TQ_NUM_PRIO]; /* Head of linked list for each queue. */

static pthread_mutex_t tq_mutex;
static pthread_mutex_t wake_up_mutex; /* Required by cond_wait. */
static pthread_cond_t wake_up_cond;   /* Notify transmit thread when queue not empty. */

static bool xmit_thread_is_waiting;
static struct audio_s *save_audio_config_p;

static bool tq_is_empty()
{
    for (int p = 0; p < TQ_NUM_PRIO; p++)
    {
        if (queue_head[p] != NULL)
            return false;
    }

    return true;
}

void tq_init(struct audio_s *audio_config_p)
{
    save_audio_config_p = audio_config_p;

    for (int p = 0; p < TQ_NUM_PRIO; p++)
    {
        queue_head[p] = NULL;
    }

    /*
     * Mutex to coordinate access to the queue.
     */
    pthread_mutex_init(&tq_mutex, NULL);

    xmit_thread_is_waiting = false;

    int err = pthread_cond_init(&wake_up_cond, NULL);

    if (err != 0)
    {
        fprintf(stderr, "tq_init: pthread_cond_init err=%d\n", err);
        exit(1);
    }

    pthread_mutex_init(&wake_up_mutex, NULL);
}

/*
 * Called from kiss_frame
 */
void tq_append(int prio, packet_t pp)
{
    packet_t pnext;

    if (pp == NULL)
    {
        fprintf(stderr, "INTERNAL ERROR:  tq_append NULL packet pointer\n");
        return;
    }

    il2p_mutex_lock(&tq_mutex);

    if (queue_head[prio] == NULL)
    {
        queue_head[prio] = pp;
    }
    else
    {
        packet_t plast = queue_head[prio];

        while ((pnext = ax25_get_nextp(plast)) != NULL)
        {
            plast = pnext;
        }

        ax25_set_nextp(plast, pp);
    }

    il2p_mutex_unlock(&tq_mutex);

    if (xmit_thread_is_waiting == true)
    {
        il2p_mutex_lock(&wake_up_mutex);

        int err = pthread_cond_signal(&wake_up_cond);

        if (err != 0)
        {
            fprintf(stderr, "tq_append: pthread_cond_signal err=%d\n", err);
            exit(1);
        }

        il2p_mutex_unlock(&wake_up_mutex);
    }
}

/*
 * Called from ax25_link
 */
void lm_data_request(int prio, packet_t pp)
{
    packet_t pnext;

    if (pp == NULL)
    {
        return;
    }

    /*
     * Is transmit queue out of control?
     */
    if (tq_count(prio, "", "", 0) > 250)
    {
        fprintf(stderr, "Warning: Transmit packet queue for channel is extremely long.\n");
        fprintf(stderr, "Perhaps the channel is so busy there is no opportunity to send.\n");
    }

    il2p_mutex_lock(&tq_mutex);

    if (queue_head[prio] == NULL)
    {
        queue_head[prio] = pp;
    }
    else
    {
        packet_t plast = queue_head[prio];

        while ((pnext = ax25_get_nextp(plast)) != NULL)
        {
            plast = pnext;
        }

        ax25_set_nextp(plast, pp);
    }

    il2p_mutex_unlock(&tq_mutex);

    if (xmit_thread_is_waiting == true)
    {
        il2p_mutex_lock(&wake_up_mutex);

        int err = pthread_cond_signal(&(wake_up_cond));

        if (err != 0)
        {
            fprintf(stderr, "lm_data_request: pthread_cond_signal err=%d\n", err);
            exit(1);
        }

        il2p_mutex_unlock(&wake_up_mutex);
    }
}

/*
 * Called from ax25_link
 */
void lm_seize_request()
{
    int prio = TQ_PRIO_1_LO;

    packet_t pnext;
    packet_t pp = ax25_new();

    il2p_mutex_lock(&tq_mutex);

    if (queue_head[prio] == NULL)
    {
        queue_head[prio] = pp;
    }
    else
    {
        packet_t plast = queue_head[prio];

        while ((pnext = ax25_get_nextp(plast)) != NULL)
        {
            plast = pnext;
        }

        ax25_set_nextp(plast, pp);
    }

    il2p_mutex_unlock(&tq_mutex);

    if (xmit_thread_is_waiting == true)
    {
        int err;

        il2p_mutex_lock(&wake_up_mutex);

        err = pthread_cond_signal(&(wake_up_cond));

        if (err != 0)
        {
            fprintf(stderr, "lm_seize_request: pthread_cond_signal err=%d\n", err);
            exit(1);
        }

        il2p_mutex_unlock(&(wake_up_mutex));
    }
}

/*
 * Called from tx
 */
void tq_wait_while_empty()
{
    il2p_mutex_lock(&tq_mutex);

    bool is_empty = tq_is_empty();

    il2p_mutex_unlock(&tq_mutex);

    if (is_empty == true)
    {
        il2p_mutex_lock(&wake_up_mutex);

        xmit_thread_is_waiting = true;

        int err = pthread_cond_wait(&wake_up_cond, &wake_up_mutex);

        xmit_thread_is_waiting = false;

        if (err != 0)
        {
            fprintf(stderr, "tq_wait_while_empty: pthread_cond_wait err=%d\n", err);
            exit(1);
        }

        il2p_mutex_unlock(&wake_up_mutex);
    }
}

/*
 * Called from tx
 */
packet_t tq_remove(int prio)
{
    packet_t result_p;

    il2p_mutex_lock(&tq_mutex);

    if (queue_head[prio] == NULL)
    {
        result_p = NULL;
    }
    else
    {
        result_p = queue_head[prio];
        queue_head[prio] = ax25_get_nextp(result_p);
        ax25_set_nextp(result_p, NULL);
    }

    il2p_mutex_unlock(&tq_mutex);

    return result_p;
}

/*
 * Called from tx
 */
packet_t tq_peek(int prio)
{
    return queue_head[prio];
}

/*
 * Called from local and kiss_frame
 */
int tq_count(int prio, char *source, char *dest, int bytes)
{
    if (prio == -1) // kiss pseudo terminal
    {
        return (tq_count(TQ_PRIO_0_HI, source, dest, bytes) + tq_count(TQ_PRIO_1_LO, source, dest, bytes));
    }

    // Array bounds check.  FIXME: TODO:  should have internal error instead of dying.

    if (prio < 0 || prio >= TQ_NUM_PRIO)
    {
        fprintf(stderr, "INTERNAL ERROR - tq_count(%d, \"%s\", \"%s\", %d)\n", prio, source, dest, bytes);
        return 0;
    }

    if (queue_head[prio] == 0)
    {
        return 0;
    }

    // Don't want lists being rearranged while we are traversing them.

    il2p_mutex_lock(&tq_mutex);

    packet_t pp = queue_head[prio];

    int n = 0; // Result.  Number of bytes or packets.

    while (pp != NULL)
    {
        // Consider only real packets.
        bool count_it = true;

        if (source != NULL && *source != '\0')
        {
            char frame_source[AX25_MAX_ADDR_LEN];
            ax25_get_addr_with_ssid(pp, AX25_SOURCE, frame_source);

            if (strcmp(source, frame_source) != 0)
                count_it = false;
        }

        if ((count_it == true) && (dest != NULL) && (*dest != '\0'))
        {
            char frame_dest[AX25_MAX_ADDR_LEN];
            ax25_get_addr_with_ssid(pp, AX25_DESTINATION, frame_dest);

            if (strcmp(dest, frame_dest) != 0)
                count_it = false;
        }

        if (count_it == true)
        {
            if (bytes)
            {
                n += ax25_get_frame_len(pp);
            }
            else
            {
                n++;
            }
        }

        pp = ax25_get_nextp(pp);
    }

    il2p_mutex_unlock(&tq_mutex);

    return n;
}
