/*
 * audio.c
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
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <bsd/bsd.h>
#include <errno.h>
#include <alsa/asoundlib.h>

#include "ipnode.h"
#include "audio.h"
#include "demod.h"

#define roundup1k(n) (((n) + 0x3ff) & ~0x3ff)

/*
 * FYI snd_pcm_t is a typedef of struct _snd_pcm
 * Which is located in pcm_local.h in dev package
 *
 * https://github.com/alsa-project/alsa-lib/blob/master/src/pcm/pcm_local.h
 */

static struct
{
    snd_pcm_t *audio_in_handle;
    snd_pcm_t *audio_out_handle;

    unsigned char *inbuf_ptr;
    unsigned char *outbuf_ptr;

    int bytes_per_frame;
    int inbuf_size_in_bytes;
    int outbuf_size_in_bytes;
    int inbuf_len;
    int outbuf_len;
    int inbuf_next;
} adev;

static struct audio_s *save_audio_config_p;
static int channels;
static int bits_per_sample;

static int set_alsa_params(snd_pcm_t *, struct audio_s *, char *, char *);

int audio_open(struct audio_s *pa)
{
    char audio_in_name[30];
    char audio_out_name[30];

    channels = 2; // I and Q Stereo
    bits_per_sample = 16;

    save_audio_config_p = pa;

    memset(&adev, 0, sizeof(adev));

    adev.audio_in_handle = NULL;
    adev.audio_out_handle = NULL;

    if (pa->defined == true)
    {
        /* If not specified, the device names should be "default". */

        strlcpy(audio_in_name, pa->adevice_in, sizeof(audio_in_name));
        strlcpy(audio_out_name, pa->adevice_out, sizeof(audio_out_name));

        fprintf(stderr, "Audio device for both receive and transmit: %s\n", audio_in_name);

        int err = snd_pcm_open(&(adev.audio_in_handle), audio_in_name, SND_PCM_STREAM_CAPTURE, 0);

        if (err < 0)
        {
            return -1;
        }

        adev.inbuf_size_in_bytes = set_alsa_params(adev.audio_in_handle, pa, audio_in_name, "input");

        if (adev.inbuf_size_in_bytes <= 0)
        {
            return -1;
        }

        err = snd_pcm_open(&(adev.audio_out_handle), audio_out_name, SND_PCM_STREAM_PLAYBACK, 0);

        if (err < 0)
        {
            return -1;
        }

        adev.outbuf_size_in_bytes = set_alsa_params(adev.audio_out_handle, pa, audio_out_name, "output");

        if (adev.outbuf_size_in_bytes <= 0)
        {
            return -1;
        }

        adev.inbuf_ptr = (unsigned char *)calloc(adev.inbuf_size_in_bytes, sizeof(unsigned char));

        if (adev.inbuf_ptr == NULL)
            return -1;

        adev.outbuf_ptr = (unsigned char *)calloc(adev.outbuf_size_in_bytes, sizeof(unsigned char));

        if (adev.outbuf_ptr == NULL)
            return -1;

        adev.inbuf_len = 0;
        adev.outbuf_len = 0;
        adev.inbuf_next = 0;

        audio_wait();

        return 0;
    }

    return -1;
}

static int set_alsa_params(snd_pcm_t *handle, struct audio_s *pa, char *devname, char *inout)
{
    snd_pcm_hw_params_t *hw_params;

    int err = snd_pcm_hw_params_malloc(&hw_params);

    if (err < 0)
    {
        fprintf(stderr, "Could not alloc hw param structure.\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    err = snd_pcm_hw_params_any(handle, hw_params);

    if (err < 0)
    {
        fprintf(stderr, "Could not init hw param structure.\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    err = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);

    if (err < 0)
    {
        fprintf(stderr, "Could not set interleaved mode.\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    err = snd_pcm_hw_params_set_format(handle, hw_params, SND_PCM_FORMAT_S16_LE);

    if (err < 0)
    {
        fprintf(stderr, "Could not set sound format.\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    err = snd_pcm_hw_params_set_channels(handle, hw_params, channels); // stereo

    if (err < 0)
    {
        fprintf(stderr, "Could not set number of audio channels.\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    /* Audio sample rate. */

    unsigned int val = FS;

    int dir = 0;

    err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &val, &dir);

    if (err < 0)
    {
        fprintf(stderr, "Fatal: Could not set audio sample rate. %s ", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    if (val != (int)FS)
    {
        fprintf(stderr, "Fatal: Asked for %d samples/sec but got %d ", (int)FS, val);
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    int buf_size_in_bytes = roundup1k((val * (channels * bits_per_sample / 8) * ONE_BUF_TIME) / 1000);

#if __arm__
    /*
     * RPi hack
     *
     * Reducing buffer size is fine for input
     * but not so good for output
     */
    if (*inout == 'o')
    {
        buf_size_in_bytes = buf_size_in_bytes * 4;
    }
#endif

    snd_pcm_uframes_t fpp = buf_size_in_bytes / (channels * bits_per_sample / 8); // stereo

    dir = 0;

    err = snd_pcm_hw_params_set_period_size_near(handle, hw_params, &fpp, &dir);

    if (err < 0)
    {
        fprintf(stderr, "Could not set period size\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    err = snd_pcm_hw_params(handle, hw_params);

    if (err < 0)
    {
        fprintf(stderr, "Could not set hw params\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    /*
     * Driver might not like our suggested period size
     * and might have another idea
     */
    err = snd_pcm_hw_params_get_period_size(hw_params, &fpp, NULL);

    if (err < 0)
    {
        fprintf(stderr, "Could not get audio period size.\n%s\n", snd_strerror(err));
        fprintf(stderr, "for %s %s.\n", devname, inout);
        return -1;
    }

    snd_pcm_hw_params_free(hw_params);

    /*
     * A "frame" is one sample for all channels
     *
     * The read and write use units of frames, not bytes
     */
    adev.bytes_per_frame = snd_pcm_frames_to_bytes(handle, 1);

    buf_size_in_bytes = fpp * adev.bytes_per_frame;

    if (buf_size_in_bytes < 256 || buf_size_in_bytes > 32768)
    {
        buf_size_in_bytes = 2048;
    }

    return buf_size_in_bytes;
}

/*
 * Called by demod
 */
int audio_get()
{
    int err;

    int retries = 0;

    while (adev.inbuf_next >= adev.inbuf_len)
    {
        err = snd_pcm_readi(adev.audio_in_handle, adev.inbuf_ptr, adev.inbuf_size_in_bytes / adev.bytes_per_frame);

        if (err > 0)
        {
            adev.inbuf_len = err * adev.bytes_per_frame; /* convert to number of bytes */
            adev.inbuf_next = 0;
        }
        else if (err == 0)
        {
            /*
             * Didn't expect this, but it's not a problem
             * Wait a little while and try again
             */
            fprintf(stderr, "Audio input got zero bytes: %s\n", snd_strerror(err));
            SLEEP_MS(10);

            adev.inbuf_len = 0;
            adev.inbuf_next = 0;
        }
        else
        {
            fprintf(stderr, "Audio input device error code %d: %s\n", err, snd_strerror(err));

            if (err == (-EPIPE))
            {
                fprintf(stderr, "Most likely a slow CPU unable to keep up with the audio stream.\n");
            }

            /*
             * Try to recover a few times and eventually give up
             */
            if (++retries > 10)
            {
                adev.inbuf_len = 0;
                adev.inbuf_next = 0;

                return -1;
            }

            if (err == -EPIPE)
            {
                /*
                 * EPIPE means overrun
                 */
                snd_pcm_recover(adev.audio_in_handle, err, 1);
            }
            else
            {
                SLEEP_MS(250);
                snd_pcm_recover(adev.audio_in_handle, err, 1);
            }
        }
    }

    if (adev.inbuf_next < adev.inbuf_len)
        return adev.inbuf_ptr[adev.inbuf_next++];
    else
        return 0;
}

/*
 * Called externally by tx.c
 * but also internally
 */
void audio_flush()
{
    snd_pcm_status_t *status;

    snd_pcm_status_alloca(&status);

    int k = snd_pcm_status(adev.audio_out_handle, status);

    if (k != 0)
    {
        fprintf(stderr, "Audio output get status error.\n%s\n", snd_strerror(k));
    }

    if ((k = snd_pcm_status_get_state(status)) != SND_PCM_STATE_RUNNING)
    {
        k = snd_pcm_prepare(adev.audio_out_handle);

        if (k != 0)
        {
            fprintf(stderr, "Audio output start error.\n%s\n", snd_strerror(k));
        }
    }

    unsigned char *psound = adev.outbuf_ptr;
    int retries = 10;

    while (retries-- > 0)
    {
        k = snd_pcm_writei(adev.audio_out_handle, psound, adev.outbuf_len / adev.bytes_per_frame);

        if (k == -EPIPE)
        {
            fprintf(stderr, "Audio output data underrun.\n");
            snd_pcm_recover(adev.audio_out_handle, k, 1);
        }
        else if (k == -ESTRPIPE)
        {
            fprintf(stderr, "Driver suspended, recovering\n");
            snd_pcm_recover(adev.audio_out_handle, k, 1);
        }
        else if (k == -EBADFD)
        {
            k = snd_pcm_prepare(adev.audio_out_handle);

            if (k < 0)
            {
                fprintf(stderr, "Error preparing after bad state: %s\n", snd_strerror(k));
            }
        }
        else if (k < 0)
        {
            fprintf(stderr, "Audio write error: %s\n", snd_strerror(k));

            k = snd_pcm_prepare(adev.audio_out_handle);

            if (k < 0)
            {
                fprintf(stderr, "Error preparing after error: %s\n", snd_strerror(k));
            }
        }
        else if (k != adev.outbuf_len / adev.bytes_per_frame)
        {
            fprintf(stderr, "Audio write took %d frames rather than %d.\n", k, adev.outbuf_len / adev.bytes_per_frame);

            // Go around again with the rest of it

            psound += k * adev.bytes_per_frame;
            adev.outbuf_len -= k * adev.bytes_per_frame;
        }
        else
        {
            // Success!
            adev.outbuf_len = 0;
            return;
        }
    }

    fprintf(stderr, "Audio write error retry count exceeded.\n");

    adev.outbuf_len = 0;
}

/*
 * Called by modulate
 */
void audio_put(unsigned char c)
{
    adev.outbuf_ptr[adev.outbuf_len++] = c;

    if (adev.outbuf_len == adev.outbuf_size_in_bytes)
    {
        audio_flush();
    }
}

void audio_wait()
{
    audio_flush();
    snd_pcm_drain(adev.audio_out_handle);
}

void audio_close()
{
    if (adev.audio_in_handle != NULL && adev.audio_out_handle != NULL)
    {
        audio_wait();

        snd_pcm_close(adev.audio_in_handle);
        snd_pcm_close(adev.audio_out_handle);

        adev.audio_in_handle = adev.audio_out_handle = NULL;

        free(adev.inbuf_ptr);
        free(adev.outbuf_ptr);

        adev.inbuf_size_in_bytes = 0;
        adev.inbuf_ptr = NULL;
        adev.inbuf_len = 0;
        adev.inbuf_next = 0;

        adev.outbuf_size_in_bytes = 0;
        adev.outbuf_ptr = NULL;
        adev.outbuf_len = 0;
    }
}
