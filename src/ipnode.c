/*
 * ipnode.c
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
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <bsd/bsd.h>
#include <sys/soundcard.h>

#include "ipnode.h"
#include "audio.h"
#include "config.h"
#include "demod.h"
#include "dlq.h"
#include "kiss_pt.h"
#include "kiss_frame.h"
#include "tq.h"
#include "tx.h"
#include "ptt.h"
#include "rx.h"
#include "ax25_link.h"
#include "fec.h"
#include "il2p.h"
#include "costas_loop.h"
#include "constellation.h"
#include "rrc_fir.h"

extern bool node_shutdown;

#define IS_DIR_SEPARATOR(c) ((c) == '/')

static struct audio_s audio_config;
static struct misc_config_s misc_config;
static char *progname;

/* Process control-C and window close events. */

static void cleanup(int x)
{
    node_shutdown = true; // kill tx/rx threads

    ptt_term();
    audio_close();

    SLEEP_SEC(1);
    exit(0);
}

int main(int argc, char *argv[])
{
    char config_file[100];
    char input_file[80];

    if (getuid() == 0 || geteuid() == 0)
    {
        printf("Do not run as root\n");
        exit(1);
    }

    char *pn = argv[0] + strlen(argv[0]);

    while (pn != argv[0] && !IS_DIR_SEPARATOR(pn[-1]))
        --pn;

    progname = pn;

    // default name
    strlcpy(config_file, "ipnode.conf", sizeof(config_file));

    config_init(config_file, &audio_config, &misc_config);

    strlcpy(input_file, "", sizeof(input_file));

    signal(SIGINT, cleanup);

    /*
     * Open the audio source
     */
    int err = audio_open(&audio_config);

    if (err < 0)
    {
        fprintf(stderr, "Fatal: No audio device found\n");
        SLEEP_SEC(5);
        exit(1);
    }

    createQPSKConstellation();

    /*
     * Create an RRC filter using the
     * Sample Rate, Baud, and Alpha
     */
    rrc_make(FS, RS, .35f);

    /*
     * Create a costas loop
     *
     * All terms are radians per sample.
     *
     * The loop bandwidth determins the lock range
     * and should be set around TAU/100 to TAU/200
     */
    create_control_loop((TAU / 180.0f), -1.0f, 1.0f);

    node_shutdown = false;

    dlq_init();
    ax25_link_init(&misc_config);
    il2p_init();
    // ptt_init(&audio_config);          ///////////// disabled for debugging
    tx_init(&audio_config);
    rx_init(&audio_config);    // also inits demod and TED

    kisspt_init();                    // kiss pseudo-terminal
    kiss_frame_init(&audio_config);   // normal kiss

    // Run daemon process forever

    rx_process();

    exit(EXIT_SUCCESS);
}

void app_process_rec_packet(packet_t pp)
{
    unsigned char fbuf[AX25_MAX_PACKET_LEN];

    int flen = ax25_pack(pp, fbuf);

    kisspt_send_rec_packet(KISS_CMD_DATA_FRAME, fbuf, flen); // KISS pseudo terminal
}
