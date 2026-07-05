/* Human68k device driver header */

    .section    ".header","ax"

    .global devheader
    .extern reqheader
    .extern interrupt
    .extern g_param

devheader:
    .long   -1                  // link pointer
    .word   0x4000              // device type
    .long   strategy            // strategy routine entry point
    .long   interrupt_asm       // interrupt routine entry point
    .ascii  "\x02LOOPDRV"       // device driver name

    .long   g_param             // pointer to loopdrv parameters

strategy:
    move.l  %a5,reqheader
    rts

interrupt_asm:
    movem.l %d1-%d2/%a0-%a2/%a5,%sp@-
    bsr     interrupt
    movea.l reqheader,%a5
    move.b  %d0,%a5@(3)         // errl
    rol.w   #8,%d0
    move.b  %d0,%a5@(4)         // errh
    movem.l %sp@+,%d1-%d2/%a0-%a2/%a5
    rts

    .end
