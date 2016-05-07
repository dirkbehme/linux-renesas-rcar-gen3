/*
 * Boot Mode Register
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

#include <asm/errno.h>

#include <linux/export.h>
#include <linux/module.h>

#include <misc/boot-mode-reg.h>

static DEFINE_MUTEX(boot_mode_mutex);
static bool boot_mode_is_set;
static u32 boot_mode;

/**
 * boot_mode_reg_get() - retrieve boot mode register value
 * @mode: implementation-dependent boot mode register value
 *
 * Retrieves the boot mode register value previously registered
 * using boot_mode_reg_set().
 *
 * return: 0 on success
 */
int boot_mode_reg_get(u32 *mode)
{
	int err = -ENOENT;

	mutex_lock(&boot_mode_mutex);
	if (boot_mode_is_set) {
		*mode = boot_mode;
		err = 0;
	}
	mutex_unlock(&boot_mode_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(boot_mode_reg_get);

/**
 * boot_mode_reg_set() - record boot mode register value
 * @mode: implementation-dependent boot mode register value
 *
 * Records the boot mode register value which may subsequently
 * be retrieved using boot_mode_reg_get().
 *
 * return: 0 on success
 */
int boot_mode_reg_set(u32 mode)
{
	int err = -EBUSY;

	mutex_lock(&boot_mode_mutex);
	if (!boot_mode_is_set) {
		boot_mode = mode;
		boot_mode_is_set = true;
		err = 0;
	}
	mutex_unlock(&boot_mode_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(boot_mode_reg_set);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Simon Horman <horms@verge.net.au>");
MODULE_DESCRIPTION("Core Boot Mode Register Driver");
