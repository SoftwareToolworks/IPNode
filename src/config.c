/*
 * config.c
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
#include <ctype.h>
#include <math.h>
#include <bsd/bsd.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "audio.h"
#include "config.h"
#include "tx.h"
#include "ax25_link.h"

#define MAXCMDLEN 1200

#ifdef NOTUSED
/* Do we have a string of all digits? */

static int alldigits(char *p)
{
    if (p == NULL)
        return (0);

    if (strlen(p) == 0)
        return (0);

    while (*p != '\0')
    {
        if (!isdigit(*p))
            return (0);

        p++;
    }

    return (1);
}

/* Do we have a string of all letters or + or -  ? */

static int alllettersorpm(char *p)
{
    if (p == NULL)
        return (0);
    if (strlen(p) == 0)
        return (0);
    while (*p != '\0')
    {
        if (!isalpha(*p) && *p != '+' && *p != '-')
            return (0);
        p++;
    }
    return (1);
}

static int parse_interval(char *str, int line)
{
    char *p;
    int sec;
    int nc = 0;
    int bad = 0;

    for (p = str; *p != '\0'; p++)
    {
        if (*p == ':')
            nc++;
        else if (!isdigit(*p))
            bad++;
    }

    if (bad > 0 || nc > 1)
    {
        printf("Config file, line %d: Time interval must be of the form minutes or minutes:seconds.\n", line);
    }

    p = strchr(str, ':');

    if (p != NULL)
    {
        sec = atoi(str) * 60 + atoi(p + 1);
    }
    else
    {
        sec = atoi(str) * 60;
    }

    return (sec);
}
#endif

static char *split(char *string)
{
    static char cmd[MAXCMDLEN];
    static char token[MAXCMDLEN];
    static char shutup[] = " ";
    static char *c = shutup; // Current position in command line.

    /*
     * If string is provided, make a copy.
     * Drop any CRLF at the end.
     * Change any tabs to spaces so we don't have to check for it later.
     */
    if (string != NULL)
    {
        c = cmd;

        for (char *s = string; *s != '\0'; s++)
        {
            if (*s == '\t')
            {
                *c++ = ' ';
            }
            else if (*s == '\r' || *s == '\n')
            {
                ;
            }
            else
            {
                *c++ = *s;
            }
        }

        *c = '\0';
        c = cmd;
    }

    /*
     * Get next part, separated by whitespace, keeping spaces within quotes.
     * Quotation marks inside need to be doubled.
     */

    while (*c == ' ')
    {
        c++;
    }

    char *t = token;
    bool in_quotes = false;

    for (; *c != '\0'; c++)
    {
        if (*c == '"')
        {
            if (in_quotes == true)
            {
                if (c[1] == '"')
                {
                    *t++ = *c++;
                }
                else
                {
                    in_quotes = false;
                }
            }
            else
            {
                in_quotes = true;
            }
        }
        else if (*c == ' ')
        {
            if (in_quotes == true)
            {
                *t++ = *c;
            }
            else
            {
                break;
            }
        }
        else
        {
            *t++ = *c;
        }
    }

    *t = '\0';

    t = token;

    if (*t == '\0')
    {
        return NULL;
    }

    return t;
}

void config_init(char *fname, struct audio_s *p_audio_config, struct misc_config_s *p_misc_config)
{
    /*
     * First apply defaults.
     */

    memset(p_audio_config, 0, sizeof(struct audio_s));

    strlcpy(p_audio_config->adevice_in, DEFAULT_ADEVICE, sizeof(p_audio_config->adevice_in));
    strlcpy(p_audio_config->adevice_out, DEFAULT_ADEVICE, sizeof(p_audio_config->adevice_out));

    p_audio_config->defined = false;
    p_audio_config->baud = RS;

    for (int ot = 0; ot < NUM_OCTYPES; ot++)
    {
        p_audio_config->octrl[ot].out_gpio_num = 0;
        p_audio_config->octrl[ot].ptt_invert = 0;
    }

    for (int it = 0; it < NUM_ICTYPES; it++)
    {
        p_audio_config->ictrl[it].in_gpio_num = 0;
        p_audio_config->ictrl[it].inh_invert = 0;
    }

    p_audio_config->dwait = DEFAULT_DWAIT;
    p_audio_config->slottime = DEFAULT_SLOTTIME;
    p_audio_config->persist = DEFAULT_PERSIST;
    p_audio_config->txdelay = DEFAULT_TXDELAY;
    p_audio_config->txtail = DEFAULT_TXTAIL;
    p_audio_config->fulldup = DEFAULT_FULLDUP;

    strlcpy(p_audio_config->mycall, "NOCALL", 6);

    memset(p_misc_config, 0, sizeof(struct misc_config_s));

    /* connected mode. */

    p_misc_config->frack = AX25_T1V_FRACK_DEFAULT;     /* Number of seconds to wait for ack to transmission. */
    p_misc_config->retry = AX25_N2_RETRY_DEFAULT;      /* Number of times to retry before giving up. */
    p_misc_config->paclen = AX25_N1_PACLEN_DEFAULT;    /* Max number of bytes in information part of frame. */
    p_misc_config->maxframe = AX25_K_MAXFRAME_DEFAULT; /* Max frames to send before ACK.  mod 8 "Window" size. */

    char filepath[128];

    strlcpy(filepath, fname, sizeof(filepath));

    FILE *fp = fopen(filepath, "r");

    if (fp == NULL && strcmp(fname, "il2pmodem.conf") == 0)
    {

        strlcpy(filepath, "", sizeof(filepath));

        char *p = getenv("HOME");

        if (p != NULL)
        {
            strlcpy(filepath, p, sizeof(filepath));
            strlcat(filepath, "/il2pmodem.conf", sizeof(filepath));

            fp = fopen(filepath, "r");
        }
    }

    if (fp == NULL)
    {
        fprintf(stderr, "Warning: Could not open config file %s\n", filepath);
        return;
    }

    char stuff[MAXCMDLEN];
    int line = 0;

    while (fgets(stuff, sizeof(stuff), fp) != NULL)
    {
        line++;

        char *t = split(stuff);

        if (t == NULL)
        {
            continue;
        }

        // Ignore comments

        if (*t == '#' || *t == '*')
        {
            continue;
        }

        /*
         * ADEVICE
         */

        if (strcasecmp(t, "adevice") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Config file: Missing name of audio device for ADEVICE command on line %d.\n", line);
                continue;
            }

            strlcpy(p_audio_config->adevice_in, t, sizeof(p_audio_config->adevice_in));
            strlcpy(p_audio_config->adevice_out, t, sizeof(p_audio_config->adevice_out));

            p_audio_config->defined = true;
        }

        /*
         * MYCALL station
         */
        else if (strcasecmp(t, "mycall") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                fprintf(stderr, "Config file: Missing value for MYCALL command on line %d.\n", line);
                continue;
            }
            else
            {
                char call_no_ssid[AX25_MAX_ADDR_LEN];
                int ssid; /* not used */

                for (char *p = t; *p != '\0'; p++)
                {
                    if (islower(*p))
                    {
                        *p = toupper(*p);
                    }
                }

                if (!ax25_parse_addr(1, t, call_no_ssid, &ssid))
                {
                    fprintf(stderr, "Config file: Invalid value for MYCALL command on line %d.\n", line);
                    continue;
                }

                strlcpy(p_audio_config->mycall, t, sizeof(p_audio_config->mycall));
            }
        }

        /*
         * PTT 		- Push To Talk signal line.
         * DCD		- Data Carrier Detect indicator.
         * CON		- Connected to another station indicator.
         * SYN          - Received IL2P Sync Symbol
         *
         * xxx  GPIO  [-]gpio-num
         *
         */

        else if (strcasecmp(t, "PTT") == 0 || strcasecmp(t, "DCD") == 0 || strcasecmp(t, "CON") == 0 || strcasecmp(t, "SYN") == 0)
        {
            int ot = 0;
            char otname[8];

            if (strcasecmp(t, "PTT") == 0)
            {
                ot = OCTYPE_PTT;
                strlcpy(otname, "PTT", sizeof(otname));
            }
            else if (strcasecmp(t, "DCD") == 0)
            {
                ot = OCTYPE_DCD;
                strlcpy(otname, "DCD", sizeof(otname));
            }
            else if (strcasecmp(t, "CON") == 0)
            {
                ot = OCTYPE_CON;
                strlcpy(otname, "CON", sizeof(otname));
            }
            else if (strcasecmp(t, "SYN") == 0)
            {
                ot = OCTYPE_SYN;
                strlcpy(otname, "SYN", sizeof(otname));
            }

            t = split(NULL);

            if (t == NULL)
            {
                printf("Config file line %d: Missing output control device for %s command.\n", line, otname);
                continue;
            }

            if (strcasecmp(t, "GPIO") == 0)
            {
                t = split(NULL);

                if (t == NULL)
                {
                    printf("Config file line %d: Missing GPIO number for %s.\n", line, otname);
                    continue;
                }

                if (*t == '-')
                {
                    p_audio_config->octrl[ot].out_gpio_num = atoi(t + 1);
                    p_audio_config->octrl[ot].ptt_invert = 1;
                }
                else
                {
                    p_audio_config->octrl[ot].out_gpio_num = atoi(t);
                    p_audio_config->octrl[ot].ptt_invert = 0;
                }
            }
        }

        /*
         * INPUTS
         *
         * TXINH - TX holdoff input
         *
         * TXINH GPIO [-]gpio-num (only type supported so far)
         */

        else if (strcasecmp(t, "TXINH") == 0)
        {
            char itname[8];

            strlcpy(itname, "TXINH", sizeof(itname));

            t = split(NULL);

            if (t == NULL)
            {

                printf("Config file line %d: Missing input type name for %s command.\n", line, itname);
                continue;
            }

            if (strcasecmp(t, "GPIO") == 0)
            {
                t = split(NULL);

                if (t == NULL)
                {
                    printf("Config file line %d: Missing GPIO number for %s.\n", line, itname);
                    continue;
                }

                if (*t == '-')
                {
                    p_audio_config->ictrl[ICTYPE_TXINH].in_gpio_num = atoi(t + 1);
                    p_audio_config->ictrl[ICTYPE_TXINH].inh_invert = 1;
                }
                else
                {
                    p_audio_config->ictrl[ICTYPE_TXINH].in_gpio_num = atoi(t);
                    p_audio_config->ictrl[ICTYPE_TXINH].inh_invert = 0;
                }
            }
        }

        /*
         * DWAIT n - Extra delay for receiver squelch. n = 10 mS units.
         */

        else if (strcasecmp(t, "DWAIT") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing delay time for DWAIT command.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= 0 && n <= 255)
            {
                p_audio_config->dwait = n;
            }
            else
            {
                p_audio_config->dwait = DEFAULT_DWAIT;

                printf("Line %d: Invalid delay time for DWAIT. Using %d.\n", line, p_audio_config->dwait);
            }
        }

        /*
         * SLOTTIME n		- For transmit delay timing. n = 10 mS units.
         */

        else if (strcasecmp(t, "SLOTTIME") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing delay time for SLOTTIME command.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= 0 && n <= 255)
            {
                p_audio_config->slottime = n;
            }
            else
            {
                p_audio_config->slottime = DEFAULT_SLOTTIME;

                printf("Line %d: Invalid delay time for persist algorithm. Using %d.\n",
                       line, p_audio_config->slottime);
            }
        }

        /*
         * PERSIST
         */

        else if (strcasecmp(t, "PERSIST") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing probability for PERSIST command.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= 0 && n <= 255)
            {
                p_audio_config->persist = n;
            }
            else
            {
                p_audio_config->persist = DEFAULT_PERSIST;

                printf("Line %d: Invalid probability for persist algorithm. Using %d.\n",
                       line, p_audio_config->persist);
            }
        }

        /*
         * TXDELAY n		- For transmit delay timing. n = 10 mS units.
         */

        else if (strcasecmp(t, "TXDELAY") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing time for TXDELAY command.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= 0 && n <= 255)
            {
                p_audio_config->txdelay = n;
            }
            else
            {
                p_audio_config->txdelay = DEFAULT_TXDELAY;

                printf("Line %d: Invalid time for transmit delay. Using %d.\n",
                       line, p_audio_config->txdelay);
            }
        }

        /*
         * TXTAIL n		- For transmit timing. n = 10 mS units.
         */

        else if (strcasecmp(t, "TXTAIL") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {

                printf("Line %d: Missing time for TXTAIL command.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= 0 && n <= 255)
            {
                p_audio_config->txtail = n;
            }
            else
            {
                p_audio_config->txtail = DEFAULT_TXTAIL;

                printf("Line %d: Invalid time for transmit timing. Using %d.\n",
                       line, p_audio_config->txtail);
            }
        }

        /*
         * FULLDUP  {on|off} 		- Full Duplex
         */
        else if (strcasecmp(t, "FULLDUP") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing parameter for FULLDUP command.  Expecting ON or OFF.\n", line);
                continue;
            }

            if (strcasecmp(t, "ON") == 0)
            {
                p_audio_config->fulldup = 1;
            }
            else if (strcasecmp(t, "OFF") == 0)
            {
                p_audio_config->fulldup = 0;
            }
            else
            {
                p_audio_config->fulldup = 0;

                printf("Line %d: Expected ON or OFF for FULLDUP.\n", line);
            }
        }

        /*
         * FRACK  n 		- Number of seconds to wait for ack to transmission.
         */

        else if (strcasecmp(t, "FRACK") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing value for FRACK.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= AX25_T1V_FRACK_MIN && n <= AX25_T1V_FRACK_MAX)
            {
                p_misc_config->frack = n;
            }
            else
            {
                printf("Line %d: Invalid FRACK time. Using default %d.\n", line, p_misc_config->frack);
            }
        }

        /*
         * RETRY  n 		- Number of times to retry before giving up.
         */

        else if (strcasecmp(t, "RETRY") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing value for RETRY.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= AX25_N2_RETRY_MIN && n <= AX25_N2_RETRY_MAX)
            {
                p_misc_config->retry = n;
            }
            else
            {
                printf("Line %d: Invalid RETRY number. Using default %d.\n", line, p_misc_config->retry);
            }
        }

        /*
         * PACLEN  n 		- Maximum number of bytes in information part.
         */

        else if (strcasecmp(t, "PACLEN") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing value for PACLEN.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= AX25_N1_PACLEN_MIN && n <= AX25_N1_PACLEN_MAX)
            {
                p_misc_config->paclen = n;
            }
            else
            {
                printf("Line %d: Invalid PACLEN value. Using default %d.\n", line, p_misc_config->paclen);
            }
        }

        /*
         * MAXFRAME  n 		- Max frames to send before ACK.  mod 8 "Window" size.
         *
         * Window size would make more sense but everyone else calls it MAXFRAME.
         */

        else if (strcasecmp(t, "MAXFRAME") == 0)
        {
            t = split(NULL);

            if (t == NULL)
            {
                printf("Line %d: Missing value for MAXFRAME.\n", line);
                continue;
            }

            int n = atoi(t);

            if (n >= AX25_K_MAXFRAME_MIN && n <= AX25_K_MAXFRAME_MAX)
            {
                p_misc_config->maxframe = n;
            }
            else
            {
                p_misc_config->maxframe = AX25_K_MAXFRAME_DEFAULT;

                printf("Line %d: Invalid MAXFRAME value outside range of %d to %d. Using default %d.\n",
                       line, AX25_K_MAXFRAME_MIN, AX25_K_MAXFRAME_MAX, p_misc_config->maxframe);
            }
        }
    }

    fclose(fp);
}
