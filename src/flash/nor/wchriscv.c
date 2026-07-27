/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * WCH RISC-V flash driver: CH32V103X CH32V20X CH32V30X CH56X CH57X CH58X.
 * Taken from hathach/riscv-openocd-wch (originally the WCH openocd fork)
 * with only the mechanical changes needed for mainline: erase/protect
 * signatures, extern declarations moved to jtag/drivers/wlinke.h, and the
 * fork's flash/nor/tcl.c protect-check patch relocated into the driver's
 * protect_check op (vendor logic, driver-local placement).
 *
 * Erase/program go through the WCH-Link probe's proprietary protocol
 * (wlink_* in jtag/drivers/wlinke.c), not through memory-mapped FLC
 * registers, so this driver is only usable with the wlinke adapter.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <target/algorithm.h>
#include <target/target.h>
#include <jtag/drivers/wlinke.h>

int writeloop = 0;
static bool flash_unfreeze = false;

struct ch32vx_options
{
	uint8_t rdp;
	uint8_t user;
	uint16_t data;
	uint32_t protection;
};

struct ch32vx_flash_bank
{
	struct ch32vx_options option_bytes;
	int ppage_size;
	int probed;

	bool has_dual_banks;
	bool can_load_options;
	uint32_t register_base;
	uint8_t default_rdp;
	int user_data_offset;
	int option_offset;
	uint32_t user_bank_size;
};

FLASH_BANK_COMMAND_HANDLER(ch32vx_flash_bank_command)
{
	struct ch32vx_flash_bank *ch32vx_info;

	if (CMD_ARGC < 6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	ch32vx_info = malloc(sizeof(struct ch32vx_flash_bank));

	bank->driver_priv = ch32vx_info;
	ch32vx_info->probed = 0;
	ch32vx_info->has_dual_banks = false;
	ch32vx_info->can_load_options = false;
	ch32vx_info->user_bank_size = bank->size;

	return ERROR_OK;
}
static int ch32x_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{

	if ((riscvchip == 1) || (riscvchip == 5) || (riscvchip == 6) || (riscvchip == 9) || (riscvchip ==0x0c)||(riscvchip==0x0e))
	{
		int retval = wlink_flash_protect(set);
		if (retval == ERROR_OK)
		{
			if (set)
				LOG_INFO("Success to Enable Read-Protect");
			else
				LOG_INFO("Success to Disable Read-Protect");
			return ERROR_OK;
		}
		else
		{
			LOG_ERROR("Operation Failed");
			return ERROR_FAIL;
		}
	}
	else
	{
		LOG_ERROR("This chip do not support function");
		return ERROR_FAIL;
	}
}

/* the fork patched this logic into flash/nor/tcl.c's protect_check command;
 * same logic, kept driver-local here */
static int ch32vx_protect_check(struct flash_bank *bank)
{
	if (riscvchip == 1)
		wlink_softreset();
	if ((riscvchip == 1) || (riscvchip == 5) || (riscvchip == 6) || (riscvchip == 9) || (riscvchip == 0x0c) || (riscvchip == 0x0e))
	{
		int retval = wlnik_protect_check();
		if (retval == 4)
			LOG_INFO("Code Read-Protect Status Enable");
		else
			LOG_INFO("Code Read-Protect status Disable");
		return ERROR_OK;
	}
	else
	{
		LOG_ERROR("This chip do not support function");
		return ERROR_FAIL;
	}
}

static int ch32vx_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
	if (pageerase)
		return ERROR_OK;
	if ((riscvchip == 5) || (riscvchip == 6) || (riscvchip == 9)|| (riscvchip == 0x0c)||(riscvchip==0x0e))
	{
		int retval = wlnik_protect_check();
		if (retval == 4)
		{
			LOG_ERROR("Read-Protect Status Currently Enabled");
			return ERROR_FAIL;
		}
	}
	if (noloadflag)
		return ERROR_OK;

	int ret = wlink_erase();
	target_halt(bank->target);
	if (ret)
		return ERROR_OK;
	else
		return ERROR_FAIL;
	return ERROR_OK;
}


static int ch32vx_write(struct flash_bank *bank, const uint8_t *buffer,
						uint32_t offset, uint32_t count)
{

	struct target *target = bank->target;
	if (((riscvchip == 5) || (riscvchip == 6) || (riscvchip == 9)|| (riscvchip == 0x0c)||(riscvchip==0x0e)) && (writeloop==0))
	{
		int retval = wlnik_protect_check();
		if (retval == 4)
		{
			LOG_ERROR("Read-Protect Status Currently Enabled");
			return ERROR_FAIL;
		}
	}
	if (noloadflag)
		return ERROR_OK;

	if(writeloop)
		wlink_clean();
	int ret = 0;
	int mod = offset % 256;
	if (mod)
	{
		if (offset < 256)
			offset = 0;
		else
			offset -= mod;
		uint8_t *buffer1;
		uint8_t *buffer2;
		buffer1 = malloc(count + mod);
		buffer2 = malloc(mod);
		target_read_memory(bank->target, offset, 1, mod, buffer2);
		memcpy(buffer1, buffer2, mod);
		memcpy(&buffer1[mod], buffer, count);
		ret = wlink_write(buffer1, offset, count + mod);
	}
	else
	{
		target_halt(target);
		ret = wlink_write(buffer, offset, count);
	}
	wlink_chip_reset();
	writeloop++;
	return ret;
}

static int ch32vx_get_device_id(struct flash_bank *bank, uint32_t *device_id)
{
	if ((riscvchip != 0x02) && (riscvchip != 0x03)&& (riscvchip != 0x07)&& (riscvchip != 0x0b))
	{
		struct target *target = bank->target;
		int retval = target_read_u32(target, 0x1ffff7e8, device_id);
		if (retval != ERROR_OK)
			return retval;
	}
	return ERROR_OK;
}

static int ch32vx_get_flash_size(struct flash_bank *bank, uint32_t *flash_size_in_kb)
{

	struct target *target = bank->target;
	if(riscvchip == 0x09)
	{
			*flash_size_in_kb = 0x7fffe;
		return ERROR_OK;


	}
	if ((riscvchip == 0x02) || (riscvchip == 0x03) || (riscvchip == 0x07)|| (riscvchip == 0x0b))
	{
		if((chip_type ==0x71000000) || (chip_type ==0x81000000) || (chip_type ==0x91000000))
				*flash_size_in_kb = 192;
		else
			*flash_size_in_kb = 448;
		return ERROR_OK;
	}
	if (riscvchip == 0x0c)
	{
		if(chip_type==0x03570601)
			*flash_size_in_kb = 48;
		*flash_size_in_kb = 62;
		return ERROR_OK;
	}
	if (riscvchip == 0x0e)
	{
		if(chip_type==0x10370700)
			*flash_size_in_kb = 32;
		*flash_size_in_kb = 64;
		return ERROR_OK;
	}
	/* vendor code passes flash_size_in_kb (uint32_t *) straight to
	 * target_read_u16; go through a u16 so mainline compiles */
	uint16_t size_kb = 0;
	int retval = target_read_u16(target, 0x1ffff7e0, &size_kb);
	if (retval != ERROR_OK)
		return retval;
	*flash_size_in_kb = size_kb;
	return ERROR_OK;
}

static int ch32vx_probe(struct flash_bank *bank)
{
	struct ch32vx_flash_bank *ch32vx_info = bank->driver_priv;
	uint16_t delfault_max_flash_size = 512;
	uint32_t flash_size_in_kb = 0;
	uint32_t device_id = 0;
	uint32_t rom = 0;
	uint32_t ram = 0;
	int page_size;
	uint32_t base_address = (uint32_t)wlink_address;
	ch32vx_info->probed = 0;

	/* read ch32 device id register */
	int retval = ch32vx_get_device_id(bank, &device_id);
	if (retval != ERROR_OK)
		return retval;
	if (device_id)
		LOG_INFO("device id = 0x%08" PRIx32 "", device_id);
	page_size = 1024;
	ch32vx_info->ppage_size = 4;

	/* get flash size from target. */
	retval = ch32vx_get_flash_size(bank, &flash_size_in_kb);

	if ((flash_size_in_kb)&&(!flash_unfreeze)&&(riscvchip !=0x09))
		LOG_INFO("flash size = %dkbytes", flash_size_in_kb);
	else
		flash_size_in_kb = delfault_max_flash_size;
	if ((riscvchip == 0x05) || (riscvchip == 0x06))
	{
		wlink_getromram(&rom, &ram);
		if ((rom != 0) && (ram != 0))
			LOG_INFO("ROM %d kbytes RAM %d kbytes", rom, ram);
	}
	// /* calculate numbers of pages */
	int num_pages = flash_size_in_kb * 1024 / page_size;
	bank->base = base_address;
	bank->size = (num_pages * page_size);
	bank->num_sectors = num_pages;
	bank->sectors = alloc_block_array(0, page_size, num_pages);
	ch32vx_info->probed = 1;

	return ERROR_OK;
}

static int ch32vx_auto_probe(struct flash_bank *bank)
{

	struct ch32vx_flash_bank *ch32vx_info = bank->driver_priv;
	if (ch32vx_info->probed)
		return ERROR_OK;
	return ch32vx_probe(bank);
}

COMMAND_HANDLER(ch32vx_handle_unfreeze_command)
{

	flash_unfreeze=true;
	return ERROR_OK;
}


static const struct command_registration ch32vx_exec_command_handlers[] = {
	{
		.name = "unfreeze",
		.handler = ch32vx_handle_unfreeze_command,
		.mode = COMMAND_EXEC,
		.usage = "",
		.help = "unfreeze entire flash device.",
	},
	COMMAND_REGISTRATION_DONE
};
static const struct command_registration ch32vx_command_handlers[] = {
	{
		.name = "wch_riscv",
		.mode = COMMAND_ANY,
		.help = "wch_riscv flash command group",
		.usage = "",
		.chain = ch32vx_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE};

const struct flash_driver wch_riscv_flash = {
	.name = "wch_riscv",
	.commands = ch32vx_command_handlers,
	.flash_bank_command = ch32vx_flash_bank_command,
	.erase = ch32vx_erase,
	.protect = ch32x_protect,
	.write = ch32vx_write,
	.read = default_flash_read,
	.probe = ch32vx_probe,
	.auto_probe = ch32vx_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = ch32vx_protect_check,
	.free_driver_priv = default_flash_free_driver_priv,
};
