#ifndef FM1_PROFILE_H
#define FM1_PROFILE_H
#include <stdint.h>
enum { FM1_PROF_OTHER, FM1_PROF_CPU, FM1_PROF_APU, FM1_PROF_PPU,
       FM1_PROF_VIDEO, FM1_PROF_LCD, FM1_PROF_FX, FM1_PROF_PCM,
       FM1_PROF_FRAME, FM1_PROF_COUNT };
/* Single NES task only. Exclusive elapsed wall time, including preemption.
   Never call from ISRs/USB task. Tick scale belongs to the supplied clock. */
typedef struct { uint32_t ticks[FM1_PROF_COUNT], wall, transitions; } fm1_profile_stats;
#ifdef FM1_NES_PROFILE
void fm1_profile_start(uint32_t (*clock)(void));
unsigned fm1_profile_enter(unsigned section);
void fm1_profile_snapshot(fm1_profile_stats *out);
#define FM1_PROFILE_BEGIN(section) unsigned fm1_profile_previous=fm1_profile_enter(section)
#define FM1_PROFILE_END() ((void)fm1_profile_enter(fm1_profile_previous))
#else
#define FM1_PROFILE_BEGIN(section) ((void)0)
#define FM1_PROFILE_END() ((void)0)
#endif
#endif
