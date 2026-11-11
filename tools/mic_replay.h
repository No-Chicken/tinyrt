#ifndef TINYRT_MIC_REPLAY_H
#define TINYRT_MIC_REPLAY_H
#include "tinyrt_mic_features.h"

typedef struct { uint32_t t_ms; uint16_t peak,rms,flags; } mic_level_t;
int mic_replay_file(const char *path);
void mic_replay_shutdown(void);
int32_t mic_replay_open(uint32_t now,uint32_t flags);
int32_t mic_replay_close(void);
int32_t mic_replay_gain(uint32_t now,int32_t db);
int32_t mic_replay_pcm(void *dst,uint32_t bytes);
int32_t mic_replay_features(void *dst,uint32_t bytes);
void mic_replay_playback(uint32_t now,uint32_t duration_ms);
/* Advance until target or one level event, allowing Guest callbacks to close
 * or change capture before processing subsequent samples. */
int mic_replay_advance(uint32_t target,mic_level_t *level);
#endif
