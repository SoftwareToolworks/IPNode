/*
 * ptt.c
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
#include <string.h>
#include <time.h>
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>
#include <dirent.h>
#include <bsd/bsd.h>

#include "ipnode.h"
#include "audio.h"
#include "ptt.h"
#include "dlq.h"

#define INVALID_HANDLE_VALUE (-1)

#define MAX_GROUPS 50

static struct audio_s *save_audio_config_p; /* Save config information for later use. */

static void get_access_to_gpio(const char *path)
{
    static int my_uid = -1;
    static int my_gid = -1;
    static gid_t my_groups[MAX_GROUPS];
    static int num_groups = 0;
    static int first_time = 1;

    struct stat finfo;
    int i;
    char cmd[80];
    int err;

    if (stat(path, &finfo) < 0)
    {
        fprintf(stderr, "Fatal: %s(): Can't get properties of %s\n", __func__, path);
        exit(1);
    }

    if (first_time)
    {
        my_uid = geteuid();
        my_gid = getegid();
        num_groups = getgroups(MAX_GROUPS, my_groups);

        if (num_groups < 0)
        {
            fprintf(stderr, "%s(): getgroups() failed to get supplementary groups, errno=%d\n", __func__, errno);
            num_groups = 0;
        }

        first_time = 0;
    }

    /*
     * Do we have permission to access it?
     *
     * On Debian 7 (Wheezy) we see this:
     *
     *	$ ls -l /sys/class/gpio/export
     *	--w------- 1 root root 4096 Feb 27 12:31 /sys/class/gpio/export
     *
     *
     * Only root can write to it.
     * Our work-around is change the protection so that everyone can write.
     * This requires that the current user can use sudo without a password.
     * This has been the case for the predefined "pi" user but can be a problem
     * when people add new user names.
     * Other operating systems could have different default configurations.
     *
     * A better solution is available in Debian 8 (Jessie).  The group is now "gpio"
     * so anyone in that group can now write to it.
     *
     *	$ ls -l /sys/class/gpio/export
     *	-rwxrwx--- 1 root gpio 4096 Mar  4 21:12 /sys/class/gpio/export
     *
     *
     * First see if we can access it by the usual file protection rules.
     * If not, we will try the "sudo chmod go+rw ..." hack.
     *
     */

    if ((my_uid == finfo.st_uid) && (finfo.st_mode & S_IWUSR))
    { // user write 00200
        return;
    }

    if ((my_gid == finfo.st_gid) && (finfo.st_mode & S_IWGRP))
    { // group write 00020
        return;
    }

    for (i = 0; i < num_groups; i++)
    {
        if ((my_groups[i] == finfo.st_gid) && (finfo.st_mode & S_IWGRP))
        { // group write 00020
            return;
        }
    }

    if (finfo.st_mode & S_IWOTH)
    { // other write 00002
        return;
    }

    /*
     * We don't have permission.
     * Try a hack which requires that the user be set up to use sudo without a password.
     */

    snprintf(cmd, sizeof(cmd), "sudo chmod go+rw %s", path);

    err = system(cmd);

    (void)err; // suppress warning about not using result.

    /*
     * I don't trust status coming back from system() so we will check the mode again.
     */

    if (stat(path, &finfo) < 0)
    {
        /* Unexpected because we could do it before. */

        fprintf(stderr, "This system is not configured with the GPIO user interface.\n");
        exit(1);
    }

    /* Did we succeed in changing the protection? */

    if ((finfo.st_mode & 0266) != 0266)
    {
        fprintf(stderr, "You don't have the necessary permission to access GPIO.\n");
        fprintf(stderr, "There are three different solutions: \n");
        fprintf(stderr, " 1. Run as root. (not recommended)\n");
        fprintf(stderr, " 2. If operating system has 'gpio' group, add your user id to it.\n");
        fprintf(stderr, " 3. Configure your user id for sudo without a password.\n\n");
        exit(1);
    }
}

void export_gpio(int ot, int invert, int direction)
{
    const char gpio_export_path[] = "/sys/class/gpio/export";
    char gpio_direction_path[80];
    char gpio_value_path[80];
    char stemp[16];
    int gpio_num;
    char *gpio_name;

    if (direction)
    {
        gpio_num = save_audio_config_p->octrl[ot].out_gpio_num;
        gpio_name = save_audio_config_p->octrl[ot].out_gpio_name;
    }
    else
    {
        gpio_num = save_audio_config_p->ictrl[ot].in_gpio_num;
        gpio_name = save_audio_config_p->ictrl[ot].in_gpio_name;
    }

    get_access_to_gpio(gpio_export_path);

    int fd = open(gpio_export_path, O_WRONLY);

    if (fd < 0)
    {
        fprintf(stderr, "Permissions do not allow access to GPIO.\n");
        exit(1);
    }

    snprintf(stemp, sizeof(stemp), "%d", gpio_num);

    if (write(fd, stemp, strlen(stemp)) != strlen(stemp))
    {
        int e = errno;

        if (e != EBUSY)
        {
            fprintf(stderr, "Fatal: Error writing \"%s\" to %s, errno=%d\n%s\n", stemp, gpio_export_path, e, strerror(e));
            exit(1);
        }
    }

    SLEEP_MS(250);
    close(fd);

    struct dirent **file_list;
    int i;
    int ok = 0;

    int num_files = scandir("/sys/class/gpio", &file_list, NULL, alphasort);

    if (num_files < 0)
    {
        fprintf(stderr, "ERROR! Could not get directory listing for /sys/class/gpio\n");

        snprintf(gpio_name, MAX_GPIO_NAME_LEN, "gpio%d", gpio_num);
        num_files = 0;
        ok = 1;
    }
    else
    {

        // Look for exact name gpioNN

        char lookfor[16];
        snprintf(lookfor, sizeof(lookfor), "gpio%d", gpio_num);

        for (i = 0; i < num_files && !ok; i++)
        {
            if (strcmp(lookfor, file_list[i]->d_name) == 0)
            {
                strlcpy(gpio_name, file_list[i]->d_name, MAX_GPIO_NAME_LEN);
                ok = 1;
            }
        }

        // If not found, Look for gpioNN_*

        snprintf(lookfor, sizeof(lookfor), "gpio%d_", gpio_num);

        for (i = 0; i < num_files && !ok; i++)
        {
            if (strncmp(lookfor, file_list[i]->d_name, strlen(lookfor)) == 0)
            {
                strlcpy(gpio_name, file_list[i]->d_name, MAX_GPIO_NAME_LEN);
                ok = 1;
            }
        }

        // Free the storage allocated by scandir().

        for (i = 0; i < num_files; i++)
        {
            free(file_list[i]);
        }

        free(file_list);
    }

    if (ok == 0)
    {
        fprintf(stderr, "Fatal: Could not find Path for gpio number %d.n", gpio_num);
        exit(1);
    }

    snprintf(gpio_direction_path, sizeof(gpio_direction_path), "/sys/class/gpio/%s/direction", gpio_name);
    get_access_to_gpio(gpio_direction_path);

    fd = open(gpio_direction_path, O_WRONLY);

    if (fd < 0)
    {
        int e = errno;

        fprintf(stderr, "Error opening %s\n", stemp);
        fprintf(stderr, "%s\n", strerror(e));
        exit(1);
    }

    char gpio_val[8];

    if (direction)
    {
        if (invert)
        {
            strlcpy(gpio_val, "high", sizeof(gpio_val));
        }
        else
        {
            strlcpy(gpio_val, "low", sizeof(gpio_val));
        }
    }
    else
    {
        strlcpy(gpio_val, "in", sizeof(gpio_val));
    }

    if (write(fd, gpio_val, strlen(gpio_val)) != strlen(gpio_val))
    {
        int e = errno;

        fprintf(stderr, "Fatal: Error writing initial state to %s\n%s\n", stemp, strerror(e));
        exit(1);
    }

    close(fd);

    snprintf(gpio_value_path, sizeof(gpio_value_path), "/sys/class/gpio/%s/value", gpio_name);
    get_access_to_gpio(gpio_value_path);
}

static int ptt_fd[NUM_OCTYPES];
static char otnames[NUM_OCTYPES][8];

void ptt_init(struct audio_s *audio_config_p)
{
    save_audio_config_p = audio_config_p;

    strlcpy(otnames[OCTYPE_PTT], "PTT", sizeof(otnames[OCTYPE_PTT]));
    strlcpy(otnames[OCTYPE_DCD], "DCD", sizeof(otnames[OCTYPE_DCD]));
    strlcpy(otnames[OCTYPE_CON], "CON", sizeof(otnames[OCTYPE_CON]));
    strlcpy(otnames[OCTYPE_CON], "SYN", sizeof(otnames[OCTYPE_SYN]));

    for (int ot = 0; ot < NUM_OCTYPES; ot++)
    {
        ptt_fd[ot] = INVALID_HANDLE_VALUE;
    }

    int using_gpio = 0;

    for (int ot = 0; ot < NUM_OCTYPES; ot++)
    {
        using_gpio = 1;
    }

    for (int ot = 0; ot < NUM_ICTYPES; ot++)
    {
        using_gpio = 1;
    }

    if (using_gpio)
    {
        get_access_to_gpio("/sys/class/gpio/export");
    }

    for (int ot = 0; ot < NUM_OCTYPES; ot++)
    { // output control type, PTT, DCD, CON, SYN ...
        export_gpio(ot, audio_config_p->octrl[ot].ptt_invert, 1);
    }

    for (int it = 0; it < NUM_ICTYPES; it++)
    { // input control type
        export_gpio(it, audio_config_p->ictrl[it].inh_invert, 0);
    }
}

void ptt_set(int ot, int ptt_signal)
{
#ifdef DEBUG_TX
    int ptt = ptt_signal;

    dlq_channel_busy(ot, ptt_signal);

    if (save_audio_config_p->octrl[ot].ptt_invert)
    {
        ptt = !ptt;
    }

    char gpio_value_path[80];
    char stemp[16];

    snprintf(gpio_value_path, sizeof(gpio_value_path), "/sys/class/gpio/%s/value", save_audio_config_p->octrl[ot].out_gpio_name);

    int fd = open(gpio_value_path, O_WRONLY);

    if (fd < 0)
    {
        int e = errno;

        fprintf(stderr, "Fatal: Error opening %s to set %s signal.\n%s\n", stemp, otnames[ot], strerror(e));
        return;
    }

    snprintf(stemp, sizeof(stemp), "%d", ptt);

    if (write(fd, stemp, 1) != 1)
    {
        int e = errno;

        fprintf(stderr, "Fatal: Error setting GPIO %d for %s\n%s\n",
                save_audio_config_p->octrl[ot].out_gpio_num, otnames[ot], strerror(e));
    }

    close(fd);
#endif
}

int get_input(int it)
{
    char gpio_value_path[80];

    snprintf(gpio_value_path, sizeof(gpio_value_path), "/sys/class/gpio/%s/value", save_audio_config_p->ictrl[it].in_gpio_name);

    get_access_to_gpio(gpio_value_path);

    int fd = open(gpio_value_path, O_RDONLY);

    if (fd < 0)
    {
        int e = errno;

        fprintf(stderr, "Error opening %s to check input.\n", gpio_value_path);
        fprintf(stderr, "%s\n", strerror(e));
        return -1;
    }

    char vtemp[2];

    if (read(fd, vtemp, 1) != 1)
    {
        int e = errno;

        fprintf(stderr, "Error getting GPIO %d value\n", save_audio_config_p->ictrl[it].in_gpio_num);
        fprintf(stderr, "%s\n", strerror(e));
    }

    close(fd);

    vtemp[1] = '\0';

    if (atoi(vtemp) != save_audio_config_p->ictrl[it].inh_invert)
    {
        return 1;
    }
    else
    {
        return 0;
    }

    return -1;
}

void ptt_term()
{
#ifdef DEBUG_TX
    for (int ot = 0; ot < NUM_OCTYPES; ot++)
    {
        ptt_set(ot, 0);
    }

    for (int ot = 0; ot < NUM_OCTYPES; ot++)
    {
        if (ptt_fd[ot] != INVALID_HANDLE_VALUE)
        {
            close(ptt_fd[ot]);
            ptt_fd[ot] = INVALID_HANDLE_VALUE;
        }
    }
#endif
}
