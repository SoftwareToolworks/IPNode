/*
 * il2p_header.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "il2p.h"

static int ascii_to_sixbit(int a)
{
    if (a >= ' ' && a <= '_')
        return (a - ' ');

    return 31; // '?' for any invalid.
}

static int sixbit_to_ascii(int s)
{
    return (s + ' ');
}

static void set_field(unsigned char *hdr, int bit_num, int lsb_index, int width, int value)
{
    while (width > 0 && value != 0)
    {

        if (value & 1)
        {
            hdr[lsb_index] |= 1 << bit_num;
        }

        value >>= 1;
        lsb_index--;
        width--;
    }
}

#define SET_UI(hdr, val) set_field(hdr, 6, 0, 1, val)

#define SET_PID(hdr, val) set_field(hdr, 6, 4, 4, val)

#define SET_CONTROL(hdr, val) set_field(hdr, 6, 11, 7, val)

#define SET_FEC_LEVEL(hdr, val) set_field(hdr, 7, 0, 1, val)

#define SET_HDR_TYPE(hdr, val) set_field(hdr, 7, 1, 1, val)

#define SET_PAYLOAD_BYTE_COUNT(hdr, val) set_field(hdr, 7, 11, 10, val)

// Extracting the fields.

static int get_field(unsigned char *hdr, int bit_num, int lsb_index, int width)
{
    int result = 0;
    lsb_index -= (width - 1);

    while (width > 0)
    {
        result <<= 1;

        if (hdr[lsb_index] & (1 << bit_num))
        {
            result |= 1;
        }

        lsb_index++;
        width--;
    }

    return result;
}

#define GET_UI(hdr) get_field(hdr, 6, 0, 1)

#define GET_PID(hdr) get_field(hdr, 6, 4, 4)

#define GET_CONTROL(hdr) get_field(hdr, 6, 11, 7)

#define GET_PAYLOAD_BYTE_COUNT(hdr) get_field(hdr, 7, 11, 10)

static int encode_pid(packet_t pp)
{
    int pid = ax25_get_pid(pp);

    if ((pid & 0x30) == 0x20)
        return (0x2); // AX.25 Layer 3

    if ((pid & 0x30) == 0x10)
        return (0x2); // AX.25 Layer 3

    if (pid == 0x01)
        return (0x3); // ISO 8208 / CCIT X.25 PLP

    if (pid == 0x06)
        return (0x4); // Compressed TCP/IP

    if (pid == 0x07)
        return (0x5); // Uncompressed TCP/IP

    if (pid == 0x08)
        return (0x6); // Segmentation fragmen

    if (pid == 0xcc)
        return (0xb); // ARPA Internet Protocol

    if (pid == 0xcd)
        return (0xc); // ARPA Address Resolution

    if (pid == 0xce)
        return (0xd); // FlexNet

    if (pid == 0xcf)
        return (0xe); // TheNET

    if (pid == 0xf0)
        return (0xf); // No L3

    return -1;
}

static const unsigned char axpid[16] = {
    0xf0, // Should not happen. 0 is for 'S' frames.
    0xf0, // Should not happen. 1 is for 'U' frames (but not UI).
    0x20, // AX.25 Layer 3
    0x01, // ISO 8208 / CCIT X.25 PLP
    0x06, // Compressed TCP/IP
    0x07, // Uncompressed TCP/IP
    0x08, // Segmentation fragment
    0xf0, // Future
    0xf0, // Future
    0xf0, // Future
    0xf0, // Future
    0xcc, // ARPA Internet Protocol
    0xcd, // ARPA Address Resolution
    0xce, // FlexNet
    0xcf, // TheNET
    0xf0  // No L3
};

// Convert IL2P 4 bit PID to AX.25 8 bit PID.

static int decode_pid(int pid)
{
    return axpid[pid];
}

int il2p_type_1_header(packet_t pp, unsigned char *hdr)
{
    memset(hdr, 0, IL2P_HEADER_SIZE);

    // Destination and source addresses go into low bits 0-5 for bytes 0-11.

    char dst_addr[AX25_MAX_ADDR_LEN];
    char src_addr[AX25_MAX_ADDR_LEN];

    ax25_get_addr_no_ssid(pp, AX25_DESTINATION, dst_addr);
    int dst_ssid = ax25_get_ssid(pp, AX25_DESTINATION);

    ax25_get_addr_no_ssid(pp, AX25_SOURCE, src_addr);
    int src_ssid = ax25_get_ssid(pp, AX25_SOURCE);

    unsigned char *a = (unsigned char *)dst_addr;

    for (int i = 0; *a != '\0'; i++, a++)
    {
        if (*a < ' ' || *a > '_')
        {
            return -1;
        }

        hdr[i] = ascii_to_sixbit(*a);
    }

    a = (unsigned char *)src_addr;

    for (int i = 6; *a != '\0'; i++, a++)
    {
        if (*a < ' ' || *a > '_')
        {
            return -1;
        }

        hdr[i] = ascii_to_sixbit(*a);
    }

    // Byte 12 has DEST SSID in upper nibble and SRC SSID in lower nibble and
    hdr[12] = (dst_ssid << 4) | src_ssid;

    ax25_frame_type_t frame_type;
    cmdres_t cr; // command or response.
    int pf;      // Poll/Final.
    int nr, ns;  // Sequence numbers.

    frame_type = ax25_frame_type(pp, &cr, &pf, &nr, &ns);

    switch (frame_type)
    {

    case frame_type_S_RR:   // Receive Ready - System Ready To Receive
    case frame_type_S_RNR:  // Receive Not Ready - TNC Buffer Full
    case frame_type_S_REJ:  // Reject Frame - Out of Sequence or Duplicate
    case frame_type_S_SREJ: // Selective Reject - Request single frame repeat

        // S frames (RR, RNR, REJ, SREJ), mod 8, have control N(R) P/F S S 0 1
        // These are mapped into    P/F N(R) C S S
        // Bit 6 is not mentioned in documentation but it is used for P/F for the other frame types.
        // C is copied from the C bit in the destination addr.
        // C from source is not used here.  Reception assumes it is the opposite.
        // PID is set to 0, meaning none, for S frames.

        SET_UI(hdr, 0);
        SET_PID(hdr, 0);
        SET_CONTROL(hdr, (pf << 6) | (nr << 3) | (((cr == cr_cmd) | (cr == cr_11)) << 2));

        // This gets OR'ed into the above.
        switch (frame_type)
        {
        case frame_type_S_RR:
            SET_CONTROL(hdr, 0);
            break;
        case frame_type_S_RNR:
            SET_CONTROL(hdr, 1);
            break;
        case frame_type_S_REJ:
            SET_CONTROL(hdr, 2);
            break;
        case frame_type_S_SREJ:
            SET_CONTROL(hdr, 3);
            break;
        default:
            break;
        }

        break;

    case frame_type_U_SABM: // Set Async Balanced Mode
    case frame_type_U_DISC: // Disconnect
    case frame_type_U_DM:   // Disconnect Mode
    case frame_type_U_UA:   // Unnumbered Acknowledge
    case frame_type_U_FRMR: // Frame Reject
    case frame_type_U_UI:   // Unnumbered Information

        // The encoding allows only 3 bits for frame type and SABME got left out.
        // Control format:  P/F opcode[3] C n/a n/a
        // The grayed out n/a bits are observed as 00 in the example.
        // The header UI field must also be set for UI frames.
        // PID is set to 1 for all U frames other than UI.

        if (frame_type == frame_type_U_UI)
        {
            SET_UI(hdr, 1); // I guess this is how we distinguish 'I' and 'UI' on the receving end.
            int pid = encode_pid(pp);

            if (pid < 0)
                return -1;

            SET_PID(hdr, pid);
        }
        else
        {
            SET_PID(hdr, 1); // 1 for 'U' other than 'UI'.
        }

        SET_CONTROL(hdr, (pf << 6) | (((cr == cr_cmd) | (cr == cr_11)) << 2));

        // This gets OR'ed into the above.
        switch (frame_type)
        {
        case frame_type_U_SABM:
            SET_CONTROL(hdr, 0 << 3);
            break;
        case frame_type_U_DISC:
            SET_CONTROL(hdr, 1 << 3);
            break;
        case frame_type_U_DM:
            SET_CONTROL(hdr, 2 << 3);
            break;
        case frame_type_U_UA:
            SET_CONTROL(hdr, 3 << 3);
            break;
        case frame_type_U_FRMR:
            SET_CONTROL(hdr, 4 << 3);
            break;
        case frame_type_U_UI:
            SET_CONTROL(hdr, 5 << 3);
            break;
        default:
            break;
        }
        break;

    case frame_type_I: // Information

        // I frames (mod 8 only)
        // encoded control: P/F N(R) N(S)

        SET_UI(hdr, 0);

        int pid2 = encode_pid(pp);

        if (pid2 < 0)
            return -1;

        SET_PID(hdr, pid2);

        SET_CONTROL(hdr, (pf << 6) | (nr << 3) | ns);
        break;

    case frame_type_U_SABME: // Set Async Balanced Mode, Extended
    case frame_type_U_TEST:  // Test
    case frame_type_U:       // other Unnumbered, not used by AX.25.
    case frame_not_AX25:     // Could not get control byte from frame.
    default:
        return -1;
    }

    // Common for all header type 1.

    // Bit 7 has [FEC Level:1], [HDR Type:1], [Payload byte Count:10]

    SET_FEC_LEVEL(hdr, 1); // Only MAX FEC used
    SET_HDR_TYPE(hdr, 1);  // Only HDR 1 is used

    unsigned char *pinfo;
    int info_len;

    info_len = ax25_get_info(pp, &pinfo);

    if (info_len < 0 || info_len > IL2P_MAX_PAYLOAD_SIZE)
    {
        return -2;
    }

    SET_PAYLOAD_BYTE_COUNT(hdr, info_len);

    return info_len;
}

static void trim(char *stuff)
{
    char *p = stuff + strlen(stuff) - 1;

    while (strlen(stuff) > 0 && (*p == ' '))
    {
        *p = '\0';
        p--;
    }
}

packet_t il2p_decode_header_type_1(unsigned char *hdr, int num_sym_changed)
{
    // First get the addresses including SSID.

    char addrs[AX25_ADDRS][AX25_MAX_ADDR_LEN];

    memset(addrs, 0, 2 * AX25_MAX_ADDR_LEN);

    for (int i = 0; i <= 5; i++)
    {
        addrs[AX25_DESTINATION][i] = sixbit_to_ascii(hdr[i] & 0x3f);
    }

    trim(addrs[AX25_DESTINATION]);

    for (int i = 0; i < strlen(addrs[AX25_DESTINATION]); i++)
    {
        if (!isupper(addrs[AX25_DESTINATION][i]) && !isdigit(addrs[AX25_DESTINATION][i]))
        {
            if (num_sym_changed == 0)
            {
                fprintf(stderr, "IL2P: Invalid character '%c' in destination address '%s'\n", addrs[AX25_DESTINATION][i], addrs[AX25_DESTINATION]);
            }
            return NULL;
        }
    }

    snprintf(addrs[AX25_DESTINATION] + strlen(addrs[AX25_DESTINATION]), 4, "-%d", (hdr[12] >> 4) & 0xf);

    for (int i = 0; i <= 5; i++)
    {
        addrs[AX25_SOURCE][i] = sixbit_to_ascii(hdr[i + 6] & 0x3f);
    }

    trim(addrs[AX25_SOURCE]);

    for (int i = 0; i < strlen(addrs[AX25_SOURCE]); i++)
    {
        if (!isupper(addrs[AX25_SOURCE][i]) && !isdigit(addrs[AX25_SOURCE][i]))
        {
            if (num_sym_changed == 0)
            {
                fprintf(stderr, "IL2P: Invalid character '%c' in source address '%s'\n", addrs[AX25_SOURCE][i], addrs[AX25_SOURCE]);
            }
            return NULL;
        }
    }

    snprintf(addrs[AX25_SOURCE] + strlen(addrs[AX25_SOURCE]), 4, "-%d", hdr[12] & 0xf);

    // The PID field gives us the general type.
    // 0 = 'S' frame.
    // 1 = 'U' frame other than UI.
    // others are either 'UI' or 'I' depending on the UI field.

    int pid = GET_PID(hdr);
    int ui = GET_UI(hdr);

    if (pid == 0)
    {

        // 'S' frame.
        // The control field contains: P/F N(R) C S S

        int control = GET_CONTROL(hdr);
        cmdres_t cr = (control & 0x04) ? cr_cmd : cr_res;
        ax25_frame_type_t ftype;

        switch (control & 0x03)
        {
        case 0:
            ftype = frame_type_S_RR;
            break;
        case 1:
            ftype = frame_type_S_RNR;
            break;
        case 2:
            ftype = frame_type_S_REJ;
            break;
        default:
            ftype = frame_type_S_SREJ;
            break;
        }

        int nr = (control >> 3) & 0x07;
        int pf = (control >> 6) & 0x01;
        unsigned char *pinfo = NULL; // Any info for SREJ will be added later.
        int info_len = 0;

        return ax25_s_frame(addrs, cr, ftype, nr, pf, pinfo, info_len);
    }
    else if (pid == 1)
    {

        // 'U' frame other than 'UI'.
        // The control field contains: P/F OPCODE{3) C x x

        int control = GET_CONTROL(hdr);
        cmdres_t cr = (control & 0x04) ? cr_cmd : cr_res;
        int axpid = 0; // unused for U other than UI.
        ax25_frame_type_t ftype;

        switch ((control >> 3) & 0x7)
        {
        case 0:
            ftype = frame_type_U_SABM;
            break;
        case 1:
            ftype = frame_type_U_DISC;
            break;
        case 2:
            ftype = frame_type_U_DM;
            break;
        case 3:
            ftype = frame_type_U_UA;
            break;
        default:
        case 4:
            ftype = frame_type_U_FRMR;
            break;
        case 5:
            ftype = frame_type_U_UI;
            axpid = 0xf0;
            break; // Should not happen with IL2P pid == 1.
        }

        int pf = (control >> 6) & 0x01;
        unsigned char *pinfo = NULL; // Any info for UI, XID, TEST will be added later.
        int info_len = 0;

        return ax25_u_frame(addrs, cr, ftype, pf, axpid, pinfo, info_len);
    }
    else if (ui)
    {

        // 'UI' frame.
        // The control field contains: P/F OPCODE{3) C x x

        int control = GET_CONTROL(hdr);
        cmdres_t cr = (control & 0x04) ? cr_cmd : cr_res;
        ax25_frame_type_t ftype = frame_type_U_UI;
        int pf = (control >> 6) & 0x01;
        int axpid = decode_pid(GET_PID(hdr));
        unsigned char *pinfo = NULL;
        int info_len = 0;

        return ax25_u_frame(addrs, cr, ftype, pf, axpid, pinfo, info_len);
    }
    else
    {

        // 'I' frame.
        // The control field contains: P/F N(R) N(S)

        int control = GET_CONTROL(hdr);
        cmdres_t cr = cr_cmd; // Always command.
        int pf = (control >> 6) & 0x01;
        int nr = (control >> 3) & 0x7;
        int ns = control & 0x7;
        int axpid = decode_pid(GET_PID(hdr));
        unsigned char *pinfo = NULL;
        int info_len = 0;

        return ax25_i_frame(addrs, cr, nr, ns, pf, axpid, pinfo, info_len);
    }

    return NULL;
}

int il2p_get_header_attributes(unsigned char *hdr)
{
    return GET_PAYLOAD_BYTE_COUNT(hdr);
}

int il2p_clarify_header(unsigned char *rec_hdr, unsigned char *corrected_descrambled_hdr)
{
    unsigned char corrected[IL2P_HEADER_SIZE + IL2P_HEADER_PARITY];

    int e = il2p_decode_rs(rec_hdr, IL2P_HEADER_SIZE, IL2P_HEADER_PARITY, corrected);

    il2p_descramble_block(corrected, corrected_descrambled_hdr, IL2P_HEADER_SIZE);

    return e;
}
