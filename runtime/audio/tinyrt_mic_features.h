#ifndef TINYRT_MIC_FEATURES_H
#define TINYRT_MIC_FEATURES_H
#include <stddef.h>
#define TINYRT_MIC_ABI_ONLY
#include "../../contracts/guest-v1.h"
#undef TINYRT_MIC_ABI_ONLY

/* 16 kHz mono. The caller owns naturally aligned state and calls reset on open. */
#define TINYRT_MIC_RATE 16000
#define TINYRT_MIC_HOP 160
#define TINYRT_MIC_FRAME_MS 10
#define TINYRT_MIC_HISTORY 150
#define TINYRT_MIC_WARMUP_FRAMES 100
#define TINYRT_MIC_RING_FRAMES 100
#define TINYRT_MIC_DEFAULT_GAIN 24
#define TINYRT_MIC_GAIN_SETTLE_MS 200
#define TINYRT_MIC_PLAYBACK_TAIL_MS 150
#define TINYRT_MIC_CLIP 32000
#define TINYRT_MIC_ZCR_GATE 64
#define TINYRT_MIC_DC_POLE 0.995f
#define TINYRT_MIC_MIN_DB (-100.0f)
#define TINYRT_MIC_BANDS_MIN_DB (-80.0f)
#define TINYRT_MIC_PITCH_MIN_DB (-55.0f)
#define TINYRT_MIC_FFT 512
/* Pitch uses 2:1 filtered decimation; the FFT and level path stay at 16 kHz. */
#define TINYRT_MIC_YIN_RATE (TINYRT_MIC_RATE/2)
#define TINYRT_MIC_YIN_MIN (TINYRT_MIC_YIN_RATE/1000)
#define TINYRT_MIC_YIN_MAX (TINYRT_MIC_YIN_RATE/80)
/* Mean square of a -65 dBFS signal in PCM16 units, below the -55 dBFS gate. */
#define TINYRT_MIC_YIN_QUIET_POWER 339.55f
#define TINYRT_MIC_YIN_THRESHOLD 0.15f

typedef struct tinyrt_mic_fx tinyrt_mic_fx_t;
size_t tinyrt_mic_fx_size(void);
void tinyrt_mic_fx_reset(tinyrt_mic_fx_t *fx);
void tinyrt_mic_fx_push(tinyrt_mic_fx_t *fx,const int16_t pcm[TINYRT_MIC_HOP],
                        uint32_t t_ms,uint16_t extra_flags,tinyrt_mic_frame_t *out);
#endif
