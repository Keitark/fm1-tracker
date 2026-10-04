/* Cold-built helper only: retire optional FC40..48 commands before reusing
   their handler storage. Do not install this into an executing helper. */
.section .route, "ax"
.global fm1_retire_optional_fc
fm1_retire_optional_fc:
    goto original_fc_fallback
