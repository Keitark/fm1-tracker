#include "routing.h"
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline,used))
#endif
void fm1_tracker_speaker_fold(int32_t *pcm,unsigned frames,unsigned stereo){
    unsigned i;
    if(!pcm || frames>64 || stereo)return;
    for(i=0;i<frames;i++){
        int32_t mono=(int32_t)(((int64_t)pcm[i*2]+pcm[i*2+1])/2);
        pcm[i*2]=pcm[i*2+1]=mono;
    }
}
