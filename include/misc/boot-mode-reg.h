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

#ifndef _BOOT_MODE_REG_H
#define _BOOT_MODE_REG_H

#include <linux/types.h>

int boot_mode_reg_get(u32 *mode);
int boot_mode_reg_set(u32 mode);

#endif
