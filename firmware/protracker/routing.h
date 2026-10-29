#ifndef FM1_TRACKER_ROUTING_H
#define FM1_TRACKER_ROUTING_H
#include <stdint.h>
/* At most one 64-frame DAC callback. SPK includes both stereo sides; ST
 * leaves all samples intact. USB capture is tapped before this operation. */
void fm1_tracker_speaker_fold(int32_t *, unsigned frames, unsigned stereo);
#endif
