/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * WCH SDI (single-wire debug) transport, ported from the vendor fork
 * (hathach/riscv-openocd-wch) for config compatibility: vendor configs do
 * 'transport select sdi' + 'sdi newtap ...'. In this tree the wlinke
 * adapter translates the RISC-V DTM registers to WCH-Link transfers at the
 * driver level, so sdi behaves exactly like jtag here - this transport
 * only provides the vendor command names.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "interface.h"

#include <helper/command.h>
#include <transport/transport.h>

COMMAND_HANDLER(handle_sdi_newtap_command)
{
	struct jtag_tap *tap;

	/*
	 * only need "basename" and "tap_type", but for backward compatibility
	 * ignore extra parameters
	 */
	if (CMD_ARGC < 2)
		return ERROR_COMMAND_SYNTAX_ERROR;

	tap = calloc(1, sizeof(*tap));
	if (!tap) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}

	tap->chip = strdup(CMD_ARGV[0]);
	tap->tapname = strdup(CMD_ARGV[1]);
	tap->dotted_name = alloc_printf("%s.%s", CMD_ARGV[0], CMD_ARGV[1]);
	if (!tap->chip || !tap->tapname || !tap->dotted_name) {
		LOG_ERROR("Out of memory");
		free(tap->dotted_name);
		free(tap->tapname);
		free(tap->chip);
		free(tap);
		return ERROR_FAIL;
	}

	LOG_DEBUG("Creating new sdi \"tap\", Chip: %s, Tap: %s, Dotted: %s",
			  tap->chip, tap->tapname, tap->dotted_name);

	/* The vendor fork ignored the trailing options entirely; the DTM
	 * bridge underneath needs a real IR length, so honor -irlen and
	 * default to the QingKe DTM's 5 bits otherwise. */
	tap->ir_length = 5;
	for (unsigned int i = 2; i + 1 < CMD_ARGC; i++) {
		if (strcmp(CMD_ARGV[i], "-irlen") == 0)
			COMMAND_PARSE_NUMBER(int, CMD_ARGV[i + 1], tap->ir_length);
	}

	/* default is enabled-after-reset */
	tap->enabled = true;

	jtag_tap_init(tap);
	return ERROR_OK;
}

static const struct command_registration sdi_transport_subcommand_handlers[] = {
	{
		.name = "newtap",
		.handler = handle_sdi_newtap_command,
		.mode = COMMAND_CONFIG,
		.help = "Create a new TAP instance named basename.tap_type, "
				"and appends it to the scan chain.",
		.usage = "basename tap_type",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration sdi_transport_command_handlers[] = {
	{
		.name = "sdi",
		.mode = COMMAND_ANY,
		.help = "perform sdi adapter actions",
		.usage = "",
		.chain = sdi_transport_subcommand_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

static int sdi_transport_select(struct command_context *cmd_ctx)
{
	LOG_DEBUG(__func__);

	return register_commands(cmd_ctx, NULL, sdi_transport_command_handlers);
}

static int sdi_transport_init(struct command_context *cmd_ctx)
{
	/* The jtag-transport init that normally deasserts and records the
	 * trst/srst state does not run under sdi; normalize it here so the
	 * scan queue (which backs the wlinke DTM bridge) is usable. */
	jtag_add_reset(0, 0);
	return jtag_execute_queue();
}

static struct transport sdi_transport = {
	.id = TRANSPORT_SDI,
	.select = sdi_transport_select,
	.init = sdi_transport_init,
};

static void sdi_constructor(void) __attribute__ ((constructor));
static void sdi_constructor(void)
{
	transport_register(&sdi_transport);
}

bool transport_is_sdi(void)
{
	return get_current_transport() == &sdi_transport;
}
