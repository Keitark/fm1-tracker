#include "forge.h"
unsigned test_module_inits,test_module_updates,test_module_renders,test_module_audio,test_module_stops;
fm1_forge_input test_module_update_input;
fm1_forge_frame test_module_render_frame;
static void init(void){test_module_inits++;}
static void update(const fm1_forge_input *input,uint32_t now){test_module_update_input=*input;(void)now;test_module_updates++;}
static void render(const fm1_forge_frame *frame,unsigned y,unsigned rows,uint8_t *wire,unsigned bpp){test_module_render_frame=*frame;test_module_renders++;(void)fm1_forge_render(frame,y,rows,wire,bpp);}
static void audio(int32_t *out,unsigned frames){unsigned i;test_module_audio++;for(i=0;i<frames;i++){out[2*i]=INT32_MAX;out[2*i+1]=INT32_MIN;}}
static void shutdown(void){test_module_stops++;}
const fm1_forge_module fm1_forge_user_module={1,"test",init,update,render,audio,shutdown};
