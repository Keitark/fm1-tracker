/* Replace only the unsupported-FC fallback's first four bytes. Earlier CDB
   decode/negotiation remains untouched. All non-FC03 commands resume at the
   original instruction following the displaced subtraction. */
.section .patch, "ax"
.global fm1_status_patch
fm1_status_patch:
    goto fm1_status_dispatch

.section .text.status_dispatch, "ax"
.global fm1_status_dispatch
fm1_status_dispatch:
    if (r0 != 3) goto fm1_status_passthrough
    r0 = r4
    call fm1_loader_status
    goto original_response
fm1_status_passthrough:
    r1 = r0 + -0x85
    goto original_fc_resume
