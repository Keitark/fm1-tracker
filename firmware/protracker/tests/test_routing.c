#include "routing.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
int main(void){
    int32_t original[12]={6400,0,0,6400,3200,-6400,INT32_MAX,INT32_MAX,
        INT32_MIN,INT32_MIN,INT32_MAX,INT32_MIN},pcm[12];unsigned i;
    memcpy(pcm,original,sizeof(pcm));fm1_tracker_speaker_fold(pcm,6,1);
    assert(!memcmp(pcm,original,sizeof(pcm)));
    fm1_tracker_speaker_fold(pcm,6,0);
    for(i=0;i<6;i++){
        assert(pcm[2*i]==pcm[2*i+1]);
        assert(pcm[2*i]==(int32_t)(((int64_t)original[2*i]+original[2*i+1])/2));
    }
    assert(pcm[0]==3200 && pcm[2]==3200 && pcm[4]==-1600);
    memcpy(pcm,original,sizeof(pcm));fm1_tracker_speaker_fold(pcm,65,0);
    assert(!memcmp(pcm,original,sizeof(pcm)));fm1_tracker_speaker_fold(0,64,0);
    puts("SPK combines both sides; ST exact; integer extremes and frame bound passed");return 0;
}
