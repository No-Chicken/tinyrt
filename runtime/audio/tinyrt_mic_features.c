#include "tinyrt_mic_features.h"
#include <math.h>
#include <string.h>

#define N TINYRT_MIC_FFT
/* IEEE binary32 and explicit FMA keep quantized frames identical across
 * RV32 and desktop libm/compilers. Only exact exponent scaling and specified
 * IEEE rounding operations are delegated to the platform. */
static float phase_sine(unsigned phase,unsigned quarter)
{
    unsigned quadrant=phase/quarter, step=phase%quarter;
    if(quadrant&1) step=quarter-step;
    float x=(float)step/(float)quarter*1.5707963267948966f;
    float xx=x*x;
    /* sin(x) through x^13 on [0, pi/2], remainder below 7e-10. */
    float p=fmaf(xx,1.0f/6227020800.0f,-1.0f/39916800.0f);
    p=fmaf(xx,p,1.0f/362880.0f);
    p=fmaf(xx,p,-1.0f/5040.0f);
    p=fmaf(xx,p,1.0f/120.0f);
    p=fmaf(xx,p,-1.0f/6.0f);
    float value=x*fmaf(xx,p,1.0f);
    return quadrant&2 ? -value : value;
}

/* Positive, normal binary32 inputs. Range reduction gives |z| <= 0.172
 * for ln(m) = 2(z + z^3/3 + ...). The omitted terms sum to < 8e-10. */
static float feature_log(float x)
{
    uint32_t bits;
    memcpy(&bits,&x,sizeof(bits));
    int exponent=(int)(bits>>23)-127;
    bits=(bits&0x007fffffu)|0x3f800000u;
    float m;
    memcpy(&m,&bits,sizeof(m));
    if(m>1.41421356237f) { m*=0.5f; exponent++; }
    float z=(m-1)/(m+1),zz=z*z;
    float p=fmaf(zz,1.0f/9.0f,1.0f/7.0f);
    p=fmaf(zz,p,1.0f/5.0f);
    p=fmaf(zz,p,1.0f/3.0f);
    return fmaf((float)exponent,0.6931471805599453f,2*z*fmaf(zz,p,1.0f));
}

/* exp(x), x <= 0. Reduce to |r| <= ln(2)/2. Terms through r^7 leave
 * less than 8e-9 absolute error before the exact power-of-two scaling. */
static float feature_exp(float x)
{
    int n=(int)lroundf(x*1.4426950408889634f);
    float r=fmaf(-(float)n,0.6931471805599453f,x);
    float p=fmaf(r,1.0f/5040.0f,1.0f/720.0f);
    p=fmaf(r,p,1.0f/120.0f);
    p=fmaf(r,p,1.0f/24.0f);
    p=fmaf(r,p,1.0f/6.0f);
    p=fmaf(r,p,0.5f);
    return ldexpf(fmaf(r,fmaf(r,p,1.0f),1.0f),n);
}
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
    for(unsigned i=0;i<N;i++) fx->hann[i]=fmaf(-0.5f,phase_sine(4*i+(N-1),N-1),0.5f);
    for(unsigned i=0;i<N/2;i++) {
        fx->cosine[i]=phase_sine(4*i+N,N);
        fx->sine[i]=-phase_sine(4*i,N);
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
                float re=fmaf(fx->re[b],fx->cosine[tw],-fx->im[b]*fx->sine[tw]);
                float im=fmaf(fx->re[b],fx->sine[tw],fx->im[b]*fx->cosine[tw]);
                fx->re[b]=fx->re[a]-re; fx->im[b]=fx->im[a]-im;
                fx->re[a]+=re; fx->im[a]+=im;
            }
        }
    }
    float bands[4]={0},total=0,log_sum=0;
    unsigned count=0;
    for(unsigned k=2;k<=240;k++) { /* bin centers 62.5..7500 Hz */
        float power=fmaf(fx->re[k],fx->re[k],fx->im[k]*fx->im[k]);
        bands[k<16?0:k<64?1:k<128?2:3]+=power;
        total+=power;
        log_sum+=feature_log(fmaxf(power,1e-20f));
        count++;
    }
    if(total<=0) return;
    for(unsigned i=0;i<4;i++) out->band_q15[i]=(uint16_t)lroundf(bands[i]/total*32768);
    float flatness=feature_exp(log_sum/count-feature_log(total/count));
    out->flatness_q15=(uint16_t)lroundf(fminf(flatness,1)*32767);
}

/* YIN compares equal-length, unwindowed DC-filtered segments at every lag.
 * Stop once the first below-threshold trough and its right neighbor are known. */
static void pitch(tinyrt_mic_fx_t *fx,tinyrt_mic_frame_t *out)
{
    /* Seven-tap binomial low-pass, followed by decimation to 8 kHz.
     * Take only valid convolutions from the 512-sample analysis window.
     * FFT output is no longer needed, so the filtered signal reuses its RAM. */
    const unsigned samples=(N-7)/2+1;
    float *filtered=fx->re;
    for(unsigned i=0;i<samples;i++) {
        const float *s=fx->window+2*i;
        float pair=fmaf(6.0f,s[1]+s[5],s[0]+s[6]);
        pair=fmaf(15.0f,s[2]+s[4],pair);
        filtered[i]=fmaf(20.0f,s[3],pair)*(1.0f/64.0f);
    }
    float sum=0;
    fx->yin[0]=1;
    unsigned best=0;
    for(unsigned lag=1;lag<=TINYRT_MIC_YIN_MAX;lag++) {
        float d=0;
        for(unsigned i=0;i<samples-TINYRT_MIC_YIN_MAX;i++) {
            float delta=filtered[i]-filtered[i+lag];
            d=fmaf(delta,delta,d);
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
        float curvature=fmaf(-2,mid,left)+right;
        if(curvature>0) period+=0.5f*(left-right)/curvature;
    }
    period=fmaxf(TINYRT_MIC_YIN_MIN,fminf(period,TINYRT_MIC_YIN_MAX));
    out->pitch_hz_q4=(uint16_t)lroundf(TINYRT_MIC_YIN_RATE*16/period);
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
        float y=fmaf(TINYRT_MIC_DC_POLE,fx->previous_out,sample-fx->previous_in);
        fx->previous_out=y; fx->previous_in=(float)sample;
        fx->window[N-hop+i]=y;
        sum=fmaf(y,y,sum); peak=fmaxf(peak,fabsf(y));
        if(sample>=TINYRT_MIC_CLIP || sample<=-TINYRT_MIC_CLIP) out->flags|=TINYRT_MICF_CLIPPED;
        int sign=y>=TINYRT_MIC_ZCR_GATE?1:y<=-TINYRT_MIC_ZCR_GATE?-1:fx->sign;
        if(fx->sign && sign!=fx->sign) out->zcr++;
        fx->sign=sign;
    }
    out->peak=(uint16_t)lroundf(fminf(32767,peak));
    float db=4.342944819032518f*feature_log(fmaxf(1e-10f,sum/(hop*32768.0f*32768.0f)));
    out->rms_db_q8=(int16_t)lroundf(fmaxf(TINYRT_MIC_MIN_DB,db)*256);
    out->floor_db_q8=floor_db(fx,out->rms_db_q8,out->flags);
    /* Floor convergence is a quality flag, not an ADC transient: its own
     * warmup samples must enter the history for the floor to converge. */
    if(fx->warmup<TINYRT_MIC_WARMUP_FRAMES) { fx->warmup++; out->flags|=TINYRT_MICF_SETTLING; }
    if(db>=TINYRT_MIC_BANDS_MIN_DB) spectrum(fx,out);
    if(db>=TINYRT_MIC_PITCH_MIN_DB) pitch(fx,out);
}
