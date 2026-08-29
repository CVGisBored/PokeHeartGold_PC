/*
 * Reference reconstruction of the first ARM9 startup sequence at 0x02000800.
 * This is NOT linked into the PC port. It documents what the DS bootstrap does
 * so native initialization can replace it rather than emulate it.
 */
#include <stdint.h>

typedef void (*Fn0)(void);

static inline void ds_wait_for_aux_sync(void) {
    volatile uint16_t *sync = (volatile uint16_t *)0x04000206u;
    while (*sync != 0) { }
}

/* Calls and symbol names are intentionally conservative until matched against
 * the community decompilation/symbol map. The raw ARM branch destinations are
 * preserved in comments for auditability. */
void arm9_boot_reference(void) {
    volatile uint32_t *reg_04000208 = (volatile uint32_t *)0x04000208u;
    *reg_04000208 = 0x04000000u;
    ds_wait_for_aux_sync();

    /* 0x02000814 -> BL 0x02000AB0: low-level CPU/cache/TCM setup. */
    ((Fn0)(uintptr_t)0x02000AB0u)();

    /* 0x02000818..0x02000864 changes CPSR modes and establishes IRQ/SVC/user
       stacks. Those operations have no native-PC equivalent and are replaced
       by the host OS thread stack. */

    /* 0x02000868 onward clears runtime regions and performs cache maintenance.
       The PC build replaces this with normal C/C++ zero-initialization. */
}
