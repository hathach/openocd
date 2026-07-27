/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * 'wch_riscv' target type for vendor-fork config compatibility
 * (hathach/riscv-openocd-wch does 'target create ... wch_riscv').
 *
 * It is mainline's riscv target with the reset hooks replaced: the QingKe
 * debug module does not survive a standard ndmreset over the single-wire
 * link, so resets go through the WCH-Link probe protocol instead
 * (reset+halt-hold or reset+run), after which the probe re-attaches the
 * debug module. This is the same behavior the vendor fork's own target
 * implemented with wlink calls.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <helper/log.h>
#include <target/target.h>
#include <target/target_type.h>
#include <jtag/drivers/wlinke.h>

struct target_type wch_riscv_target;

static int wch_riscv_assert_reset(struct target *target)
{
	if (target->reset_halt)
		wlink_reset();      /* probe: reset chip, hold halted under debug */
	else
		wlink_quitreset();  /* probe: reset chip, let it run */
	target->state = TARGET_RESET;
	return ERROR_OK;
}

static int wch_riscv_deassert_reset(struct target *target)
{
	target->state = target->reset_halt ? TARGET_HALTED : TARGET_RUNNING;
	return ERROR_OK;
}

static void wch_riscv_constructor(void) __attribute__ ((constructor));
static void wch_riscv_constructor(void)
{
	wch_riscv_target = riscv_target;
	wch_riscv_target.name = "wch_riscv";
	wch_riscv_target.assert_reset = wch_riscv_assert_reset;
	wch_riscv_target.deassert_reset = wch_riscv_deassert_reset;
}
