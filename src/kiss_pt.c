/*
 * kiss_pt.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <bsd/bsd.h>

#include "ipnode.h"
#include "tq.h"
#include "ax25_pad.h"
#include "kiss_pt.h"
#include "kiss_frame.h"
#include "tx.h"

#define TMP_KISSTNC_SYMLINK "/tmp/kisstnc"

static kiss_frame_t kf;

static int pt_master_fd = -1;  /* File descriptor for my end. */
static char pt_slave_name[32]; /* Pseudo terminal slave name  */

static void *kisspt_listen_thread(void *arg);
static int kisspt_open_pt();

void kisspt_init()
{
    pthread_t kiss_pterm_listen_tid;

    memset(&kf, 0, sizeof(kf));

    pt_master_fd = kisspt_open_pt();

    if (pt_master_fd != -1)
    {
        int e = pthread_create(&kiss_pterm_listen_tid, (pthread_attr_t *)NULL, kisspt_listen_thread, NULL);

        if (e != 0)
        {
            printf("Fatal: kisspt_init: Could not create kiss listening thread for pseudo terminal\n");
            exit(1);
        }
    }
}

static int kisspt_open_pt()
{
    int fd = posix_openpt(O_RDWR | O_NOCTTY);

    if (fd == -1)
    {
        fprintf(stderr, "Fatal: Could not create pseudo terminal master\n");
        return -1;
    }

    char *pts = ptsname(fd);
    int grant = grantpt(fd);
    int unlock = unlockpt(fd);

    if ((grant == -1) || (unlock == -1) || (pts == NULL))
    {
        fprintf(stderr, "Fatal: Could not create pseudo terminal\n");
        return -1;
    }

    strlcpy(pt_slave_name, pts, sizeof(pt_slave_name));

    struct termios ts;
    int e = tcgetattr(fd, &ts);

    if (e != 0)
    {
        fprintf(stderr, "Fatal: pt tcgetattr Can't get pseudo terminal attributes, err=%d\n", e);
        return -1;
    }

    cfmakeraw(&ts);

    ts.c_cc[VMIN] = 1;  /* wait for at least one character */
    ts.c_cc[VTIME] = 0; /* no fancy timing. */

    e = tcsetattr(fd, TCSANOW, &ts);

    if (e != 0)
    {
        fprintf(stderr, "Fatal: pt tcsetattr Can't set pseudo terminal attributes, err=%d\n", e);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    e = fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    if (e != 0)
    {
        fprintf(stderr, "Fatal: Can't set pseudo terminal to nonblocking\n");
        return -1;
    }

    int pt_slave_fd = open(pt_slave_name, O_RDWR | O_NOCTTY);

    if (pt_slave_fd < 0)
    {
        fprintf(stderr, "Fatal: Can't open pseudo terminal slave %s\n", pt_slave_name);
        return -1;
    }

    unlink(TMP_KISSTNC_SYMLINK);

    if (symlink(pt_slave_name, TMP_KISSTNC_SYMLINK) == 0)
    {
        printf("Created symlink %s -> %s\n", TMP_KISSTNC_SYMLINK, pt_slave_name);
    }
    else
    {
        fprintf(stderr, "Fatal: Failed to create kiss symlink %s\n", TMP_KISSTNC_SYMLINK);
        return -1;
    }

    printf("Virtual KISS TNC is available on %s\n", pt_slave_name);

    return fd;
}

void kisspt_send_rec_packet(int kiss_cmd, unsigned char *fbuf, int flen)
{
    unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN + 2];
    int kiss_len;

    if (pt_master_fd == -1)
    {
        return;
    }

    if (flen < 0)
    {
        flen = strlen((char *)fbuf);

        strlcpy((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
        kiss_len = strlen((char *)kiss_buff);
    }
    else
    {
        unsigned char stemp[AX25_MAX_PACKET_LEN + 1];

        if (flen > (int)(sizeof(stemp)) - 1)
        {
            fprintf(stderr, "Warning: Pseudo Terminal KISS buffer too small.  Truncated.\n");
            flen = (int)(sizeof(stemp)) - 1;
        }

        stemp[0] = kiss_cmd & 0x0F;
        memcpy(stemp + 1, fbuf, flen);

        kiss_len = kiss_encapsulate(stemp, flen + 1, kiss_buff);
    }

    int err = write(pt_master_fd, kiss_buff, (size_t)kiss_len);

    if (err == -1 && errno == EWOULDBLOCK)
    {
        fprintf(stderr, "Warning: Discarding KISS Send message because no listener\n");
    }
    else if (err != kiss_len)
    {
        fprintf(stderr, "Warning: KISS pseudo terminal write error: fd=%d, len=%d, write returned %d, errno = %d\n",
                pt_master_fd, kiss_len, err, errno);
    }
}

static int kisspt_get()
{
    unsigned char chr;

    int n = 0;
    fd_set fd_in;
    fd_set fd_ex;

    while (n == 0)
    {
        FD_ZERO(&fd_in);
        FD_SET(pt_master_fd, &fd_in);

        FD_ZERO(&fd_ex);
        FD_SET(pt_master_fd, &fd_ex);

        int rc = select(pt_master_fd + 1, &fd_in, NULL, &fd_ex, NULL);

        if (rc == 0)
        {
            continue;
        }

        if (rc == -1 || (n = read(pt_master_fd, &chr, (size_t)1)) != 1)
        {
            fprintf(stderr, "Fatal: pt read Error receiving KISS message from pseudo terminal.  Closing %s\n", pt_slave_name);

            close(pt_master_fd);

            pt_master_fd = -1;
            unlink(TMP_KISSTNC_SYMLINK);
            pthread_exit(NULL);
            n = -1;
        }
    }

    return chr;
}

static void *kisspt_listen_thread(void *arg)
{
    while (1)
    {
        unsigned char chr = kisspt_get();

        kiss_rec_byte(&kf, chr, -1, kisspt_send_rec_packet);
    }

    return (void *)0;
}
