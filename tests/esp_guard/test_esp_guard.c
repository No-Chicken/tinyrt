#include "tinyrt_esp_execution_guard.h"
#include "esp_timer.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cancellations,joins,deletions,recoveries;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line%d %s\n",__LINE__,#x);exit(1);}} while(0)
struct timer_stub {esp_timer_create_args_t args;uint64_t deadline;HANDLE running;int armed;};
static struct timer_stub *timer;
static struct timer_stub *recovery_timer;
static struct timer_stub *join_order[8];
static int fail_create,fail_start;
int64_t esp_timer_get_time(void) {return 123000;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    if(fail_create)return ESP_FAIL;
    struct timer_stub *created=calloc(1,sizeof(*created));CHECK(created);created->args=*args;*out=created;
    if(!strcmp(args->name,"tinyrt_recover"))recovery_timer=created;else timer=created;
    return ESP_OK;
}
esp_err_t esp_timer_start_once_at(esp_timer_handle_t t,uint64_t deadline) {
    if(fail_start)return ESP_FAIL;
    CHECK(!t->armed);t->armed=1;t->deadline=deadline;return ESP_OK;
}
esp_err_t esp_timer_stop_blocking(esp_timer_handle_t t,uint32_t ticks) {
    CHECK(ticks==0xffffffffu);join_order[joins++]=t;t->armed=0;
    if(t->running){CHECK(WaitForSingleObject(t->running,INFINITE)==WAIT_OBJECT_0);CloseHandle(t->running);t->running=NULL;}
    return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t t) {
    CHECK(!t->armed&&!t->running);++deletions;free(t);return ESP_OK;
}
static DWORD WINAPI dispatch(void *ctx) {struct timer_stub *t=ctx;Sleep(20);t->args.callback(t->args.arg);return 0;}
static void cancel(void *ctx) {CHECK(ctx==&cancellations);++cancellations;}
static void recover(void *ctx) {CHECK(ctx==&recoveries);++recoveries;}
int main(void) {
    tinyrt_esp_execution_guard_t *g=NULL;
    CHECK(tinyrt_esp_execution_guard_create(0,&g)==TINYRT_INVALID_ARGUMENT&&!g);
    CHECK(tinyrt_esp_execution_guard_create(3001,&g)==TINYRT_INVALID_ARGUMENT&&!g);
    fail_create=1;CHECK(tinyrt_esp_execution_guard_create(3000,&g)==TINYRT_NO_MEMORY&&!g);fail_create=0;
    CHECK(tinyrt_esp_execution_guard_create(3000,&g)==TINYRT_OK&&g);
    const tinyrt_execution_guard_t *config=tinyrt_esp_execution_guard_config(g);
    CHECK(config&&config->timeout_ms==3000);
#ifdef TINYRT_ESP_RECOVERY_VERSION
    CHECK(tinyrt_esp_execution_guard_set_recovery(g,recover,&recoveries,499)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_esp_execution_guard_set_recovery(g,recover,&recoveries,2001)==TINYRT_INVALID_ARGUMENT);
    CHECK(tinyrt_esp_execution_guard_set_recovery(g,recover,&recoveries,1000)==TINYRT_OK);
#else
    (void)recover;
#endif
    fail_start=1;CHECK(config->arm(config->ctx,cancel,&cancellations,3000)==TINYRT_IO_ERROR);fail_start=0;
    CHECK(config->arm(config->ctx,cancel,&cancellations,3000)==TINYRT_OK);
    CHECK(timer->deadline==3123000);
    CHECK(config->arm(config->ctx,cancel,&cancellations,3000)==TINYRT_BUSY);
    timer->running=CreateThread(NULL,0,dispatch,timer,0,NULL);CHECK(timer->running);
    config->disarm(config->ctx);CHECK(cancellations==1&&!timer->running);
    CHECK(recovery_timer&&recovery_timer->deadline==1123000&&!recovery_timer->armed&&recoveries==0);
    CHECK(joins==2&&join_order[0]==timer&&join_order[1]==recovery_timer);
    CHECK(config->arm(config->ctx,cancel,&cancellations,3000)==TINYRT_OK);
    timer->args.callback(timer->args.arg);
    CHECK(cancellations==2&&recovery_timer->armed);
    recovery_timer->args.callback(recovery_timer->args.arg);CHECK(recoveries==1);
    config->disarm(config->ctx);CHECK(joins==4);
    CHECK(config->arm(config->ctx,cancel,&cancellations,3000)==TINYRT_OK);
    tinyrt_esp_execution_guard_destroy(g);CHECK(deletions==2&&joins==6&&cancellations==2&&recoveries==1);
    CHECK(tinyrt_esp_execution_guard_config(NULL)==NULL);tinyrt_esp_execution_guard_destroy(NULL);
    printf("ESP_GUARD checks=%u PASS\n",checks);return 0;
}
