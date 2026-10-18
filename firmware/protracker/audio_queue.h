#ifndef FM1_TRACKER_AUDIO_QUEUE_H
#define FM1_TRACKER_AUDIO_QUEUE_H
#define FM1_PCM_RING_FRAMES 1536u
/* Physical slots are independent of the unsigned producer/consumer counters.
 * In particular, wrapping a uint32 counter must not change a modulo-1536
 * physical position. These helpers are shared with the queue regression. */
static inline unsigned fm1_pcm_next_slot(unsigned slot){
    return slot+1u==FM1_PCM_RING_FRAMES?0u:slot+1u;
}
#endif
