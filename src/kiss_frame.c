/*
 * kiss_frame.c
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
#include <ctype.h>
#include <string.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "kiss_frame.h"
#include "tq.h"
#include "tx.h"

static void kiss_process_msg(unsigned char *, int, int);
static int kiss_unwrap(unsigned char *, int, unsigned char *);

static struct audio_s *save_audio_config_p;

void kiss_frame_init(struct audio_s *pa)
{
    save_audio_config_p = pa;
}

int kiss_encapsulate(unsigned char *in, int ilen, unsigned char *out)
{
    int olen;
    int j;

    olen = 0;
    out[olen++] = FEND;
    for (j = 0; j < ilen; j++)
    {
        unsigned char chr = in[j];

        if (chr == FEND)
        {
            out[olen++] = FESC;
            out[olen++] = TFEND;
        }
        else if (chr == FESC)
        {
            out[olen++] = FESC;
            out[olen++] = TFESC;
        }
        else
        {
            out[olen++] = chr;
        }
    }
    out[olen++] = FEND;

    return olen;
}

static int kiss_unwrap(unsigned char *in, int ilen, unsigned char *out)
{
    int olen = 0;
    int escaped_mode = 0;

    if (ilen < 2)
    {
        fprintf(stderr, "KISS message less than minimum length.\n");
        return 0;
    }

    if (in[ilen - 1] == FEND)
    {
        ilen--;
    }
    else
    {
        fprintf(stderr, "KISS frame should end with FEND.\n");
    }

    int j;

    if (in[0] == FEND)
    {
        j = 1;
    }
    else
    {
        j = 0;
    }

    for (; j < ilen; j++)
    {
        unsigned char chr = in[j];

        if (chr == FEND)
        {
            fprintf(stderr, "KISS frame should not have FEND in the middle.\n");
        }

        if (escaped_mode)
        {

            if (chr == TFESC)
            {
                out[olen++] = FESC;
            }
            else if (chr == TFEND)
            {
                out[olen++] = FEND;
            }
            else
            {
                fprintf(stderr, "KISS protocol error.  Found 0x%02x after FESC.\n", chr);
            }

            escaped_mode = 0;
        }
        else if (chr == FESC)
        {
            escaped_mode = 1;
        }
        else
        {
            out[olen++] = chr;
        }
    }

    return olen;
}

/*
 * Call from pseudo_terminal
 */
void kiss_rec_byte(kiss_frame_t *kf, unsigned char chr, int client)
{
    switch (kf->state)
    {

    case KS_SEARCHING: /* Searching for starting FEND. */
    default:
        if (chr == FEND)
        {
            kf->kiss_len = 0;
            kf->kiss_msg[kf->kiss_len++] = chr;
            kf->state = KS_COLLECTING;
        }
        break;

    case KS_COLLECTING: /* Frame collection in progress. */
        if (chr == FEND)
        {
            unsigned char unwrapped[AX25_MAX_PACKET_LEN];

            /* End of frame. */

            if (kf->kiss_len == 0)
            {
                /* Empty frame.  Starting a new one. */
                kf->kiss_msg[kf->kiss_len++] = chr;
                return;
            }

            if (kf->kiss_len == 1 && kf->kiss_msg[0] == FEND)
            {
                /* Empty frame.  Just go on collecting. */
                return;
            }

            kf->kiss_msg[kf->kiss_len++] = chr;

            int ulen = kiss_unwrap(kf->kiss_msg, kf->kiss_len, unwrapped);

            kiss_process_msg(unwrapped, ulen, client);

            kf->state = KS_SEARCHING;
            return;
        }

        if (kf->kiss_len < MAX_KISS_LEN)
        {
            kf->kiss_msg[kf->kiss_len++] = chr;
        }
        else
        {
            fprintf(stderr, "KISS message exceeded maximum length.\n");
        }
    }
}

static void kiss_process_msg(unsigned char *kiss_msg, int kiss_len, int client)
{
    int cmd = kiss_msg[0] & 0xf;
    packet_t pp;

    /* ignore all the other KISS bo-jive */

    switch (cmd)
    {
    case KISS_CMD_DATA_FRAME: /* 0 = Data Frame */

        pp = ax25_from_frame(kiss_msg + 1, kiss_len - 1);

        if (pp == NULL)
        {
            fprintf(stderr, "ERROR - Invalid KISS data frame.\n");
        }
        else
        {
            tq_append(TQ_PRIO_1_LO, pp);
        }
    }
}
