/*
 * qpsk-demo.c
 *
 * Debug program for QPSK modem
 *
 * Software Toolworks, February 2023
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

// Includes

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <complex.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <float.h>

#include "qpsk-demo.h"
#include "costas_loop.h"
#include "rrc_fir.h"
#include "deque.h"
#include "constellation.h"
#include "timing_error_detector.h"

// Externals

extern float coeffs[]; // RRC LPF FIR coefficients
extern complex float d_constellation[];
extern deque *d_input;
extern deque *d_decision;

// Prototypes

static void processSymbols(complex float[], unsigned int *);
static complex float qpskModulate(int[]);
static void txBlock(int16_t[], complex float[], int);
static void blockModulate(int16_t[], int[], int);

// BSS Storage

static FILE *fin; // simulated ether
static FILE *fout;

static complex float tx_filter[NTAPS]; // FIR memory
static complex float rx_filter[NTAPS];

static complex float recvBlock[RATE]; // baseband symbols 8x sample buffer

static complex float m_rxPhase; // receive oscillator phase increment
static complex float m_txPhase; // transmit oscillator phase increment

static complex float m_rxRect; // receive oscillator
static complex float m_txRect; // transmit oscillator

static float m_offset_freq; // measured receive phase offset in Hertz
static float m_center;      // passband oscillator center carrier frequency

// Functions

#ifdef OLD
/*
 * Goofy sgn() function
 */
static int sgn(float x)
{
    /*
     * IEEE Floating Point has -0.0
     * so check for +/- 0.0 first
     */
    if (x == 0.0f)
        return 0;
    else
        return (signbit(x) == 0) ? 1 : -1;
}
#endif

/*
 * QPSK Receive function
 *
 * Remove any frequency and timing offsets
 *
 * Process one new 1200 Baud symbol at 8x 9600 rate
 */
static void processSymbols(complex float csamples[], unsigned int *diBits)
{
    for (int i = 0; i < RATE; i++)
    {
        /*
         * Convert 9600 rate complex samples to baseband.
         */
        m_rxPhase *= m_rxRect;

        recvBlock[i] = csamples[i] * m_rxPhase;
    }

    m_rxPhase /= cabsf(m_rxPhase); // normalize oscillator as magnitude can drift

    /*
     * Baseband Root Cosine low-pass Filter 8x new complex samples
     */
    rrc_fir(rx_filter, recvBlock, RATE);

    /*
     * Decimate by 4 for TED calculation (two samples per symbol)
     */
    for (int i = 0; i < RATE; i += 4)
    {
        ted_input(&recvBlock[i]);
    }

    complex float decision = *((complex float *)get(d_input, 1)); // use middle sample

    /*
     * By picking the best sample out of 8 we
     * are also decimating by 8
     */
    complex float costasSymbol = decision * cmplxconj(get_phase());

    /*
     * The constellation gets rotated +45 degrees (rectangular)
     * from what was transmitted (diamond) with costas enabled
     */
#ifdef TEST_SCATTER
    fprintf(stderr, "%f %f\n", crealf(costasSymbol), cimagf(costasSymbol));
#endif

    *diBits = qpsk_decision_maker(costasSymbol);

    float d_error = phase_detector(costasSymbol);

    advance_loop(d_error);
    phase_wrap();
    frequency_limit();

    /*
     * Save the detected frequency error
     */
    m_offset_freq = (get_frequency() / TAU) * RS; // convert radians to freq at symbol rate
    //printf("%.1f ", m_offset_freq);
}

/*
 * Modulate the 32 I + 32 Q PCM 1200 sample rate to 9600 sample
 * rate and translate the baseband to passband, and 1000 Hz
 * carrier center frequency.
 */
static void txBlock(int16_t out_samples[], complex float symbols[], int length)
{
    int outputSize = (length * RATE);
    complex float signal[outputSize];

    /*
     * Use zero-insertion to change the
     * sample rate from 1200 to 9600.
     */
    for (int i = 0; i < length; i++)
    {
        int index = (i * RATE); // compute once

        signal[index] = symbols[i];

        for (int j = 1; j < RATE; j++)
        {
            signal[index + j] = 0.0f; // Gad! That's a lot of zero's!
        }
    }

    /*
     * Raised Root Cosine Filter the 9600 rate baseband
     */
    rrc_fir(tx_filter, signal, outputSize);

    /*
     * Shift Baseband to Center Frequency
     * and convert +/- 1.0 amplitudes to
     * +/- 16k or about 50% PCM level.
     */
    for (int i = 0; i < outputSize; i++)
    {
        m_txPhase *= m_txRect;
        signal[i] = (signal[i] * m_txPhase) * 16384.0f;
    }

    m_txPhase /= cabs(m_txPhase); // normalize as magnitude can drift

    /*
     * Now return the (length * RATE) resulting I + Q PCM samples
     */
    for (int i = 0, j = 0; i < outputSize; i++, j += 2)
    {
        out_samples[j] = (int16_t)(crealf(signal[i]));
        out_samples[j + 1] = (int16_t)(cimagf(signal[i]));
    }
}

/*
 * Gray coded QPSK modulation function
 */
static complex float qpskModulate(int inputBits[])
{
    return d_constellation[(inputBits[0] << 1) | inputBits[1]];
}

/*
 * QPSK modulate the input bits and send the
 * 1200 Baud QPSK I+Q PCM symbols to be upsampled
 * and translated to the carrier frequency at
 * the 9600 sample rate.
 */
static void blockModulate(int16_t out_samples[], int inputBits[], int length)
{
    complex float symbols[length];
    int dibit[2];

    for (int i = 0, s = 0; i < length; i++, s += 2)
    {
        dibit[0] = inputBits[s + 1] & 0x1;
        dibit[1] = inputBits[s] & 0x1;

        symbols[i] = qpskModulate(dibit);
    }

    txBlock(out_samples, symbols, length);
}

// Main Program

int main(int argc, char **argv)
{
    srand(time(0));

    m_center = 1000.0f;
    m_rxPhase = cmplx(0.0f);
    m_rxRect = cmplxconj(TAU * m_center / FS);

    m_txPhase = cmplx(0.0f);
#ifdef FREQ_ERROR
    m_txRect = cmplx(TAU * (m_center + 50.0f) / FS);
    m_offset_freq = (m_center + 50.0f);
#else
    m_txRect = cmplx(TAU * m_center / FS);
    m_offset_freq = m_center;
#endif

    create_constellation_qpsk();
    create_timing_error_detector();

    /*
     * All terms are radians per sample.
     *
     * The loop bandwidth determins the lock range
     * and should be set around TAU/100 to TAU/200
     */
    create_control_loop((TAU / 200.0f), -1.0f, 1.0f);

    /*
     * Create an RRC filter using the
     * Sample Rate, Baud, and Alpha
     */
    rrc_make(FS, RS, .35f);

    /*
     * create the data waveform with 1000 frames
     * of 32 QPSK symbols each.
     */
    fout = fopen(TX_FILENAME, "wb");

    int txBits[32 * 2];                 // 2 bits per symbol
    int16_t txBlock[((32 * 2) * RATE)]; // 32 I + 32 Q PCM output at 9600 rate

    for (int k = 0; k < 1000; k++)
    {
        /*
         * Just use random bits to see spectrum
         *
         * 64 bits or 32 QPSK symbols (2-bits per symbol)
         */
        for (int i = 0; i < (32 * 2); i++)
        {
            txBits[i] = rand() % 2;
        }

        /*
         * create a 32 symbol QPSK 1200 Baud transmit block
         * and return the interpolated txBlock, which will
         * return 32 I + 32 Q modulated by the carrier, and
         * interpolated by 8 samples to the 9600 rate.
         *
         * These are 16 bit signed PCM values.
         */
        blockModulate(txBlock, txBits, 32);

        /*
         * Output 32 I + 32 Q PCM QPSK symbols at 9600 sample rate
         */
        fwrite(txBlock, sizeof(int16_t), ((32 * 2) * RATE), fout);
    }

    fclose(fout);

    /*
     * Now try to process what was transmitted
     */
    fin = fopen(TX_FILENAME, "rb");

    /*
     * process the receive samples
     * They are 8 I + 8 Q PCM samples
     * at 9600 sample rate at a time
     */
    complex float csamples[RATE];
    unsigned int dibitPair;
    int16_t samplesIQ[(RATE * 2)]; // 8 I and 8 Q PCM samples at 8x rate

    while (1)
    {
        /*
         * Read in 8 I + 8 Q samples at 9600 rate
         */
        int count = fread(samplesIQ, sizeof(int16_t), (RATE * 2), fin);

        if (count != (RATE * 2))
            break;

        /*
         * convert 16-bit PCM to complex float values
         */
        for (int i = 0, j = 0; i < RATE; i++, j += 2)
        {
            csamples[i] = CMPLXF((float)samplesIQ[j], (float)samplesIQ[j + 1]) / 16384.0f;
        }

        /*
         * We send one 1200 Baud symbol at a 9600 rate
         * to process and receive decoded bits back.
         */
        processSymbols(csamples, &dibitPair); // we don't do anything with the bits yet
    }

    fclose(fin);

    destroy_timing_error_detector();

    return (EXIT_SUCCESS);
}
