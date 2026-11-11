#ifndef TINYRT_MIC_WAV_H
#define TINYRT_MIC_WAV_H
#include <stdint.h>
#include <stdio.h>

/* RIFF PCM16 LE mono at 16000 Hz. File remains open for bounded streaming. */
typedef struct { FILE *file; long start; uint32_t samples; struct mic_wav_meta *meta; } mic_wav_t;
int mic_wav_open(mic_wav_t *wav,const char *path);
int mic_wav_load_meta(mic_wav_t *wav,const char *path);
void mic_wav_close(mic_wav_t *wav);
uint16_t mic_wav_flags(const mic_wav_t *wav,uint32_t begin,uint32_t end);
uint32_t mic_wav_start_ms(const mic_wav_t *wav);
unsigned mic_wav_read(mic_wav_t *wav,int16_t *dst,unsigned count);
#endif
