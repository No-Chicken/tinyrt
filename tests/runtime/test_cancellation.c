#include "wasm_export.h"
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks,failures;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);} } while(0)
typedef struct { wasm_module_inst_t instance;wasm_exec_env_t env;HANDLE stop; } guard_t;
static DWORD WINAPI cancel_thread(void *arg) {
    guard_t *g=arg;
    if(WaitForSingleObject(g->stop,75)==WAIT_TIMEOUT) {
#if WASM_ENABLE_LOOP_POLL != 0
        wasm_runtime_request_exec_env_termination(g->env);
#else
        wasm_runtime_terminate(g->instance);
#endif
    }
    return 0;
}
static int32_t slow_import(wasm_exec_env_t env) {(void)env;Sleep(2);return 0;}
static NativeSymbol natives[]={{"now_ms",(void*)slow_import,"()i",NULL}};
static void *alloc(mem_alloc_usage_t usage,unsigned int size) {(void)usage;return malloc(size);}
static void *resize(mem_alloc_usage_t usage,bool mapped,void *p,unsigned int size) {(void)usage;return mapped?NULL:realloc(p,size);}
static void release(mem_alloc_usage_t usage,void *p) {(void)usage;free(p);}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    RuntimeInitArgs args={0};args.mem_alloc_type=Alloc_With_Allocator;
    args.mem_alloc_option.allocator.malloc_func=(void*)alloc;
    args.mem_alloc_option.allocator.realloc_func=(void*)resize;
    args.mem_alloc_option.allocator.free_func=(void*)release;
    CHECK(wasm_runtime_full_init(&args));
    CHECK(wasm_runtime_register_natives("tinyrt",natives,1));
    const char *names[]={"guard_pure","guard_import"};
    for(unsigned i=0;i<2;++i) {
        char path[1024],error[192];uint8_t bytes[8192];
        snprintf(path,sizeof(path),"%s/%s.wasm",FIXTURE_DIR,names[i]);
        FILE *f=fopen(path,"rb");if(!f)return 2;
        uint32_t size=(uint32_t)fread(bytes,1,sizeof(bytes),f);fclose(f);
        wasm_module_t module=wasm_runtime_load(bytes,size,error,sizeof(error));CHECK(module);if(!module)return 2;
        wasm_module_inst_t instance=wasm_runtime_instantiate(module,8192,0,error,sizeof(error));CHECK(instance);if(!instance)return 2;
        wasm_exec_env_t env=wasm_runtime_create_exec_env(instance,8192);CHECK(env);if(!env)return 2;
        guard_t guard={instance,env,CreateEvent(NULL,TRUE,FALSE,NULL)};CHECK(guard.stop);
        wasm_runtime_set_instruction_count_limit(env,INT_MAX);
        HANDLE thread=CreateThread(NULL,0,cancel_thread,&guard,0,NULL);CHECK(thread);
        LARGE_INTEGER begin,end,frequency;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&begin);
        printf("CANCELLATION begin %s\n",names[i]);fflush(stdout);
        uint32_t argv[1]={0};
        bool ok=wasm_runtime_call_wasm(env,wasm_runtime_lookup_function(instance,"tinyrt_render"),0,argv);
        SetEvent(guard.stop);CHECK(WaitForSingleObject(thread,INFINITE)==WAIT_OBJECT_0);
        QueryPerformanceCounter(&end);double elapsed=(double)(end.QuadPart-begin.QuadPart)*1000.0/(double)frequency.QuadPart;
        CHECK(!ok && wasm_runtime_get_exception(instance));CHECK(elapsed>=50.0 && elapsed<500.0);
#if WASM_ENABLE_LOOP_POLL != 0
        CHECK(wasm_runtime_is_exec_env_terminated(env));
#endif
        printf("CANCELLATION end %s elapsed_ms=%.3f\n",names[i],elapsed);
        CloseHandle(thread);CloseHandle(guard.stop);
        wasm_runtime_destroy_exec_env(env);wasm_runtime_deinstantiate(instance);wasm_runtime_unload(module);
    }
    wasm_runtime_destroy();printf("CANCELLATION checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
