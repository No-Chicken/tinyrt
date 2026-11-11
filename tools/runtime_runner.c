/* Desktop validation of raw Wasm. No package trust, BLE, or disk persistence. */
#include "tinyrt_runtime.h"
#include "mic_replay.h"
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <time.h>
#endif

typedef struct { uint32_t mask; int32_t values[TINYRT_KV_KEYS_MAX]; uint32_t blob_len; uint8_t blob[2048]; } saved_t;
static saved_t saved;
static void desktop_log(void *ctx,const char *text,uint32_t length) {
    (void)ctx;
    printf("app: %.*s\n",(int)length,text);
}
static uint32_t desktop_random(void *ctx) {
    uint32_t value;
    (void)ctx;
#ifdef _WIN32
    if(BCryptGenRandom(NULL,(PUCHAR)&value,sizeof(value),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0) exit(1);
#else
    FILE *source=fopen("/dev/urandom","rb");
    if(!source || fread(&value,sizeof(value),1,source)!=1) exit(1);
    fclose(source);
#endif
    return value;
}
static uint32_t now_ms_value, commits;
static int32_t sim_imu[7]={0,0,1000,0,0,0,0};
static int32_t sim_battery[4]={100,0,1,4200};
static int32_t sim_brightness=80,sim_keep_on=0,sim_utc_offset=0,sim_time_valid=1;
static int64_t sim_epoch=946684800;
static int32_t sim_config(uint32_t hz,uint32_t mask) {(void)hz;(void)mask;return 0;}
static int32_t sim_motion(int32_t values[7]) {memcpy(values,sim_imu,sizeof(sim_imu));return 0;}
static int32_t sim_power(int32_t values[4]) {memcpy(values,sim_battery,sizeof(sim_battery));return 0;}
static int32_t sim_set_brightness(int32_t pct) {sim_brightness=pct;return 0;}
static int32_t sim_set_keep_on(int32_t on) {sim_keep_on=on;return 0;}
static int32_t sim_datetime(int32_t values[9]) {
    time_t local=(time_t)(sim_epoch+now_ms_value/1000+(int64_t)sim_utc_offset*60);
    struct tm *t=gmtime(&local);
    if(!t) return -2;
    int32_t result[9]={t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec,
                       t->tm_wday,sim_utc_offset,sim_time_valid};
    memcpy(values,result,sizeof(result));return sim_time_valid?0:-2;
}
static uint32_t sim_audio_bytes,sim_audio_ms;
static int32_t sim_queue_free(void) {
    uint32_t elapsed=now_ms_value-sim_audio_ms;
    sim_audio_ms=now_ms_value;
    if(elapsed>=250) sim_audio_bytes=0;
    else if(sim_audio_bytes>elapsed*32) sim_audio_bytes-=elapsed*32;
    else sim_audio_bytes=0;
    return (int32_t)(8000-sim_audio_bytes);
}
static int32_t sim_write_audio(const void *pcm,uint32_t len) {
    (void)pcm;uint32_t room=(uint32_t)sim_queue_free();
    if(!room)return -2;
    if(len>room)len=room;
    sim_audio_bytes+=len;
    mic_replay_playback(now_ms_value,(sim_audio_bytes+31)/32);
    return (int32_t)len;
}
static int32_t sim_tone(uint32_t freq,uint32_t ms) {(void)freq;mic_replay_playback(now_ms_value,ms);return 0;}
static int32_t sim_pattern(const uint16_t *steps,uint32_t count) {(void)steps;(void)count;return 0;}
static int32_t sim_blob_read(void *ctx,void *dst,uint32_t size) {
    (void)ctx;if(size<saved.blob_len)return -2;
    if(saved.blob_len)memcpy(dst,saved.blob,saved.blob_len);
    return (int32_t)saved.blob_len;
}
static int32_t sim_blob_write(void *ctx,const void *src,uint32_t size) {
    (void)ctx;if(size>sizeof(saved.blob))return -2;
    if(size)memcpy(saved.blob,src,size);
    saved.blob_len=size;return (int32_t)size;
}
static int32_t sim_acc_send(uint32_t type,const void *src,uint32_t size) {(void)type;(void)src;(void)size;return -2;}
static int32_t sim_acc_read(void *dst,uint32_t size) {(void)dst;(void)size;return -2;}
static int32_t sim_mic_open(uint32_t rate,uint32_t flags) {
    (void)rate; return mic_replay_open(now_ms_value,flags);
}
static int32_t sim_mic_gain(int32_t db) {return mic_replay_gain(now_ms_value,db);}
static const tinyrt_device_api_t sim_device={
    .imu_config=sim_config,.imu_read=sim_motion,.time_local=sim_datetime,.battery_info=sim_power,
    .display_brightness=sim_set_brightness,.screen_keep_on=sim_set_keep_on,.audio_write=sim_write_audio,
    .audio_queue_free=sim_queue_free,.audio_tone=sim_tone,.haptic_pattern=sim_pattern,
    .acc_send=sim_acc_send,.acc_read=sim_acc_read,
    .mic_open=sim_mic_open,.mic_close=mic_replay_close,.mic_gain=sim_mic_gain,
    .mic_read=mic_replay_pcm,.mic_features=mic_replay_features
};
static uint32_t clock_interval_value=TINYRT_DEFAULT_CLOCK_INTERVAL_MS;
static uint64_t next_clock_deadline=UINT64_MAX;
static tinyrt_frame_t frame;
static uint32_t last_frame_count;
static bool capture_pixels;
static uint16_t raster_surface[466u*466u];
static uint32_t raster_bytes;
static uint8_t *resources;
static uint32_t resources_size;
static tinyrt_status_t read_resource(void *ctx,uint32_t off,void *out,uint32_t length) {
    (void)ctx;
    if(off>resources_size || length>resources_size-off || length>4096)return TINYRT_INVALID_ARGUMENT;
    if(length)memcpy(out,resources+off,length);
    return TINYRT_OK;
}
/* Desktop preview validates audio resource ranges but intentionally stays silent. */
static tinyrt_status_t play_audio(void *ctx,uint32_t offset,uint32_t length,uint32_t rate) {
    (void)ctx;
    if(offset>resources_size || length>resources_size-offset) return TINYRT_INVALID_ARGUMENT;
    mic_replay_playback(now_ms_value,(length/2*1000+rate-1)/rate);
    return TINYRT_OK;
}
static double milliseconds(void) {
#ifdef _WIN32
    LARGE_INTEGER n, f; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f);
    return (double)n.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
#endif
}
static int32_t get_value(void *ctx, uint32_t key, int32_t fallback) {
    (void)ctx;
    return key < TINYRT_KV_KEYS_MAX && (saved.mask & (1u << key)) ? saved.values[key] : fallback;
}
static tinyrt_status_t set_value(void *ctx, uint32_t key, int32_t value) {
    (void)ctx;
    if (key >= TINYRT_KV_KEYS_MAX) return TINYRT_INVALID_ARGUMENT;
    saved.mask |= 1u << key; saved.values[key] = value; return TINYRT_OK;
}
static uint32_t now(void *ctx) { (void)ctx; return now_ms_value; }
static void json_string(const char *s) {
    putchar('"');
    for (const unsigned char *p=(const unsigned char *)s; *p; ++p) {
        if (*p=='"' || *p=='\\') { putchar('\\'); putchar(*p); }
        else if (*p<32) printf("\\u%04x",(unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}
static uint32_t pixel_crc32(const uint8_t *bytes,uint32_t size) {
    uint32_t value=UINT32_MAX;
    for(uint32_t i=0;i<size;++i){value^=bytes[i];
        for(unsigned j=0;j<8;++j)value=(value>>1)^(UINT32_C(0xedb88320)&(0u-(value&1u)));}
    return ~value;
}
static void emit(const char *phase, tinyrt_status_t status, const char *error, double elapsed) {
    printf("{\"phase\":"); json_string(phase);
    printf(",\"status\":%d,\"now_ms\":%" PRIu32 ",\"commits\":%" PRIu32
        ",\"heap_bytes\":%zu,\"elapsed_ms\":%.6f,\"error\":",(int)status,now_ms_value,
        commits,tinyrt_runtime_memory_used(),elapsed); json_string(error);
    printf(",\"kv\":[");
    uint32_t keys = saved.mask >> TINYRT_KV_KEYS_BASE ? TINYRT_KV_KEYS_MAX : TINYRT_KV_KEYS_BASE;
    for (uint32_t k=0;k<keys;++k) {
        if (k) putchar(',');
        if (saved.mask & (1u << k)) printf("%" PRId32,saved.values[k]); else printf("null");
    }
    uint32_t pixel_bytes=status==TINYRT_OK && strcmp(phase,"shutdown") && frame.count ? frame.pixel_bytes:0;
    printf("],\"clock_interval_ms\":%" PRIu32 ",\"pixel_bytes\":%" PRIu32
        ",\"pixel_crc32\":%" PRIu32 ",\"frame\":[",clock_interval_value,pixel_bytes,pixel_crc32(frame.pixels,pixel_bytes));
    if (status==TINYRT_OK && strcmp(phase,"shutdown")) {
        for (uint32_t k=0;k<frame.count;++k) {
            const tinyrt_draw_command_t *c=&frame.commands[k];
            if (k) putchar(',');
            printf("{\"source_w\":%" PRId32 ",\"source_h\":%" PRId32 ",\"kind\":%" PRIu32 ",\"x\":%" PRId32 ",\"y\":%" PRId32
                ",\"w\":%" PRId32 ",\"h\":%" PRId32 ",\"rgb\":%" PRIu32
                ",\"radius\":%" PRId32 ",\"thickness\":%" PRId32
                ",\"start_angle\":%" PRId32 ",\"end_angle\":%" PRId32
                ",\"font_px\":%" PRId32 ",\"align\":%" PRId32 ",\"text\":",
                c->source_w,c->source_h,c->kind,c->x,c->y,c->w,c->h,c->rgb,c->radius,c->thickness,
                c->start_angle,c->end_angle,c->font_px,c->align);
            json_string(c->text);
            if (c->kind==TINYRT_DRAW_RGB565 || c->kind==TINYRT_DRAW_RGB565_SCALED)
                printf(",\"pixel_offset\":%" PRIu32,c->pixel_offset);
            putchar('}');
        }
    }
    printf("]");
    if(capture_pixels){printf(",\"pixels_hex\":\"");for(uint32_t i=0;i<pixel_bytes;++i)printf("%02x",frame.pixels[i]);putchar('"');}
    if(capture_pixels && raster_bytes && frame.count && frame.commands[0].kind==TINYRT_DRAW_RASTER) {
        printf(",\"raster_pixels_hex\":\"");
        for(uint32_t i=0;i<466u*466u;i++)printf("%02x%02x",raster_surface[i]&255u,raster_surface[i]>>8);putchar('"');
    }
    printf("}\n"); fflush(stdout);
}
static int parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s) return 0;
    for (const char *p=s;*p;++p) if (*p<'0' || *p>'9') return 0;
    errno=0; char *end=NULL; unsigned long long value=strtoull(s,&end,10);
    if (errno || !end || *end || value>UINT32_MAX) return 0;
    *out=(uint32_t)value; return 1;
}
static int parse_mg(const char *s, int32_t *out) {
    if (!s || !*s) return 0;
    errno=0; char *end=NULL; long value=strtol(s,&end,10);
    if (errno || !end || *end || value < -16000 || value > 16000) return 0;
    *out=(int32_t)value; return 1;
}
static int parse_range(const char *s, long low, long high, int32_t *out) {
    if (!s || !*s) return 0;
    errno=0; char *end=NULL; long value=strtol(s,&end,10);
    if (errno || !end || *end || value < low || value > high) return 0;
    *out=(int32_t)value; return 1;
}
static tinyrt_status_t cycle(tinyrt_runtime_t *rt, const char *phase, int init,
                            int32_t kind, int32_t x, int32_t y, int32_t arg) {
    saved_t before=saved; double start=milliseconds();
    tinyrt_status_t result=init ? tinyrt_runtime_init(rt,x,y) : tinyrt_runtime_event(rt,kind,x,y,arg);
    if (result==TINYRT_OK) { tinyrt_frame_release(&frame); result=tinyrt_runtime_render(rt,&frame); }
    if (result==TINYRT_OK && frame.count && frame.commands[0].kind==TINYRT_DRAW_RASTER) {
        uint32_t count=frame.count;frame.count=1;
        for(uint32_t strip_y=0;strip_y<466;strip_y+=32) { uint32_t rows=466-strip_y;if(rows>32)rows=32;
            if(tinyrt_gfx_render_strip(&frame,raster_surface+strip_y*466,466,466,466,strip_y,rows,NULL,NULL)!=0) {result=TINYRT_VERIFY_FAILED;break;} }
        frame.count=count;raster_bytes=result==TINYRT_OK ? sizeof(raster_surface):0;
    }
    if (result==TINYRT_OK && frame.count) last_frame_count=frame.count;
    if (result!=TINYRT_OK) saved=before;
    else if (memcmp(&saved,&before,sizeof(saved))) ++commits;
    clock_interval_value=tinyrt_runtime_clock_interval_ms(rt);
    if (result==TINYRT_OK && (init || kind==2))
        next_clock_deadline=(uint64_t)now_ms_value+clock_interval_value;
    emit(phase,result,result==TINYRT_OK ? "" : tinyrt_runtime_last_error(rt),milliseconds()-start);
    return result;
}
static tinyrt_status_t advance_mic(tinyrt_runtime_t *rt,uint32_t target)
{
    mic_level_t level;
    int result;
    while((result=mic_replay_advance(target,&level))>0) {
        now_ms_value=level.t_ms;
        tinyrt_status_t status=cycle(rt,"mic_level",0,TINYRT_EV_MIC_LEVEL,level.peak,level.rms,level.flags);
        if(status!=TINYRT_OK) return status;
    }
    now_ms_value=target;
    return result<0?TINYRT_VERIFY_FAILED:TINYRT_OK;
}

static tinyrt_status_t stop_instance(tinyrt_runtime_t **rt, const char *phase) {
    saved_t before=saved; double start=milliseconds(); char error[192]="";
    tinyrt_status_t result=*rt ? tinyrt_runtime_stop(*rt) : TINYRT_OK;
    if (result!=TINYRT_OK) {
        saved=before;
        snprintf(error,sizeof(error),"%s",tinyrt_runtime_last_error(*rt));
    } else if (memcmp(&saved,&before,sizeof(saved))) ++commits;
    tinyrt_frame_release(&frame);
    tinyrt_runtime_destroy(*rt); *rt=NULL; frame.count=0;last_frame_count=0;raster_bytes=0;
    next_clock_deadline=UINT64_MAX;
    clock_interval_value=TINYRT_DEFAULT_CLOCK_INTERVAL_MS;
    if (phase || result!=TINYRT_OK) emit(phase ? phase : "stop",result,error,milliseconds()-start);
    return result;
}
static int usage(void) {
    fprintf(stderr,"Usage: tinyrt-run app.wasm [--min-api-level level] [--pages pages] [--budget instructions] [--permissions mask] [--assets resources.bin] [--mic input.wav]\n"
        "stdin: advance <0..60000_ms> | capture | pointer <kind> <x> <y> | key 1 <0|1> | motion <ax_mg> <ay_mg> <az_mg>\n"
        "       mic_level <peak> <rms> <flags>\n"
        "       gamepad <buttons> <lx> <ly> <rx> <ry> <lt> <rt> <connected 0|1>\n"
        "       time <uint32_ms> | tick <uint32_ms> | touch <x> <y> | stop | restart | reboot\n"
        "advance uses the guest clock interval and monotonic virtual time; capture does not run callbacks.\n"
        "KV is RAM-only. stop/restart/EOF stop normally; reboot forces teardown.\n");
    return 2;
}
int main(int argc, char **argv) {
    if (argc<2 || !strcmp(argv[1],"--help")) return usage();
    tinyrt_package_policy_t policy={1,15,2,100000,1};
    for (int i=2;i<argc;i+=2) {
        uint32_t value;
        if(i+1<argc && !strcmp(argv[i],"--mic")) {
            if(!mic_replay_file(argv[i+1])) {
                fprintf(stderr,"Expected a single valid 16000 Hz mono PCM16 WAV\n"); return 2;
            }
            continue;
        }
        if(i+1<argc && !strcmp(argv[i],"--assets")) {
            FILE *asset=fopen(argv[i+1],"rb");
            if(!asset)return 2;
            resources=malloc(TINYRT_STORE_MAX_PACKAGE_SIZE+1);
            if(!resources){fclose(asset);return 2;}
            size_t n=fread(resources,1,TINYRT_STORE_MAX_PACKAGE_SIZE+1,asset);
            int bad=ferror(asset);fclose(asset);
            if(bad || n>TINYRT_STORE_MAX_PACKAGE_SIZE){free(resources);return 2;}
            resources_size=(uint32_t)n;continue;
        }
        if (i+1>=argc || !parse_u32(argv[i+1],&value)) return usage();
        if (!strcmp(argv[i],"--pages") && value>=1) policy.max_memory_pages=value;
        else if (!strcmp(argv[i],"--budget") && value>=1) policy.instruction_budget=value;
        else if (!strcmp(argv[i],"--min-api-level") && value>=1 && value<=TINYRT_HOST_API_LEVEL) policy.min_api_level=value;
        else if (!strcmp(argv[i],"--permissions") && value<=TINYRT_PERMISSION_ALL) policy.permissions=value;
        else return usage();
    }
    tinyrt_limits_t limits=tinyrt_limits_for_level(policy.min_api_level);
    if (policy.max_memory_pages>limits.memory_pages || policy.instruction_budget>limits.instructions) return usage();
    FILE *f=fopen(argv[1],"rb");
    if (!f) { fprintf(stderr,"Cannot open Wasm file\n"); return 2; }
    if (fseek(f,0,SEEK_END)) { fclose(f); return 2; }
    long length=ftell(f);
    if (length<8 || length>TINYRT_STORE_MAX_PACKAGE_SIZE || fseek(f,0,SEEK_SET)) { fclose(f); return 2; }
    unsigned char *bytes=malloc((size_t)length);
    if (!bytes) { fclose(f); return 2; }
    size_t count=fread(bytes,1,(size_t)length,f); int close_result=fclose(f);
    if (count!=(size_t)length || close_result) { free(bytes); return 2; }
    int exit_code=0;
    if (tinyrt_runtime_system_init()!=TINYRT_OK) { free(bytes); return 1; }
    tinyrt_runtime_host_t host={NULL,get_value,set_value,now,read_resource,play_audio,NULL,NULL,desktop_log,desktop_random,&sim_device,sim_blob_read,sim_blob_write};
    tinyrt_runtime_t *rt=NULL;
    tinyrt_status_t result=tinyrt_runtime_create(bytes,(uint32_t)length,&policy,&host,&rt);
    if (result!=TINYRT_OK) { emit("create",result,"Wasm load rejected",0); exit_code=1; goto done; }
    if (cycle(rt,"init",1,0,466,466,0)!=TINYRT_OK) { exit_code=1; goto done; }
    char line[256];
    while (fgets(line,sizeof(line),stdin)) {
        if (!strchr(line,'\n') && !feof(stdin)) { exit_code=usage(); break; }
        char *op=strtok(line," \t\r\n");
        if (!op) continue;
        char *a=strtok(NULL," \t\r\n"), *b=strtok(NULL," \t\r\n"), *extra=strtok(NULL," \t\r\n");
        uint32_t x=0,y=0;
        if (!strcmp(op,"battery")) {
            char *volts=strtok(NULL," \t\r\n");
            if(!parse_range(a,0,100,&sim_battery[0]) || !parse_range(b,0,1,&sim_battery[1]) ||
               !parse_range(extra,0,1,&sim_battery[2]) || !parse_range(volts,0,6000,&sim_battery[3]) ||
               strtok(NULL," \t\r\n")) {exit_code=usage();break;}
            continue;
        } else if(!strcmp(op,"rtc")) {
            uint32_t epoch;
            if(!parse_u32(a,&epoch) || !parse_range(b,-840,840,&sim_utc_offset) ||
               !parse_range(extra,0,1,&sim_time_valid) || strtok(NULL," \t\r\n")) {exit_code=usage();break;}
            sim_epoch=(int64_t)epoch-now_ms_value/1000;
            if(rt && sim_time_valid && (tinyrt_runtime_input_events(rt)&(1u<<12))) result=cycle(rt,"minute",0,12,0,0,0);
        } else if(!strcmp(op,"mic_level")) {
            int32_t peak,rms,flags;
            if(!parse_range(a,0,32767,&peak) || !parse_range(b,0,32767,&rms) ||
               !parse_range(extra,0,65535,&flags) || strtok(NULL," \t\r\n")) {exit_code=usage();break;}
            if(rt && (tinyrt_runtime_input_events(rt)&(1u<<TINYRT_EV_MIC_LEVEL)))
                result=cycle(rt,op,0,TINYRT_EV_MIC_LEVEL,peak,rms,flags);
        } else if(!strcmp(op,"gyro") || !strcmp(op,"gesture")) {
            bool gyro=!strcmp(op,"gyro");
            int32_t gx,gy,gz;
            if(!parse_range(a,gyro?-20480:1,gyro?20480:5,&gx) ||
               !parse_range(b,-20480,20480,&gy) || !parse_range(extra,-20480,20480,&gz) ||
               strtok(NULL," \t\r\n")) {exit_code=usage();break;}
            if(gyro){sim_imu[3]=gx;sim_imu[4]=gy;sim_imu[5]=gz;sim_imu[6]=(int32_t)now_ms_value;}
            int32_t kind=gyro?10:11;
            if(rt && (tinyrt_runtime_input_events(rt)&(1u<<kind))) result=cycle(rt,op,0,kind,gx,gy,gz);
        } else if (!strcmp(op,"capture") && !a) {
            uint32_t count_before=frame.count;frame.count=last_frame_count;
            capture_pixels=true;emit("capture",TINYRT_OK,"",0);capture_pixels=false;frame.count=count_before;
            continue;
        } else if (!strcmp(op,"advance") && parse_u32(a,&x) && !b && rt && x<=60000 && x>=now_ms_value) {
            /* Match svc_apps event_owner: automatic delivery respects the
             * manifest. Explicit tick remains a strict runtime diagnostic. */
            int64_t previous_minute=(sim_epoch+now_ms_value/1000)/60;
            while ((policy.permissions&TINYRT_PERMISSION_CLOCK) && next_clock_deadline<=x) {
                result=advance_mic(rt,(uint32_t)next_clock_deadline);
                if(result!=TINYRT_OK) break;
                result=cycle(rt,"tick",0,2,0,0,(int32_t)now_ms_value);
                if (result!=TINYRT_OK) break;
            }
            if(result==TINYRT_OK) result=advance_mic(rt,x);
            if(result==TINYRT_OK) {
                if(sim_time_valid && (sim_epoch+x/1000)/60!=previous_minute &&
                   (tinyrt_runtime_input_events(rt)&(1u<<12))) result=cycle(rt,"minute",0,12,0,0,0);
            }
        } else if (!strcmp(op,"time") && parse_u32(a,&x) && !b) {
            if(mic_replay_advance(x,NULL)<0) {exit_code=1;break;}
            now_ms_value=x;
            /* Raw time is an explicit clock warp; do not later run old deadlines. */
            next_clock_deadline=rt ? (uint64_t)x+clock_interval_value : UINT64_MAX;
            continue;
        } else if (!strcmp(op,"pointer") && parse_u32(a,&x) && parse_u32(b,&y) && extra) {
            uint32_t py;char *tail=strtok(NULL," \t\r\n");
            if(x<1 || x>5 || x==2 || y>=466 || !parse_u32(extra,&py) || py>=466 || tail){exit_code=usage();break;}
            if(rt && (!(policy.permissions&TINYRT_PERMISSION_INPUT) ||
                      (x>=3 && !(tinyrt_runtime_input_events(rt)&(1u<<x))))) continue;
            result=cycle(rt,"pointer",0,(int32_t)x,(int32_t)y,(int32_t)py,0);
        } else if (!strcmp(op,"key") && parse_u32(a,&x) && parse_u32(b,&y) && !extra && x==1 && y<=1) {
            if(rt && (!(policy.permissions&TINYRT_PERMISSION_INPUT) ||
                      !(tinyrt_runtime_input_events(rt)&(1u<<6)))) continue;
            result=cycle(rt,"key",0,6,(int32_t)x,(int32_t)y,0);
        } else if (!strcmp(op,"motion")) {
            int32_t ax,ay,az;
            if (!parse_mg(a,&ax) || !parse_mg(b,&ay) || !parse_mg(extra,&az) || strtok(NULL," \t\r\n")) { exit_code=usage();break; }
            sim_imu[0]=ax;sim_imu[1]=ay;sim_imu[2]=az;sim_imu[6]=(int32_t)now_ms_value;
            if(rt && !(tinyrt_runtime_input_events(rt)&128u)) continue;
            result=cycle(rt,"motion",0,7,ax,ay,az);
        } else if (!strcmp(op,"gamepad")) {
            /* gamepad <buttons> <lx> <ly> <rx> <ry> <lt> <rt> <connected>，与宿主打包方式相同。 */
            int32_t v[7];uint32_t buttons;char *rest[5]={b,extra,NULL,NULL,NULL};
            for(unsigned i=2;i<5;i++)rest[i]=strtok(NULL," \t\r\n");
            int ok=parse_u32(a,&buttons) && !(buttons&0x00FF0000u);
            for(unsigned i=0;ok && i<7;i++) {
                char *token=i<5?rest[i]:strtok(NULL," \t\r\n");
                ok=parse_range(token,i<4?-127:0,i<4?127:(i<6?255:1),&v[i]);
            }
            if (!ok || strtok(NULL," \t\r\n")) { exit_code=usage();break; }
            if (rt && (!(policy.permissions&TINYRT_PERMISSION_INPUT) || !(tinyrt_runtime_input_events(rt)&256u))) continue;
            int32_t sticks=(int32_t)((uint32_t)(uint8_t)v[0]|(uint32_t)(uint8_t)v[1]<<8|(uint32_t)(uint8_t)v[2]<<16|(uint32_t)(uint8_t)v[3]<<24);
            result=cycle(rt,"gamepad",0,8,(int32_t)buttons,sticks,v[4]|v[5]<<8|v[6]<<16);
        } else if (!strcmp(op,"tick") && parse_u32(a,&x) && !b) {
            result=advance_mic(rt,x);
            if(result!=TINYRT_OK) {exit_code=1;break;}
            int32_t signed_now; memcpy(&signed_now,&x,sizeof(x));
            result=cycle(rt,"tick",0,2,0,0,signed_now);
        } else if (!strcmp(op,"touch") && parse_u32(a,&x) && parse_u32(b,&y) && !extra && x<466 && y<466) {
            result=cycle(rt,"touch",0,1,(int32_t)x,(int32_t)y,0);
        } else if ((!strcmp(op,"restart") || !strcmp(op,"reboot")) && !a) {
            if (!strcmp(op,"reboot")) {
                now_ms_value=0; tinyrt_frame_release(&frame);tinyrt_runtime_destroy(rt); rt=NULL;
                frame.count=0;last_frame_count=0;next_clock_deadline=UINT64_MAX;
            } else if (stop_instance(&rt,NULL)!=TINYRT_OK) { exit_code=1; break; }
            result=tinyrt_runtime_create(bytes,(uint32_t)length,&policy,&host,&rt);
            if (result==TINYRT_OK) result=cycle(rt,op,1,0,466,466,0);
            else emit("create",result,"Wasm reload rejected",0);
        } else if (!strcmp(op,"stop") && !a) {
            result=stop_instance(&rt,"stop");
        } else { exit_code=usage(); break; }
        if (result!=TINYRT_OK) { exit_code=1; break; }
    }
    if (ferror(stdin)) exit_code=2;
done:
    if (!exit_code && rt && stop_instance(&rt,NULL)!=TINYRT_OK) exit_code=1;
    tinyrt_runtime_destroy(rt); tinyrt_runtime_system_shutdown(); mic_replay_shutdown(); free(bytes);free(resources);
    if (tinyrt_runtime_memory_used()) exit_code=1;
    emit("shutdown",TINYRT_OK,"",0);
    return exit_code;
}
