/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * WCH-Link/LinkE adapter exports used by the WCH flash drivers.
 * The probe's proprietary protocol handles erase/program directly, so the
 * flash drivers talk to the adapter rather than poking flash controller
 * registers through the target.
 */

#ifndef OPENOCD_JTAG_DRIVERS_WLINKE_H
#define OPENOCD_JTAG_DRIVERS_WLINKE_H

#include <stdbool.h>
#include <stdint.h>

extern unsigned char riscvchip;
extern unsigned int chip_type;
extern unsigned long wlink_address;
extern bool noloadflag;
extern bool pageerase;
/* number of completed flash write passes; defined in flash/nor/wchriscv.c */
extern int writeloop;

void wlink_ramcodewrite(uint8_t *buffer, int size);
void wlink_getromram(uint32_t *rom, uint32_t *ram);
void wlink_reset(void);
void wlink_quitreset(void);
void wlink_softreset(void);
void wlink_clean(void);
void wlink_chip_reset(void);
int wlink_flash_protect(bool stat);
int wlink_ready_write(uint32_t address, uint32_t count);
void wlink_endprogram(void);
int wlink_fastprogram(uint8_t *buffer, int packsize);
int wlink_erase(void);
void wlink_endprocess(void);
void wlink_disabledebug(void);
int wlink_write(const uint8_t *buffer, uint32_t offset, uint32_t count);
int wlnik_protect_check(void);

#endif /* OPENOCD_JTAG_DRIVERS_WLINKE_H */
