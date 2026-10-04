#ifndef FM1_PERIPHERAL_LOGIC_H
#define FM1_PERIPHERAL_LOGIC_H
#include <stdint.h>
#define FM1_TONE_FRAMES 132300u
typedef struct { uint8_t previous[7], candidate[7], valid; uint32_t invalid[7]; int32_t count[7]; } fm1_encoders;
extern const char *const fm1_encoder_names[7];
extern const uint8_t fm1_encoder_contacts[14];
void fm1_encoders_sample(fm1_encoders *,const uint8_t rows[11]);
/* Capture each completed scanner snapshot once, independently of UI polling. */
void fm1_encoders_capture(fm1_encoders *,uint32_t *last_sequence,
                         uint32_t sequence,const uint8_t rows[11]);
/* Four contact edges per click; deliver at most two clicks per poll without
 * discarding a larger pending movement. previous/remainder start at zero. */
int fm1_encoder_detents(int32_t count,int32_t *previous,int32_t *remainder);
int32_t fm1_test_sample(uint32_t frame);
#endif
