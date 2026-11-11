#include "mic_wav.h"
#include <limits.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}

int mic_wav_open(mic_wav_t *wav,const char *path)
{
    memset(wav,0,sizeof(*wav));
    uint8_t buf[16];
    FILE *f=fopen(path,"rb");
    if(!f) return 0;
    if(fseek(f,0,SEEK_END)) goto bad;
    long file_size=ftell(f);
    if(file_size<12 || fseek(f,0,SEEK_SET) || fread(buf,1,12,f)!=12 ||
       memcmp(buf,"RIFF",4) || memcmp(buf+8,"WAVE",4)) goto bad;
    uint32_t size=le32(buf+4);
    if(size<4 || (uint64_t)size>(uint64_t)(file_size-8)) goto bad;
    uint32_t left=size-4;
    int have_fmt=0,have_data=0;
    while(left) {
        if(left<8 || fread(buf,1,8,f)!=8) goto bad;
        left-=8;
        uint32_t chunk=le32(buf+4);
        if(chunk>left || (chunk&1u)>left-chunk) goto bad;
        long start=ftell(f);
        if(start<0 || (uint64_t)start+chunk+(chunk&1u)>LONG_MAX) goto bad;
        if(!memcmp(buf,"fmt ",4)) {
            if(have_fmt || chunk<16 || fread(buf,1,16,f)!=16) goto bad;
            if(le32(buf)!=0x00010001u || le32(buf+4)!=16000 || le32(buf+8)!=32000 ||
               le32(buf+12)!=0x00100002u) goto bad;
            have_fmt=1;
        } else if(!memcmp(buf,"data",4)) {
            if(have_data || (chunk&1u)) goto bad;
            wav->start=start; wav->samples=chunk/2; have_data=1;
        }
        if(fseek(f,start+(long)(chunk+(chunk&1u)),SEEK_SET)) goto bad;
        left-=chunk+(chunk&1u);
    }
    if(!have_fmt || !have_data || fseek(f,wav->start,SEEK_SET)) goto bad;
    wav->file=f;
    if(!mic_wav_load_meta(wav,path)) { mic_wav_close(wav); return 0; }
    return 1;
bad:
    fclose(f);
    return 0;
}

unsigned mic_wav_read(mic_wav_t *wav,int16_t *dst,unsigned count)
{
    /* Decode explicitly so native hosts use the same signed samples. */
    unsigned done=0;
    while(done<count) {
        uint8_t bytes[320];
        unsigned n=count-done;
        if(n>sizeof(bytes)/2) n=sizeof(bytes)/2;
        n=(unsigned)fread(bytes,2,n,wav->file);
        if(!n) break;
        for(unsigned i=0;i<n;i++) {
            uint16_t sample=(uint16_t)(bytes[2*i]|bytes[2*i+1]<<8);
            dst[done+i]=(int16_t)sample;
        }
        done+=n;
    }
    return done;
}
