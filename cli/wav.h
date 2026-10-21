#ifndef FP_WAV_H
#define FP_WAV_H

#include <stddef.h>

/* Reads 16-bit PCM or 32-bit float WAV, downmixed to mono in [-1, 1]. Returns 0 on success. */
int wav_read(const char *path, float **samples, size_t *n, int *sample_rate);

/* wav_read, then resample to target_rate if the file's rate differs. */
int wav_read_at_rate(const char *path, int target_rate, float **samples, size_t *n);

/* Writes 16-bit mono PCM, clipping to [-1, 1]. Returns 0 on success. */
int wav_write(const char *path, const float *samples, size_t n, int sample_rate);

#endif
