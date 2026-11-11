#ifndef TINYRT_MIC_WAV_H
#define TINYRT_MIC_WAV_H
#include <stdint.h>
#include <stdio.h>

/* RIFF PCM16 LE mono at 16000 Hz. File remains open for bounded streaming. */
typedef struct { FILE *file; long start; uint32_t samples; } mic_wav_t;
int mic_wav_open(mic_wav_t *wav,const char *path);
unsigned mic_wav_read(mic_wav_t *wav,int16_t *dst,unsigned count);
#endif
