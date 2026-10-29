#ifndef FM1_TRACKER_BUILD_IDENTITY_H
#define FM1_TRACKER_BUILD_IDENTITY_H

#include <stddef.h>
#include <stdint.h>

#define FM1_TRACKER_BUILD_ID_BYTES 32u
#define FM1_TRACKER_FWINFO_NONCE_BYTES 16u
/* Exact 119-byte wire response followed by its terminating NUL. */
#define FM1_TRACKER_FWINFO_RESPONSE_BYTES 120u

/* Defined by the offline target builder; no device-specific material. */
extern const uint8_t fm1_tracker_build_id[FM1_TRACKER_BUILD_ID_BYTES];

/* The transport has already removed LF. Accept exactly FWINFO + one space +
 * 32 lowercase hex characters. Return one on success; all invalid arguments
 * return zero without changing the response buffer. No application state or
 * hardware is consulted or changed. */
int fm1_tracker_fwinfo(const char *command, char *response, size_t capacity);

#endif
