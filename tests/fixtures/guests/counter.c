#include "guest-v1.h"
#ifndef COUNTER_VERSION
#define COUNTER_VERSION 1
#endif
static int32_t width,height,count;
static char number[12];
static uint32_t decimal(uint32_t n){char reverse[10];uint32_t i=0,j=0;do{reverse[i++]=(char)('0'+n%10);n/=10;}while(n);while(i)number[j++]=reverse[--i];return j;}
int32_t tinyrt_init(int32_t w,int32_t h){width=w;height=h;count=kv_get(0,0);if(count<0)count=0;return 0;}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){(void)x;(void)y;(void)arg;if(kind==TINYRT_TOUCH_RELEASE){if(count<2147483647)count++;return kv_set(0,count);}return 0;}
int32_t tinyrt_render(void){uint32_t n=decimal((uint32_t)count);
#if COUNTER_VERSION == 2
    draw_clear(0x102a43);draw_rect(30,100,width-60,height-200,0x176b87);draw_text(50,60,"COUNTER V2",10,0xffffff);
#else
    draw_clear(0x181818);draw_rect(30,100,width-60,height-200,0x345830);draw_text(50,60,"COUNTER V1",10,0xffffff);
#endif
    draw_text(60,height/2,number,n,0xffffff);draw_text(50,height-75,"Tap to add one",14,0xffffff);return 0;}
