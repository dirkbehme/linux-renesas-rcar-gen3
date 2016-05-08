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
#include <linux/of_address.h>

#include <misc/boot-mode-reg.h>

#define RCAR_RST_BASE 0xe6160000
#define RCAR_RST_SIZE 0x200
#define MODEMR 0x60

static int __init rcar_read_mode_pins(void)
{
	void __iomem *reset;
	struct device_node *np;
	int err = -ENOMEM;
	static u32 mode;

	np = of_find_compatible_node(NULL, NULL, "renesas,rcar-rst");
	if (np)
		reset = of_iomap(np, 0);
	else {
		pr_warn("can't find renesas rcar-rst device node");
		reset = ioremap_nocache(RCAR_RST_BASE, RCAR_RST_SIZE);
	}

	if (!reset) {
		pr_err("failed to map reset registers");
		of_node_put(np);
		goto err;
	}
	mode = ioread32(reset + MODEMR);
	iounmap(reset);
	of_node_put(np);

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
	    of_machine_is_compatible("renesas,r8a7794") ||
	    of_machine_is_compatible("renesas,r8a7795") ||
	    of_machine_is_compatible("renesas,r8a7796"))
		return rcar_read_mode_pins();

	return 0;
}
early_initcall(rcar_init_boot_mode);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Horman <horms@verge.net.au>");
MODULE_DESCRIPTION("R-Car Boot Mode Register Driver");
