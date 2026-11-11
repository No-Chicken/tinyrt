/* Optional EEDB sidecar. Sample positions are uint32 half-open intervals.
 * Unrelated JSON values are skipped, so acquisition metadata can grow. */
#include "mic_wav.h"
#include "tinyrt_mic_features.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t begin,end; } span_t;
typedef struct { span_t *spans; size_t count,capacity; int present; } ranges_t;
struct mic_wav_meta {
    ranges_t playback,settling,overrun,gaps;
    uint32_t start_ms;
};

static char *space(char *p)
{
    while(isspace((unsigned char)*p)) p++;
    return p;
}

static char *string_end(char *p)
{
    if(*p!='"') return NULL;
    for(++p;*p;p++) {
        if(*p=='"') return p+1;
        if((unsigned char)*p<32) return NULL;
        if(*p=='\\' && !*++p) break;
    }
    return NULL;
}

/* Skip one complete value including strings containing escaped delimiters. */
static char *value_end(char *p)
{
    p=space(p);
    if(*p=='"') return string_end(p);
    if(*p!='{' && *p!='[') {
        char *end=p;
        while(*end && !strchr(",}]",*end) && !isspace((unsigned char)*end)) end++;
        return end!=p?end:NULL;
    }
    size_t depth=0;
    do {
        if(!*p) return NULL;
        if(*p=='"') { p=string_end(p); if(!p) return NULL; continue; }
        if(*p=='{' || *p=='[') depth++;
        if(*p=='}' || *p==']') depth--;
        p++;
    } while(depth);
    return p;
}

static int number(char **p,uint32_t *n)
{
    char *s=space(*p);
    if(!isdigit((unsigned char)*s)) return 0;
    uint32_t v=0;
    do {
        uint32_t digit=(uint32_t)(*s++-'0');
        if(v>(UINT32_MAX-digit)/10) return 0;
        v=v*10+digit;
    } while(isdigit((unsigned char)*s));
    *n=v; *p=space(s);
    return 1;
}

static int read_ranges(char **p,ranges_t *r,uint32_t samples)
{
    char *s=space(*p);
    if(r->present || *s++!='[') return 0;
    r->present=1; s=space(s);
    if(*s!=']') for(;;) {
        span_t span;
        if(*s++!='[' || !number(&s,&span.begin) || *s++!=',' || !number(&s,&span.end) ||
           *s++!=']' || span.end<=span.begin || span.end>samples ||
           (r->count && span.begin<r->spans[r->count-1].end)) return 0;
        if(r->count==r->capacity) {
            size_t capacity=r->capacity?r->capacity*2:8;
            if(capacity>SIZE_MAX/sizeof(span_t)) return 0;
            span_t *spans=realloc(r->spans,capacity*sizeof(span_t));
            if(!spans) return 0;
            r->spans=spans; r->capacity=capacity;
        }
        r->spans[r->count++]=span;
        s=space(s);
        if(*s==']') break;
        if(*s++!=',') return 0;
        s=space(s);
    }
    *p=s+1;
    return 1;
}

static int parse_meta(mic_wav_t *wav,char *p)
{
    struct mic_wav_meta *m=wav->meta;
    p=space(p);
    if(*p++!='{') return 0;
    p=space(p);
    if(*p!='}') for(;;) {
        char *key=p+1;
        p=string_end(p);
        if(!p) return 0;
        p[-1]=0; p=space(p);
        if(*p++!=':') return 0;
        p=space(p);
        ranges_t *r=NULL;
        if(!strcmp(key,"playback")) r=&m->playback;
        else if(!strcmp(key,"settling")) r=&m->settling;
        else if(!strcmp(key,"overrun")) r=&m->overrun;
        else if(!strcmp(key,"gaps")) r=&m->gaps;
        if(r) { if(!read_ranges(&p,r,wav->samples)) return 0; }
        else if(!strcmp(key,"start_ms")) { if(!number(&p,&m->start_ms)) return 0; }
        else { p=value_end(p); if(!p) return 0; }
        p=space(p);
        if(*p=='}') break;
        if(*p++!=',') return 0;
        p=space(p);
    }
    return *space(p+1)==0;
}

int mic_wav_load_meta(mic_wav_t *wav,const char *path)
{
    size_t n=strlen(path);
    char *name=malloc(n+6);
    if(!name) return 0;
    memcpy(name,path,n+1);
    const char *ext=strrchr(name,'.');
    if(ext && !strpbrk(ext,"/\\")) n=(size_t)(ext-name);
    memcpy(name+n,".json",6);
    FILE *f=fopen(name,"rb");
    int missing=errno==ENOENT;
    free(name);
    if(!f) return missing;
    long bytes;
    char *text=NULL;
    int ok=0;
    if(fseek(f,0,SEEK_END) || (bytes=ftell(f))<0 ||
       (uint64_t)bytes>=SIZE_MAX || fseek(f,0,SEEK_SET)) goto done;
    text=malloc((size_t)bytes+1);
    wav->meta=calloc(1,sizeof(*wav->meta));
    if(!text || !wav->meta || fread(text,1,(size_t)bytes,f)!=(size_t)bytes) goto done;
    text[bytes]=0;
    ok=parse_meta(wav,text);
done:
    free(text); fclose(f);
    return ok;
}

void mic_wav_close(mic_wav_t *wav)
{
    if(wav->file) fclose(wav->file);
    struct mic_wav_meta *m=wav->meta;
    if(m) {
        free(m->playback.spans); free(m->settling.spans);
        free(m->overrun.spans); free(m->gaps.spans); free(m);
    }
    memset(wav,0,sizeof(*wav));
}

static int overlaps(const ranges_t *r,uint32_t begin,uint32_t end)
{
    size_t lo=0,hi=r->count;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if(r->spans[mid].end<=begin) lo=mid+1;
        else hi=mid;
    }
    return lo<r->count && r->spans[lo].begin<end;
}

uint16_t mic_wav_flags(const mic_wav_t *wav,uint32_t begin,uint32_t end)
{
    const struct mic_wav_meta *m=wav->meta;
    uint16_t flags=0;
    if(m) {
        if(overlaps(&m->playback,begin,end)) flags|=TINYRT_MICF_PLAYBACK;
        if(overlaps(&m->settling,begin,end)) flags|=TINYRT_MICF_SETTLING;
        if(overlaps(&m->overrun,begin,end) || overlaps(&m->gaps,begin,end)) flags|=TINYRT_MICF_OVERRUN;
    }
    return flags;
}

uint32_t mic_wav_start_ms(const mic_wav_t *wav)
{
    return wav->meta?wav->meta->start_ms:0;
}
