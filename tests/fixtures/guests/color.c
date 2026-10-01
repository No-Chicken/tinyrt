#include "guest-v1.h"
static int32_t width,height,index;
int32_t tinyrt_init(int32_t w,int32_t h){width=w;height=h;index=0;return 0;}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){(void)x;(void)y;(void)arg;if(kind==TINYRT_TOUCH_RELEASE)index=(index+1)%3;return 0;}
int32_t tinyrt_render(void){uint32_t colors[3]={0x386641,0x9e2a2b,0x244d86};draw_clear(colors[index]);draw_rect(30,100,width-60,height-200,0x202020);draw_text(50,height/2,"COLOR TAP",9,0xffffff);return 0;}
