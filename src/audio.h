/*
 * audio.h
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>

#include "ipnode.h"
#include "ax25_pad.h"

#define OCTYPE_PTT 0 // Push To Talk
#define OCTYPE_DCD 1 // Data Carrier Detect
#define OCTYPE_CON 2 // Connected Indicator
#define OCTYPE_SYN 3 // Sync Indicator

#define NUM_OCTYPES 4
#define NUM_ICTYPES 1

#define ICTYPE_TXINH 0 // Transmit Inhibit
#define MAX_GPIO_NAME_LEN 20
#define ONE_BUF_TIME 10

    struct ictrl_s
    {
        int in_gpio_num;
        char in_gpio_name[MAX_GPIO_NAME_LEN];
        int inh_invert;
    };

    struct octrl_s
    {
        int out_gpio_num;
        char out_gpio_name[MAX_GPIO_NAME_LEN];
        int ptt_invert;
    };

    struct audio_s
    {
        bool defined;
        float baud;
        int dwait;
        int slottime;
        int persist;
        int txdelay;
        int txtail;
        bool fulldup;
        struct octrl_s octrl[NUM_OCTYPES];
        struct ictrl_s ictrl[NUM_ICTYPES];
        char adevice_in[80];
        char adevice_out[80];
        char mycall[AX25_MAX_ADDR_LEN];
    };

#define DEFAULT_ADEVICE "default"

#define DEFAULT_DWAIT 0
#define DEFAULT_SLOTTIME 10
#define DEFAULT_PERSIST 63
#define DEFAULT_TXDELAY 10
#define DEFAULT_TXTAIL 10
//#define DEFAULT_FULLDUP 0
#define DEFAULT_FULLDUP 1    //  testing

    int audio_open(struct audio_s *pa);
    int audio_get(void);
    void audio_put(unsigned char c);
    void audio_flush(void);
    void audio_wait(void);
    void audio_close(void);

#ifdef __cplusplus
}
#endif
