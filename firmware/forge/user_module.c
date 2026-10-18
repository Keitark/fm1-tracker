#include "forge.h"
/* Replace this translation unit with generated user.c using --forge-user-module.
 * The fixed runtime still supplies USB/UBOOT, master volume and diagnostics. */
static void init(void){}
static void update(const fm1_forge_input *input,uint32_t now){(void)input;(void)now;}
static void render(const fm1_forge_frame *frame,unsigned y,unsigned rows,uint8_t *wire,unsigned bpp){
    (void)fm1_forge_render(frame,y,rows,wire,bpp);
}
static void audio(int32_t *stereo,unsigned frames){(void)stereo;(void)frames;}
static void shutdown(void){}
const fm1_forge_module fm1_forge_user_module={FM1_FORGE_ABI,"starter",init,update,render,audio,shutdown};
