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

#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static

#include "optparse.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
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
#include "ax25_pad.h"
#include "dlq.h"
#include "kiss_pt.h"
#include "kiss_frame.h"
#include "rrc_fir.h"
#include "modulate.h"
#include "tq.h"
#include "tx.h"
#include "ptt.h"
#include "rx.h"
#include "ax25_link.h"
#include "fec.h"
#include "il2p.h"
#include "costas_loop.h"
#include "constellation.h"

#define IS_DIR_SEPARATOR(c) ((c) == '/')

static char *progname;

static void opt_help()
{
    fprintf(stderr, "\nusage: %s [options]\n\n", progname);
    fprintf(stderr, "  --help    Print this message\n");
    exit(-1);
}

/* Process control-C and window close events. */

static void cleanup(int x)
{
    ptt_term();
    audio_close();

    SLEEP_SEC(1);
    exit(0);
}

static struct audio_s audio_config;
static struct misc_config_s misc_config;

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

    struct optparse options;

    struct optparse_long longopts[] = {
        {"help", 'h', OPTPARSE_NONE},
        {0, 0, 0}};

    // default name
    strlcpy(config_file, "ipnode.conf", sizeof(config_file));

    config_init(config_file, &audio_config, &misc_config);

    strlcpy(input_file, "", sizeof(input_file));

    optparse_init(&options, argv);

    int opt;

    while ((opt = optparse_long(&options, longopts, NULL)) != -1)
    {
        switch (opt)
        {
        case '?':
        case 'h':
            opt_help();
            break;
        }
    }

    /* Print remaining arguments to give user a hint of problem */

    char *arg;

    while ((arg = optparse_arg(&options)))
        fprintf(stderr, "%s\n", arg);

    signal(SIGINT, cleanup);

    /*
     * Open the audio source
     */

    audio_config.baud = RS;

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
     * Sample Rate, baud, and Alpha
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

    dlq_init();
    ax25_link_init(&misc_config);
    il2p_init();

    tx_init(&audio_config);
    modulate_init(&audio_config);

    rx_init(&audio_config);    // also inits demod and TED

    // ptt_init(&audio_config);          ///////////// disabled for debugging

    kisspt_init();
    kiss_frame_init(&audio_config);

    // Run daemon process

    rx_process();

    exit(EXIT_SUCCESS);
}

void app_process_rec_packet(packet_t pp)
{
    unsigned char fbuf[AX25_MAX_PACKET_LEN];

    int flen = ax25_pack(pp, fbuf);

    for (int i = 0; i < flen; i++)
    {
       fprintf(stderr, "%02X", fbuf[i]);
    }

    fprintf(stderr, "\n");
 
    kisspt_send_rec_packet(KISS_CMD_DATA_FRAME, fbuf, flen); // KISS pseudo terminal
}
