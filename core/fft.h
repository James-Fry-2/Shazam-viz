#ifndef FP_FFT_H
#define FP_FFT_H

typedef struct fp_fft_plan fp_fft_plan;

fp_fft_plan *fp_fft_create(int n);
void fp_fft_free(fp_fft_plan *p);
void fp_fft_forward(const fp_fft_plan *p, float *re, float *im);

#endif
