/*
 * R-Car Boot Mode Register Driver
 *
 * Copyright (C) 2015 Simon Horman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>

#include <misc/boot-mode-reg.h>

#define MODEMR 0xe6160060

static int __init rcar_read_mode_pins(void)
{
	void __iomem *modemr;
	int err = -ENOMEM;
	static u32 mode;

	modemr = ioremap_nocache(MODEMR, 4);
	if (!modemr) {
		pr_err("failed to map boot mode register");
		goto err;
	}
	mode = ioread32(modemr);
	iounmap(modemr);

	err = boot_mode_reg_set(mode);
err:
	if (err)
		pr_err("failed to initialise boot mode");
	return err;
}

int __init rcar_init_boot_mode(void)
{
	if (of_machine_is_compatible("renesas,r8a7790") ||
	    of_machine_is_compatible("renesas,r8a7791") ||
	    of_machine_is_compatible("renesas,r8a7792") ||
	    of_machine_is_compatible("renesas,r8a7793") ||
	    of_machine_is_compatible("renesas,r8a7794"))
		return rcar_read_mode_pins();

	return 0;
}
early_initcall(rcar_init_boot_mode);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Horman <horms@verge.net.au>");
MODULE_DESCRIPTION("R-Car Boot Mode Register Driver");
