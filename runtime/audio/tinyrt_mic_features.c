#include "tinyrt_mic_features.h"
#include <math.h>
#include <string.h>

#define N TINYRT_MIC_FFT
#define PI 3.14159265358979323846f
typedef char mic_frame_must_be_32_bytes[sizeof(tinyrt_mic_frame_t)==32 ? 1 : -1];

struct tinyrt_mic_fx {
    float window[N], hann[N], re[N], im[N], cosine[N/2], sine[N/2];
    float yin[TINYRT_MIC_YIN_MAX+1], previous_in, previous_out;
    int16_t history[TINYRT_MIC_HISTORY], sorted[TINYRT_MIC_HISTORY];
    unsigned history_count, history_pos, warmup;
    uint32_t seq;
    int sign;
};

size_t tinyrt_mic_fx_size(void) { return sizeof(tinyrt_mic_fx_t); }

void tinyrt_mic_fx_reset(tinyrt_mic_fx_t *fx)
{
    memset(fx,0,sizeof(*fx));
    for(unsigned i=0;i<N;i++) fx->hann[i]=0.5f-0.5f*cosf(2*PI*i/(N-1));
    for(unsigned i=0;i<N/2;i++) {
        fx->cosine[i]=cosf(2*PI*i/N);
        fx->sine[i]=-sinf(2*PI*i/N);
    }
}

/* In-place radix-2 FFT; stores the windowed input in bit-reversed order. */
static void spectrum(tinyrt_mic_fx_t *fx,tinyrt_mic_frame_t *out)
{
    unsigned reversed=0;
    for(unsigned i=0;i<N;i++) {
        fx->re[reversed]=fx->window[i]*fx->hann[i];
        fx->im[reversed]=0;
        unsigned bit=N/2;
        while(bit && (reversed&bit)) { reversed^=bit; bit>>=1; }
        reversed^=bit;
    }
    for(unsigned size=2;size<=N;size*=2) {
        unsigned half=size/2;
        for(unsigned base=0;base<N;base+=size) {
            for(unsigned k=0;k<half;k++) {
                unsigned a=base+k,b=a+half,tw=k*(N/size);
                float re=fx->re[b]*fx->cosine[tw]-fx->im[b]*fx->sine[tw];
                float im=fx->re[b]*fx->sine[tw]+fx->im[b]*fx->cosine[tw];
                fx->re[b]=fx->re[a]-re; fx->im[b]=fx->im[a]-im;
                fx->re[a]+=re; fx->im[a]+=im;
            }
        }
    }
    float bands[4]={0},total=0,log_sum=0;
    unsigned count=0;
    for(unsigned k=2;k<=240;k++) { /* bin centers 62.5..7500 Hz */
        float power=fx->re[k]*fx->re[k]+fx->im[k]*fx->im[k];
        bands[k<16?0:k<64?1:k<128?2:3]+=power;
        total+=power;
        log_sum+=logf(fmaxf(power,1e-20f));
        count++;
    }
    if(total<=0) return;
    for(unsigned i=0;i<4;i++) out->band_q15[i]=(uint16_t)lroundf(bands[i]/total*32768);
    float flatness=expf(log_sum/count)/(total/count);
    out->flatness_q15=(uint16_t)lroundf(fminf(flatness,1)*32767);
}

/* YIN compares equal-length, unwindowed DC-filtered segments at every lag.
 * Stop once the first below-threshold trough and its right neighbor are known. */
static void pitch(tinyrt_mic_fx_t *fx,tinyrt_mic_frame_t *out)
{
    float sum=0;
    fx->yin[0]=1;
    unsigned best=0;
    for(unsigned lag=1;lag<=TINYRT_MIC_YIN_MAX;lag++) {
        float d=0;
        for(unsigned i=0;i<N-TINYRT_MIC_YIN_MAX;i++) {
            float delta=fx->window[i]-fx->window[i+lag];
            d+=delta*delta;
        }
        sum+=d;
        fx->yin[lag]=sum>0 ? d*lag/sum : 1;
        if(lag>TINYRT_MIC_YIN_MIN && fx->yin[lag-1]<TINYRT_MIC_YIN_THRESHOLD &&
           fx->yin[lag]>=fx->yin[lag-1]) { best=lag-1; break; }
    }
    if(!best && fx->yin[TINYRT_MIC_YIN_MAX]<TINYRT_MIC_YIN_THRESHOLD)
        best=TINYRT_MIC_YIN_MAX;
    if(!best) return;
    float period=(float)best;
    if(best<TINYRT_MIC_YIN_MAX) {
        float left=fx->yin[best-1],mid=fx->yin[best],right=fx->yin[best+1];
        float curvature=left-2*mid+right;
        if(curvature>0) period+=0.5f*(left-right)/curvature;
    }
    period=fmaxf(TINYRT_MIC_YIN_MIN,fminf(period,TINYRT_MIC_YIN_MAX));
    out->pitch_hz_q4=(uint16_t)lroundf(TINYRT_MIC_RATE*16/period);
    out->pitch_conf_q15=(uint16_t)lroundf((1-fx->yin[best])*32767);
}

/* Keep the eligible samples sorted as well as in age order. */
static int16_t floor_db(tinyrt_mic_fx_t *fx,int16_t rms,uint16_t flags)
{
    unsigned n=fx->history_count;
    if(!(flags&(TINYRT_MICF_PLAYBACK|TINYRT_MICF_SETTLING))) {
        if(n==TINYRT_MIC_HISTORY) {
            int16_t oldest=fx->history[fx->history_pos];
            unsigned i=0;
            while(fx->sorted[i]!=oldest) i++;
            n--;
            memmove(fx->sorted+i,fx->sorted+i+1,(n-i)*sizeof(int16_t));
        }
        fx->history[fx->history_pos]=rms;
        fx->history_pos=(fx->history_pos+1)%TINYRT_MIC_HISTORY;
        unsigned i=n;
        while(i && fx->sorted[i-1]>rms) { fx->sorted[i]=fx->sorted[i-1]; i--; }
        fx->sorted[i]=rms;
        fx->history_count=++n;
    }
    return n?fx->sorted[(n-1)/5]:(int16_t)(256*TINYRT_MIC_MIN_DB);
}

void tinyrt_mic_fx_push(tinyrt_mic_fx_t *fx,const int16_t pcm[TINYRT_MIC_HOP],
                        uint32_t t_ms,uint16_t extra_flags,tinyrt_mic_frame_t *out)
{
    const unsigned hop=TINYRT_MIC_HOP;
    memset(out,0,sizeof(*out));
    out->seq=fx->seq++; out->t_ms=t_ms; out->flags=extra_flags;
    if((fx->warmup+1)*hop<N) out->flags|=TINYRT_MICF_SETTLING;
    memmove(fx->window,fx->window+hop,(N-hop)*sizeof(float));
    float sum=0,peak=0;
    for(unsigned i=0;i<hop;i++) {
        int sample=pcm[i];
        float y=sample-fx->previous_in+TINYRT_MIC_DC_POLE*fx->previous_out;
        fx->previous_out=y; fx->previous_in=(float)sample;
        fx->window[N-hop+i]=y;
        sum+=y*y; peak=fmaxf(peak,fabsf(y));
        if(sample>=TINYRT_MIC_CLIP || sample<=-TINYRT_MIC_CLIP) out->flags|=TINYRT_MICF_CLIPPED;
        int sign=y>=TINYRT_MIC_ZCR_GATE?1:y<=-TINYRT_MIC_ZCR_GATE?-1:fx->sign;
        if(fx->sign && sign!=fx->sign) out->zcr++;
        fx->sign=sign;
    }
    out->peak=(uint16_t)lroundf(fminf(32767,peak));
    float db=sum>0 ? 10*log10f(sum/(hop*32768.0f*32768.0f)) : TINYRT_MIC_MIN_DB;
    out->rms_db_q8=(int16_t)lroundf(fmaxf(TINYRT_MIC_MIN_DB,db)*256);
    out->floor_db_q8=floor_db(fx,out->rms_db_q8,out->flags);
    /* Floor convergence is a quality flag, not an ADC transient: its own
     * warmup samples must enter the history for the floor to converge. */
    if(fx->warmup<TINYRT_MIC_WARMUP_FRAMES) { fx->warmup++; out->flags|=TINYRT_MICF_SETTLING; }
    if(db>=TINYRT_MIC_BANDS_MIN_DB) spectrum(fx,out);
    if(db>=TINYRT_MIC_PITCH_MIN_DB) pitch(fx,out);
}
