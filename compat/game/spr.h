#ifndef SPR_H
#define SPR_H

/* Native replacement for the decomp's include/spr.h (compat/game comes first on the include
 * path): the scratchpad work area at 0x70002000, which the EE code loads with one `li`. The
 * native program maps the EE's address space, scratchpad included, at the guest addresses. */
static __inline__ void *spr_base(void)
{
    return (void *)0x70002000;
}

#endif
