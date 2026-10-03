/* Desktop validation of raw Wasm. No package trust, BLE, or disk persistence. */
#include "tinyrt_runtime.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

typedef struct { uint32_t mask; int32_t values[16]; } saved_t;
static saved_t saved;
static uint32_t now_ms_value, commits;
static uint32_t clock_interval_value=TINYRT_DEFAULT_CLOCK_INTERVAL_MS;
static uint64_t next_clock_deadline=UINT64_MAX;
static tinyrt_frame_t frame;
static uint32_t last_frame_count;
static bool capture_pixels;
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
    (void)ctx;(void)rate;
    return offset>resources_size || length>resources_size-offset ? TINYRT_INVALID_ARGUMENT : TINYRT_OK;
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
    return key < 16 && (saved.mask & (1u << key)) ? saved.values[key] : fallback;
}
static tinyrt_status_t set_value(void *ctx, uint32_t key, int32_t value) {
    (void)ctx;
    if (key >= 16) return TINYRT_INVALID_ARGUMENT;
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
    for (uint32_t k=0;k<16;++k) {
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
            json_string(c->text); putchar('}');
        }
    }
    printf("]");
    if(capture_pixels){printf(",\"pixels_hex\":\"");for(uint32_t i=0;i<pixel_bytes;++i)printf("%02x",frame.pixels[i]);putchar('"');}
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
static tinyrt_status_t cycle(tinyrt_runtime_t *rt, const char *phase, int init,
                            int32_t kind, int32_t x, int32_t y, int32_t arg) {
    saved_t before=saved; double start=milliseconds();
    tinyrt_status_t result=init ? tinyrt_runtime_init(rt,x,y) : tinyrt_runtime_event(rt,kind,x,y,arg);
    if (result==TINYRT_OK) result=tinyrt_runtime_render(rt,&frame);
    if (result==TINYRT_OK && frame.count) last_frame_count=frame.count;
    if (result!=TINYRT_OK) saved=before;
    else if (memcmp(&saved,&before,sizeof(saved))) ++commits;
    clock_interval_value=tinyrt_runtime_clock_interval_ms(rt);
    if (result==TINYRT_OK && (init || kind==2))
        next_clock_deadline=(uint64_t)now_ms_value+clock_interval_value;
    emit(phase,result,result==TINYRT_OK ? "" : tinyrt_runtime_last_error(rt),milliseconds()-start);
    return result;
}
static tinyrt_status_t stop_instance(tinyrt_runtime_t **rt, const char *phase) {
    saved_t before=saved; double start=milliseconds(); char error[192]="";
    tinyrt_status_t result=*rt ? tinyrt_runtime_stop(*rt) : TINYRT_OK;
    if (result!=TINYRT_OK) {
        saved=before;
        snprintf(error,sizeof(error),"%s",tinyrt_runtime_last_error(*rt));
    } else if (memcmp(&saved,&before,sizeof(saved))) ++commits;
    tinyrt_runtime_destroy(*rt); *rt=NULL; frame.count=0;last_frame_count=0;
    next_clock_deadline=UINT64_MAX;
    clock_interval_value=TINYRT_DEFAULT_CLOCK_INTERVAL_MS;
    if (phase || result!=TINYRT_OK) emit(phase ? phase : "stop",result,error,milliseconds()-start);
    return result;
}
static int usage(void) {
    fprintf(stderr,"Usage: tinyrt-run app.wasm [--pages 1..16] [--budget 1..100000] [--permissions 0..31] [--assets resources.bin]\n"
        "stdin: advance <0..60000_ms> | capture | pointer <kind> <x> <y> | key 1 <0|1> | motion <ax_mg> <ay_mg> <az_mg>\n"
        "       time <uint32_ms> | tick <uint32_ms> | touch <x> <y> | stop | restart | reboot\n"
        "advance uses the guest clock interval and monotonic virtual time; capture does not run callbacks.\n"
        "KV is RAM-only. stop/restart/EOF stop normally; reboot forces teardown.\n");
    return 2;
}
int main(int argc, char **argv) {
    if (argc<2 || !strcmp(argv[1],"--help")) return usage();
    tinyrt_package_policy_t policy={1,15,2,100000};
    for (int i=2;i<argc;i+=2) {
        uint32_t value;
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
        if (!strcmp(argv[i],"--pages") && value>=1 && value<=16) policy.max_memory_pages=value;
        else if (!strcmp(argv[i],"--budget") && value>=1 && value<=100000) policy.instruction_budget=value;
        else if (!strcmp(argv[i],"--permissions") && value<=31) policy.permissions=value;
        else return usage();
    }
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
    tinyrt_runtime_host_t host={NULL,get_value,set_value,now,read_resource,play_audio};
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
        if (!strcmp(op,"capture") && !a) {
            uint32_t count_before=frame.count;frame.count=last_frame_count;
            capture_pixels=true;emit("capture",TINYRT_OK,"",0);capture_pixels=false;frame.count=count_before;
            continue;
        } else if (!strcmp(op,"advance") && parse_u32(a,&x) && !b && rt && x<=60000 && x>=now_ms_value) {
            /* Match svc_apps event_owner: automatic delivery respects the
             * manifest. Explicit tick remains a strict runtime diagnostic. */
            while ((policy.permissions&TINYRT_PERMISSION_CLOCK) && next_clock_deadline<=x) {
                now_ms_value=(uint32_t)next_clock_deadline;
                result=cycle(rt,"tick",0,2,0,0,(int32_t)now_ms_value);
                if (result!=TINYRT_OK) break;
            }
            if (result==TINYRT_OK) now_ms_value=x;
        } else if (!strcmp(op,"time") && parse_u32(a,&x) && !b) {
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
            if (rt && (!(policy.permissions&TINYRT_PERMISSION_INPUT) || !(tinyrt_runtime_input_events(rt)&128u))) continue;
            result=cycle(rt,"motion",0,7,ax,ay,az);
        } else if (!strcmp(op,"tick") && parse_u32(a,&x) && !b) {
            now_ms_value=x; int32_t signed_now; memcpy(&signed_now,&x,sizeof(x));
            result=cycle(rt,"tick",0,2,0,0,signed_now);
        } else if (!strcmp(op,"touch") && parse_u32(a,&x) && parse_u32(b,&y) && !extra && x<466 && y<466) {
            result=cycle(rt,"touch",0,1,(int32_t)x,(int32_t)y,0);
        } else if ((!strcmp(op,"restart") || !strcmp(op,"reboot")) && !a) {
            if (!strcmp(op,"reboot")) {
                now_ms_value=0; tinyrt_runtime_destroy(rt); rt=NULL;
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
    tinyrt_runtime_destroy(rt); tinyrt_runtime_system_shutdown(); free(bytes);free(resources);
    if (tinyrt_runtime_memory_used()) exit_code=1;
    emit("shutdown",TINYRT_OK,"",0);
    return exit_code;
}
