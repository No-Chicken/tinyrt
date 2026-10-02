#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures;
#define CHECK(x) do { checks++; if(!(x)){failures++;printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);} }while(0)
static tinyrt_package_policy_t policy={1,15,2,100000};
static int32_t saved;
static int32_t get(void *c,uint32_t k,int32_t f){(void)c;(void)k;(void)f;return saved;}
static tinyrt_status_t set(void *c,uint32_t k,int32_t v){(void)c;(void)k;saved=v;return TINYRT_OK;}
static uint32_t now(void *c){(void)c;return 123;}
static tinyrt_runtime_host_t host={NULL,get,set,now};
static const char *fixture_directory=FIXTURE_DIR;
static unsigned char *bytes;static uint32_t size;
static void fixture(const char *name){char path[512];snprintf(path,sizeof(path),"%s/%s.wasm",fixture_directory,name);FILE*f=fopen(path,"rb");CHECK(f!=NULL);if(!f)exit(2);fseek(f,0,SEEK_END);size=(uint32_t)ftell(f);rewind(f);free(bytes);bytes=malloc(size);CHECK(bytes!=NULL);CHECK(fread(bytes,1,size,f)==size);fclose(f);}
static tinyrt_status_t read_bytes(void*c,uint32_t o,void*b,uint32_t n){(void)c;if(o>size||n>size-o)return TINYRT_IO_ERROR;memcpy(b,bytes+o,n);return TINYRT_OK;}
static tinyrt_status_t io_fail(void*c,uint32_t o,void*b,uint32_t n){(void)c;(void)o;(void)b;(void)n;return TINYRT_IO_ERROR;}
static void rejects(void){const char* names[]={"unknown","wasi","nomax","shared","huge","ctor","post","start","badsig","malformed","bulkfill","bulkcopy","bulkinit","datadrop","tablecopy","tablefill"};tinyrt_store_io_t io={0};io.read=read_bytes;for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++){fixture(names[i]);CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_VERIFY_FAILED);}fixture("valid");CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_OK);io.read=io_fail;CHECK(tinyrt_runtime_validate_wasm(NULL,&io,0,size,&policy)==TINYRT_IO_ERROR);}
static void lifecycle(void){saved=0;size_t base=tinyrt_runtime_memory_used();for(int i=0;i<50;i++){fixture("valid");tinyrt_runtime_t*rt=NULL;CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&rt)==TINYRT_OK);if(!rt)continue;CHECK(tinyrt_runtime_memory_used()>base+65536);memset(bytes,0,size);CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);CHECK(tinyrt_runtime_event(rt,1,10,20,0)==TINYRT_OK);static tinyrt_frame_t frame;memset(&frame,0xa5,sizeof(frame));CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_OK);CHECK(frame.count==3);CHECK(frame.commands[0].rgb==0x102030);CHECK(frame.commands[1].w==i+1);CHECK(!strcmp(frame.commands[2].text,"COUNT"));CHECK(saved==i+1);tinyrt_runtime_destroy(rt);CHECK(tinyrt_runtime_memory_used()==base);}}
static void runtime_failures(void){const char*names[]={"spin","badkey","oob","overflow","noclear","badutf8"};for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++){fixture(names[i]);tinyrt_runtime_t*rt=NULL;CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&rt)==TINYRT_OK);if(!rt)continue;CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);static tinyrt_frame_t frame;memset(&frame,0xa5,sizeof(frame));if(i<2)CHECK(tinyrt_runtime_event(rt,1,10,20,0)==TINYRT_VERIFY_FAILED);else CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_VERIFY_FAILED&&frame.count==0);CHECK(tinyrt_runtime_last_error(rt)[0]!=0);CHECK(tinyrt_runtime_render(rt,&frame)==TINYRT_VERIFY_FAILED&&frame.count==0);tinyrt_runtime_destroy(rt);}}

static void examples(void){fixture_directory=EXAMPLE_DIR;const char*names[]={"counter-v1","counter-v2","color"};saved=7;uint32_t background=0;for(unsigned i=0;i<3;i++){fixture(names[i]);tinyrt_runtime_t*rt=NULL;CHECK(tinyrt_runtime_create(bytes,size,&policy,&host,&rt)==TINYRT_OK);if(!rt)continue;CHECK(tinyrt_runtime_init(rt,466,466)==TINYRT_OK);static tinyrt_frame_t f;CHECK(tinyrt_runtime_render(rt,&f)==TINYRT_OK);CHECK(f.count==(i==2?3u:5u));if(i==0){CHECK(!strcmp(f.commands[3].text,"7"));background=f.commands[0].rgb;}if(i==1){CHECK(!strcmp(f.commands[3].text,"8"));CHECK(f.commands[0].rgb!=background);}CHECK(tinyrt_runtime_event(rt,1,30,40,0)==TINYRT_OK);CHECK(tinyrt_runtime_render(rt,&f)==TINYRT_OK);if(i<2){CHECK(saved==(int32_t)(8+i));CHECK(!strcmp(f.commands[3].text,i==0?"8":"9"));}else CHECK(f.commands[0].rgb==0x9e2a2b);tinyrt_runtime_destroy(rt);}fixture_directory=FIXTURE_DIR;}

int main(void){CHECK(tinyrt_runtime_system_init()==TINYRT_OK);rejects();lifecycle();runtime_failures();fixture("valid");tinyrt_runtime_t*rt=NULL;tinyrt_package_policy_t p=policy;p.permissions=0;CHECK(tinyrt_runtime_create(bytes,size,&p,&host,&rt)==TINYRT_VERIFY_FAILED);CHECK(rt==NULL);examples();free(bytes);tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);printf("runtime checks=%d failures=%d\n",checks,failures);return failures?1:0;}
