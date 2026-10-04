#ifndef FM1_CLOCK_CONTRACT_H
#define FM1_CLOCK_CONTRACT_H
/* These bound reported SDK values; they do NOT program clocks or qualify
 * electrical frequency limits. Stock 010 and SDK early-clock code both have
 * a 480 MHz reporting branch. The previous 396 MHz gate rejected that branch.
 * Keep the conservative peripheral-bus bound until its route is qualified. */
static int fm1_clock_report_valid(int sys,int lsb) {
    return sys>=24000000 && sys<=480000000 && lsb>0 && lsb<=80000000;
}
#endif
