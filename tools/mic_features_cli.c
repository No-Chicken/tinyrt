#include "mic_wav.h"
#include "tinyrt_mic_features.h"
#include <inttypes.h>
#include <stdlib.h>

int main(int argc,char **argv)
{
    if(argc!=3) { fprintf(stderr,"Usage: tinyrt-mic-features input.wav output.csv\n"); return 2; }
    mic_wav_t wav;
    if(!mic_wav_open(&wav,argv[1])) {
        fprintf(stderr,"Expected a valid 16000 Hz mono PCM16 WAV and valid capture metadata if present\n"); return 1;
    }
    tinyrt_mic_fx_t *fx=malloc(tinyrt_mic_fx_size());
    if(!fx) { mic_wav_close(&wav); return 1; }
    FILE *csv=fopen(argv[2],"wb");
    if(!csv) { free(fx); mic_wav_close(&wav); return 1; }
    tinyrt_mic_fx_reset(fx);
    fputs("seq,t_ms,rms_db_q8,floor_db_q8,peak,zcr,band0_q15,band1_q15,band2_q15,band3_q15,"
          "flatness_q15,pitch_hz_q4,pitch_conf_q15,flags\n",csv);
    int failed=0;
    for(uint32_t i=0;i<wav.samples/TINYRT_MIC_HOP;i++) {
        int16_t pcm[TINYRT_MIC_HOP];
        if(mic_wav_read(&wav,pcm,TINYRT_MIC_HOP)!=TINYRT_MIC_HOP) { failed=1; break; }
        tinyrt_mic_frame_t f;
        uint16_t flags=mic_wav_flags(&wav,i*TINYRT_MIC_HOP,(i+1)*TINYRT_MIC_HOP);
        uint32_t t_ms=mic_wav_start_ms(&wav)+(i+1)*TINYRT_MIC_FRAME_MS;
        tinyrt_mic_fx_push(fx,pcm,t_ms,flags,&f);
        if(fprintf(csv,"%" PRIu32 ",%" PRIu32 ",%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                   f.seq,f.t_ms,f.rms_db_q8,f.floor_db_q8,f.peak,f.zcr,
                   f.band_q15[0],f.band_q15[1],f.band_q15[2],f.band_q15[3],
                   f.flatness_q15,f.pitch_hz_q4,f.pitch_conf_q15,f.flags)<0) { failed=1; break; }
    }
    if(fclose(csv)) failed=1;
    mic_wav_close(&wav); free(fx);
    return failed;
}
