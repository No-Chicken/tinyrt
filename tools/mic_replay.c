#include "mic_replay.h"
#include "mic_wav.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    unsigned capacity, read, used;
} fifo_t;

static mic_wav_t wav;
static struct {
    fifo_t pcm,features;
    tinyrt_mic_fx_t *fx;
    uint32_t flags,remaining,level_ms;
    uint64_t next_ms,settle_until;
    float gain;
    int32_t gain_db;
} mic;
static uint64_t playback_end;

static int reserve(fifo_t *fifo,unsigned capacity)
{
    fifo->bytes=malloc(capacity); fifo->capacity=capacity;
    return fifo->bytes!=NULL;
}

static void copy_ring(fifo_t *fifo,unsigned at,void *data,unsigned bytes,int write)
{
    unsigned first=fifo->capacity-at;
    if(first>bytes) first=bytes;
    if(write) {
        memcpy(fifo->bytes+at,data,first);
        memcpy(fifo->bytes,(uint8_t *)data+first,bytes-first);
    } else {
        memcpy(data,fifo->bytes+at,first);
        memcpy((uint8_t *)data+first,fifo->bytes,bytes-first);
    }
}

static int push(fifo_t *fifo,void *src,unsigned bytes)
{
    unsigned end=(fifo->read+fifo->used)%fifo->capacity;
    unsigned space=fifo->capacity-fifo->used;
    if(bytes>space) {
        fifo->read=(fifo->read+bytes-space)%fifo->capacity;
        fifo->used=fifo->capacity;
    } else fifo->used+=bytes;
    copy_ring(fifo,end,src,bytes,1);
    return bytes>space;
}

static int32_t pop(fifo_t *fifo,void *dst,unsigned bytes)
{
    if(bytes>fifo->used) bytes=fifo->used;
    if(!bytes) return 0;
    copy_ring(fifo,fifo->read,dst,bytes,0);
    fifo->read=(fifo->read+bytes)%fifo->capacity;
    fifo->used-=bytes;
    return (int32_t)bytes;
}

int mic_replay_file(const char *path)
{
    if(wav.file) return 0;
    return mic_wav_open(&wav,path);
}

int32_t mic_replay_close(void)
{
    free(mic.pcm.bytes); free(mic.features.bytes); free(mic.fx);
    memset(&mic,0,sizeof(mic));
    return 0;
}

void mic_replay_shutdown(void)
{
    mic_replay_close();
    if(wav.file) fclose(wav.file);
    memset(&wav,0,sizeof(wav)); playback_end=0;
}

int32_t mic_replay_open(uint32_t now,uint32_t flags)
{
    if(!wav.file) return -2;
    if(mic.flags) return 1;
    if((flags&TINYRT_MIC_PCM) && !reserve(&mic.pcm,TINYRT_MIC_RATE*2)) goto unavailable;
    if(flags&TINYRT_MIC_FEATURES) {
        mic.fx=malloc(tinyrt_mic_fx_size());
        if(!mic.fx || !reserve(&mic.features,TINYRT_MIC_RING_FRAMES*sizeof(tinyrt_mic_frame_t)))
            goto unavailable;
        tinyrt_mic_fx_reset(mic.fx);
    }
    uint64_t offset=(uint64_t)now*(TINYRT_MIC_RATE/1000);
    if(offset>wav.samples) offset=wav.samples;
    if(fseek(wav.file,wav.start+(long)(offset*2),SEEK_SET)) goto unavailable;
    mic.remaining=wav.samples-(uint32_t)offset;
    mic.flags=flags; mic.gain_db=TINYRT_MIC_DEFAULT_GAIN; mic.gain=1;
    mic.next_ms=(uint64_t)now+TINYRT_MIC_FRAME_MS;
    return 0;
unavailable:
    mic_replay_close();
    return -2;
}

int32_t mic_replay_gain(uint32_t now,int32_t db)
{
    if(!mic.flags) return -2;
    if(db!=mic.gain_db) {
        mic.gain_db=db;
        mic.gain=powf(10.0f,(db-TINYRT_MIC_DEFAULT_GAIN)/20.0f);
        mic.settle_until=(uint64_t)now+TINYRT_MIC_GAIN_SETTLE_MS;
    }
    return db;
}

int32_t mic_replay_pcm(void *dst,uint32_t bytes) { return pop(&mic.pcm,dst,bytes); }
int32_t mic_replay_features(void *dst,uint32_t bytes) { return pop(&mic.features,dst,bytes); }
void mic_replay_playback(uint32_t now,uint32_t duration_ms)
{
    uint64_t end=(uint64_t)now+duration_ms+TINYRT_MIC_PLAYBACK_TAIL_MS;
    if(end>playback_end) playback_end=end;
}

int mic_replay_advance(uint32_t target,mic_level_t *level)
{
    while(mic.flags && mic.remaining && mic.next_ms<=target) {
        int16_t block[TINYRT_MIC_HOP];
        unsigned n=mic.remaining<TINYRT_MIC_HOP?mic.remaining:TINYRT_MIC_HOP;
        if(mic_wav_read(&wav,block,n)!=n) return -1;
        mic.remaining-=n;
        uint32_t t_ms=(uint32_t)mic.next_ms;
        uint16_t flags=0;
        if(mic.next_ms-TINYRT_MIC_FRAME_MS<playback_end) flags|=TINYRT_MICF_PLAYBACK;
        if(mic.next_ms<=mic.settle_until) flags|=TINYRT_MICF_SETTLING;
        mic.next_ms+=TINYRT_MIC_FRAME_MS;
        uint32_t peak=0;
        float squares=0;
        for(unsigned i=0;i<n;i++) {
            if(mic.gain!=1)
                block[i]=(int16_t)lroundf(fmaxf(-32768,fminf(32767,block[i]*mic.gain)));
            unsigned mag=(unsigned)abs((int)block[i]);
            if(mag>=TINYRT_MIC_CLIP) flags|=TINYRT_MICF_CLIPPED;
            if(mag>peak) peak=mag;
            squares+=(float)mag*mag;
        }
        if(mic.pcm.bytes && push(&mic.pcm,block,n*2)) flags|=TINYRT_MICF_OVERRUN;
        if(n!=TINYRT_MIC_HOP) continue;
        if(mic.fx) {
            tinyrt_mic_frame_t f;
            if(mic.features.used==mic.features.capacity) flags|=TINYRT_MICF_OVERRUN;
            tinyrt_mic_fx_push(mic.fx,block,t_ms,flags,&f);
            push(&mic.features,&f,sizeof(f));
            flags=f.flags;
        }
        if(mic.flags&TINYRT_MIC_LEVEL) {
            mic.level_ms+=TINYRT_MIC_FRAME_MS;
            if(mic.level_ms>=33) {
                mic.level_ms-=33;
                if(level) {
                    level->t_ms=t_ms; level->flags=flags;
                    level->peak=(uint16_t)(peak>32767?32767:peak);
                    level->rms=(uint16_t)lroundf(fminf(32767,sqrtf(squares/n)));
                    return 1;
                }
            }
        }
    }
    return 0;
}
