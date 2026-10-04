#include "fm1_mdx.h"
#include <stdio.h>
#include <stdlib.h>
static fm1_mdx_player p;
extern const unsigned char fm1_demo_mdx[],fm1_demo_pdx[];
extern const size_t fm1_demo_mdx_size,fm1_demo_pdx_size;
static void word(FILE *f,unsigned v,unsigned n){while(n--){fputc(v&255,f);v>>=8;}}
int main(int argc,char **argv) {
    FILE *f;unsigned i,j;int16_t data[256];unsigned frames=FM1_MDX_RATE*10;
    if(argc!=2)return 2;
    if(fm1_mdx_load(&p,fm1_demo_mdx,fm1_demo_mdx_size,fm1_demo_pdx,fm1_demo_pdx_size))return 3;
    f=fopen(argv[1],"wb");if(!f)return 4;
    fwrite("RIFF",1,4,f);word(f,36+frames*4,4);fwrite("WAVEfmt ",1,8,f);word(f,16,4);word(f,1,2);word(f,2,2);word(f,FM1_MDX_RATE,4);word(f,FM1_MDX_RATE*4,4);word(f,4,2);word(f,16,2);fwrite("data",1,4,f);word(f,frames*4,4);
    for(i=0;i<frames;) {unsigned n=frames-i;if(n>128)n=128;if(fm1_mdx_render(&p,data,n)){fclose(f);return 5;}for(j=0;j<n*2;j++)word(f,(unsigned)(uint16_t)data[j],2);i+=n;}
    fclose(f);return 0;
}
