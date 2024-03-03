/* Human68k device driver header */

    .text

    .extern reqheader
    .extern interrupt
    .extern g_param

devheader:
    .long   -1                  // link pointer
    .word   0x4000              // device type
    .long   strategy            // strategy routine entry point
    .long   interrupt_asm       // interrupt routine entry point
    .ascii  "\x01LOOPDRV"       // device driver name

    .long   g_param             // pointer to loopdrv parameters

strategy:
    move.l  %a5,reqheader
    rts

interrupt_asm:
    movem.l %d1-%d2/%a0-%a2/%a5,%sp@-
    bsr     interrupt
    movem.l %sp@+,%d1-%d2/%a0-%a2/%a5
    rts

    .end
