#include "fm1_lcd_pack.h"
static inline void pair(uint8_t *d,unsigned a,unsigned b) {
    d[0]=(uint8_t)(((a>>8)&0xf0)|((a>>7)&15));
    d[1]=(uint8_t)(((a<<3)&0xf0)|((b>>12)&15));
    d[2]=(uint8_t)(((b>>3)&0xf0)|((b>>1)&15));
}
#if defined(__GNUC__)
__attribute__((noinline,used))
#endif
void fm1_lcd_pack_native_rgb444(uint8_t *dst,const uint16_t *src,unsigned rows,unsigned crop) {
    unsigned y,g,n;
    for(y=0;y<rows;++y) {
        const uint16_t *p=src+y*256u;
        if(crop) {
            p+=8;
            for(n=0;n<120;++n){pair(dst,p[0],p[1]);dst+=3;p+=2;}
        } else {
            /* 32 source pixels ->30 destination pixels, omitting15 and31.
             * Handle the pair straddling the first omission explicitly.
             * Equivalent to floor(x*256/240), without divide/temporary bytes. */
            for(g=0;g<8;++g) {
                for(n=0;n<7;++n){pair(dst,p[0],p[1]);dst+=3;p+=2;}
                pair(dst,p[0],p[2]);dst+=3;p+=3;
                for(n=0;n<7;++n){pair(dst,p[0],p[1]);dst+=3;p+=2;}
                ++p;
            }
        }
    }
}
