/*
 * Just a dumb kiss versus fftw3 test algorithm
 * Alas, I have the double fftw3 on my Ubuntu box
 *
 * gcc fft-test.c fft.c -o test_fft -lm -lfftw3
 */
#include <stdio.h>
#include <complex.h>
#include <fftw3.h>

#include "fft.h"

int main()
{
    fft_cfg fwd = fft_alloc(256, 0, NULL, NULL);
    fft_cfg inv = fft_alloc(256, 1, NULL, NULL);

    complex float x[256] = { CMPLXF(0.0f, 0.0f) };
    complex float fx[256] = { CMPLXF(0.0f, 0.0f) };

    complex double x3[256] = { CMPLX(0.0, 0.0) };
    complex double fx3[256] = { CMPLX(0.0, 0.0) };

    fftw_plan fwd3 = fftw_plan_dft_1d(256, x3, fx3, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_plan inv3 = fftw_plan_dft_1d(256, fx3, x3, FFTW_BACKWARD, FFTW_ESTIMATE);

    x[0] = CMPLXF(1.0f, 0.0f);
    x[1] = CMPLXF(0.0f, 3.0f);

    fft(fwd, (complex float *)x, (complex float *)fx);

    for (int i = 0; i < 256; i++)
    {
        fx[i] = fx[i] * conjf(fx[i]);
        fx[i] *= 1.0f / 256.0f;
    }

    fft(inv, (complex float *)fx, (complex float *)x);

    printf("KISS  circular correlation of [1+0i, 0+3i, 0+0i, 0+0i ... ] with itself = %.2f%+.2fi, %.2f%+.2fi, %.2f%+.2fi, %.2f%+.2fi\n",
        crealf(x[0]), cimagf(x[0]), crealf(x[1]), cimagf(x[1]), crealf(x[2]), cimagf(x[2]), crealf(x[3]), cimagf(x[3]));

    free(fwd);
    free(inv);

    for (int i = 0; i < 256; i++)
    {
        x3[i] = CMPLX(0.0, 0.0);
        fx3[i] = CMPLX(0.0, 0.0);
    }

    x3[0] = CMPLX(1.0, 0.0);
    x3[1] = CMPLX(0.0, 3.0);

    fftw_execute(fwd3);

    for (int i = 0; i < 256; i++)
    {
        fx3[i] = fx3[i] * conjf(fx3[i]);
        fx3[i] *= 1.0 / 256.0;
    }

    fftw_execute(inv3);

    printf("FFTW3 circular correlation of [1+0i, 0+3i, 0+0i, 0+0i ... ] with itself = %.2lf%+.2lfi, %.2lf%+.2lfi, %.2lf%+.2lfi, %.2lf%+.2lfi\n",
        creal(x3[0]), cimag(x3[0]), creal(x3[1]), cimag(x3[1]), creal(x3[2]), cimag(x3[2]), creal(x3[3]), cimag(x3[3]));

    fftw_destroy_plan(inv3);
    fftw_destroy_plan(fwd3);

    return 0;
}

