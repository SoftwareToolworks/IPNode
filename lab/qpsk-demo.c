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

#include "qpsk-demo.h"
#include "interp.h"
#include "costas_loop.h"
#include "rrc_fir.h"

// Externals

extern float coeffs[]; // RRC LPF FIR coefficients

// Prototypes

static void qpskDemodulate(complex float, int[]);
static void processSymbols(complex float[], int[]);
static complex float qpskModulate(int[]);
static void txBlock(int16_t[], complex float[], int);
static void blockModulate(int16_t[], int[], int);

// Globals

static FILE *fin; // simulated ether
static FILE *fout;

static complex float tx_filter[NTAPS]; // FIR memory
static complex float rx_filter[NTAPS];

static complex float *recvBlock; // receive symbol buffer

static complex float m_rxPhase; // receive oscillator phase increment
static complex float m_txPhase; // transmit oscillator phase increment

static complex float m_rxRect; // receive oscillator
static complex float m_txRect; // transmit oscillator

static float m_offset_freq; // measured receive phase offset in Hertz
static float m_center;      // passband oscillator center carrier frequency

/*
 * QPSK Quadrant bit-pair values - Gray Coded
 */
static const complex float constellation[] = {
    1.0f + 0.0f * I, //  I
    0.0f + 1.0f * I, //  Q
    0.0f - 1.0f * I, // -Q
    -1.0f + 0.0f * I // -I
};

/*
 * Gray coded QPSK demodulation function
 */
static void qpskDemodulate(complex float symbol, int outputBits[])
{
    outputBits[0] = crealf(symbol) < 0.0f; // I < 0 ?
    outputBits[1] = cimagf(symbol) < 0.0f; // Q < 0 ?
}

/*
 * QPSK Receive function
 *
 * Remove any frequency and timing offsets
 *
 * Process one 1200 Baud symbol at 9600 rate
 */
static void processSymbols(complex float csamples[], int diBits[])
{
    /*
     * Convert 9600 rate complex samples to baseband.
     */
    for (int i = 0; i < RATE; i++)
    {
        int extended = (RATE + i); // compute once
        m_rxPhase *= m_rxRect;

        recvBlock[i] = recvBlock[extended];
        recvBlock[extended] = csamples[i] * m_rxPhase;
    }

    m_rxPhase /= cabsf(m_rxPhase); // normalize oscillator as magnitude can drift

    /*
     * Baseband Root Cosine low-pass Filter
     * Using the previous sample block.
     *
     * We're still at 9600 sample rate
     */
    rrc_fir(rx_filter, recvBlock, RATE);

    /*
     * Now decimate the 9600 rate sample to the 1200 rate.
     */
    complex float decimatedSymbol = recvBlock[0];

    /*
     * Choose the highest amplitude histogram as index
     */
    if (get_costas_enable() == true)
    {
        complex float costasSymbol = decimatedSymbol * cmplxconj(get_phase());

        /*
         * The constellation gets rotated +45 degrees (rectangular)
         * from what was transmitted (diamond) with costas enabled
         */
#ifdef TEST_SCATTER
        fprintf(stderr, "%f %f\n", crealf(costasSymbol), cimagf(costasSymbol));
#endif

        float d_error = phase_detector(costasSymbol);

        advance_loop(d_error);
        phase_wrap();
        frequency_limit();

        qpskDemodulate(costasSymbol, diBits);

        // printf("%d%d ", diBits[0], diBits[1]);
    }
    else
    {
        /*
         * Rotate constellation from diamond to rectangular.
         * This makes rasier quadrant detection possible.
         */
        complex float decodedSymbol = decimatedSymbol * cmplxconj(ROTATE45);

#ifdef TEST_SCATTER
        fprintf(stderr, "%f %f\n", crealf(decodedSymbol), cimagf(decodedSymbol));
#endif
        qpskDemodulate(decodedSymbol, diBits);

        // printf("%d%d ", diBits[0], diBits[1]);
    }

    /*
     * Save the detected frequency error
     */
    m_offset_freq = (get_frequency() * m_center / TAU); // convert radians to freq at symbol rate
    // printf("%.1f ", m_offset_freq);
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
    return constellation[(inputBits[1] << 1) | inputBits[0]];
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
    m_txRect = cmplx(TAU * m_center / FS);

    m_offset_freq = m_center;

    /*
     * 32 Symbols at 8x sample rate, twice as big as needed to buffer
     */
    recvBlock = (complex float *)calloc(((RATE * 32) * 2), sizeof(complex float));

    /*
     * All terms are radians per sample.
     *
     * The loop bandwidth determins the lock range
     * and should be set around TAU/100 to TAU/200
     */
    create_control_loop((TAU / 180.0f), -1.0f, 1.0f);

    /*
     * Shut off the costas loop
     * for a validity test
     */
    // set_costas_enable(false);

    /*
     * Create an RRC filter using the
     * Sample Rate, Baud, and Alpha
     */
    rrc_make(FS, RS, .35f);

    /*
     * Create Timing Error Detector (TED)
     */
    create_QPSKDemodulator(.3f);  // sample counter gain = .3

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
    int diBits[2];
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
        processSymbols(csamples, diBits); // we don't do anything with the bits yet
    }

    free(recvBlock);
    destroy_interpolatingSampleBuffer();

    fclose(fin);

    return (EXIT_SUCCESS);
}
