/*
 * rx.c
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
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sys/types.h>

#include "ipnode.h"
#include "audio.h"
#include "demod.h"
#include "rx.h"
#include "ax25_link.h"
#include "timing_error_detector.h"

static pthread_t xmit_tid;

static void *rx_adev_thread(void *arg);
static struct audio_s *save_pa; /* Keep pointer to audio configuration */

bool node_shutdown;

void rx_init(struct audio_s *pa)
{
    save_pa = pa;

    if (pa->defined == true)
    {
        int e = pthread_create(&xmit_tid, NULL, rx_adev_thread, 0);

        if (e != 0)
        {
            fprintf(stderr, "Fatal: Could not create audio receive thread\n");
            exit(1);
        }

        create_timing_error_detector();
        demod_init(pa);

        node_shutdown = false;
    }
    else
    {
        fprintf(stderr, "Fatal: No audio device defined\n");
        exit(1);
    }
}

static void *rx_adev_thread(void *arg)
{
    complex float csamples[CYCLES];

    while (node_shutdown == false)
    {
        if (dcd_detect() == true)
        {
            if (demod_get_samples(csamples) == true)
            {
                processSymbols(csamples);
            }
        }
    }

    fprintf(stderr, "\nShutdown: Terminating after audio input closed.\n");
    exit(1);
}

void rx_process()
{
    struct dlq_item_s *pitem;

    while (1)
    {
        double timeout_value = ax25_link_get_next_timer_expiry();

        int timed_out = dlq_wait_while_empty(timeout_value);

        if (timed_out)
        {
            dl_timer_expiry();
        }
        else
        {
            pitem = dlq_remove();

            if (pitem != NULL)
            {
                switch (pitem->type)
                {
                case DLQ_REC_FRAME:
                    app_process_rec_packet(pitem->pp);
                    lm_data_indication(pitem);
                    break;

                case DLQ_CHANNEL_BUSY:
                    lm_channel_busy(pitem);
                    break;

                case DLQ_SEIZE_CONFIRM:
                    lm_seize_confirm(pitem);
                    break;
                }

                dlq_delete(pitem);
            }
        }
    }
}
