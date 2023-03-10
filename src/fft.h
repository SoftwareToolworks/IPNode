/*
 * Copyright (c) 2003-2004, Mark Borgerding
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this list
 * of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * Neither the author nor the names of any contributors may be used to endorse or
 * promote products derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Modified March 2020 by Software Toolworks
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI    3.14159265358979
#endif

#ifndef TAU
#define TAU     (2.0 * M_PI)
#endif

#define cmplx(value) (cosf(value) + sinf(value) * I)
#define cmplxconj(value) (cosf(value) + sinf(value) * -I)

/* Complex FFT */

struct fft_state {
    int nfft;
    int inverse;
    int factors[64];
    complex float twiddles[1];
};

typedef struct fft_state *fft_cfg;

/* Real FFT */

struct fftr_state {
    fft_cfg substate;
    complex float *tmpbuf;
    complex float *super_twiddles;
};

typedef struct fftr_state *fftr_cfg;

/* Complex Function Calls */

fft_cfg fft_alloc(int, int, void *, size_t *);
void fft(fft_cfg, const complex float *, complex float *);

/* Real Function Calls */

fftr_cfg fftr_alloc(int, int, void *, size_t *);
void fftr(fftr_cfg, const float *, complex float *);
void fftri(fftr_cfg, const complex float *, float *);

#ifdef __cplusplus
}
#endif