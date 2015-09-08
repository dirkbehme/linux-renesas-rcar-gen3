/*
 * rcar_gen3 Core CPG Clocks
 *
 * Copyright (C) 2015 Renesas Electronics Corp.
 *
 * Based on rcar_gen2 Core CPG Clocks driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk-provider.h>
#include <linux/clk/shmobile.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#define RCAR_GEN3_CLK_MAIN	0
#define RCAR_GEN3_CLK_PLL0	1
#define RCAR_GEN3_CLK_PLL1	2
#define RCAR_GEN3_CLK_PLL2	3
#define RCAR_GEN3_CLK_PLL3	4
#define RCAR_GEN3_CLK_PLL4	5
#define RCAR_GEN3_CLK_NR	6

static const char * const rcar_gen3_clk_names[RCAR_GEN3_CLK_NR] = {
	[RCAR_GEN3_CLK_MAIN] = "main",
	[RCAR_GEN3_CLK_PLL0] = "pll0",
	[RCAR_GEN3_CLK_PLL1] = "pll1",
	[RCAR_GEN3_CLK_PLL2] = "pll2",
	[RCAR_GEN3_CLK_PLL3] = "pll3",
	[RCAR_GEN3_CLK_PLL4] = "pll4",
};

struct rcar_gen3_cpg {
	struct clk_onecell_data data;
	void __iomem *reg;
	struct clk *clks[RCAR_GEN3_CLK_NR];
};

#define CPG_PLL0CR	0x00d8
#define CPG_PLL2CR	0x002c

/*
 * common function
 */
#define rcar_clk_readl(cpg, _reg) readl(cpg->reg + _reg)

/*
 * Reset register definitions.
 */
#define MODEMR 0xe6160060

static u32 rcar_gen3_read_mode_pins(void)
{
	static u32 mode;
	static bool mode_valid;

	if (!mode_valid) {
		void __iomem *modemr = ioremap_nocache(MODEMR, 4);

		BUG_ON(!modemr);
		mode = ioread32(modemr);
		iounmap(modemr);
		mode_valid = true;
	}

	return mode;
}

/* -----------------------------------------------------------------------------
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL2	PLL3	PLL4
 * 14 13 19 17	(MHz)
 *-------------------------------------------------------------------
 * 0  0  0  0	16.66 x 1	x180	x192	x144	x192	x144
 * 0  0  0  1	16.66 x 1	x180	x192	x144	x128	x144
 * 0  0  1  0	Prohibited setting
 * 0  0  1  1	16.66 x 1	x180	x192	x144	x192	x144
 * 0  1  0  0	20    x 1	x150	x156	x120	x156	x120
 * 0  1  0  1	20    x 1	x150	x156	x120	x106	x120
 * 0  1  1  0	Prohibited setting
 * 0  1  1  1	20    x 1	x150	x156	x120	x156	x120
 * 1  0  0  0	25    x 1	x120	x128	x96	x128	x96
 * 1  0  0  1	25    x 1	x120	x128	x96	x84	x96
 * 1  0  1  0	Prohibited setting
 * 1  0  1  1	25    x 1	x120	x128	x96	x128	x96
 * 1  1  0  0	33.33 / 2	x180	x192	x144	x192	x144
 * 1  1  0  1	33.33 / 2	x180	x192	x144	x128	x144
 * 1  1  1  0	Prohibited setting
 * 1  1  1  1	33.33 / 2	x180	x192	x144	x192	x144
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 11) | \
					 (((md) & BIT(13)) >> 11) | \
					 (((md) & BIT(19)) >> 18) | \
					 (((md) & BIT(17)) >> 17))
struct cpg_pll_config {
	unsigned int extal_div;
	unsigned int pll1_mult;
	unsigned int pll3_mult;
	unsigned int pll4_mult;
};

static const struct cpg_pll_config cpg_pll_configs[16] __initconst = {
/* EXTAL div	PLL1	PLL3	PLL4 */
	{ 1,	192,	192,	144, },
	{ 1,	192,	128,	144, },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 1,	192,	192,	144, },
	{ 1,	156,	156,	120, },
	{ 1,	156,	106,	120, },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 1,	156,	156,	120, },
	{ 1,	128,	128,	96,  },
	{ 1,	128,	84,	96,  },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 1,	128,	128,	96,  },
	{ 2,	192,	192,	144, },
	{ 2,	192,	128,	144, },
	{ 0,	0,	0,	0,   }, /* Prohibited setting */
	{ 2,	192,	192,	144, },
};

/* -----------------------------------------------------------------------------
 * Initialization
 */

static struct clk * __init
rcar_gen3_cpg_register_clk(struct device_node *np, struct rcar_gen3_cpg *cpg,
			   const struct cpg_pll_config *config,
			   unsigned int gen3_clk)
{
	const char *parent_name = rcar_gen3_clk_names[RCAR_GEN3_CLK_MAIN];
	unsigned int mult = 1;
	unsigned int div = 1;
	u32 value;

	switch (gen3_clk) {
	case RCAR_GEN3_CLK_MAIN:
		parent_name = of_clk_get_parent_name(np, 0);
		div = config->extal_div;
		break;
	case RCAR_GEN3_CLK_PLL0:
		/* PLL0 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = rcar_clk_readl(cpg, CPG_PLL0CR);
		mult = ((value >> 24) & 0x3f) + 1;
		break;
	case RCAR_GEN3_CLK_PLL1:
		mult = config->pll1_mult;
		break;
	case RCAR_GEN3_CLK_PLL2:
		/* PLL2 is a configurable multiplier clock. Register it as a
		 * fixed factor clock for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		value = rcar_clk_readl(cpg, CPG_PLL2CR);
		mult = ((value >> 24) & 0x3f) + 1;
		break;
	case RCAR_GEN3_CLK_PLL3:
		mult = config->pll3_mult;
		break;
	case RCAR_GEN3_CLK_PLL4:
		mult = config->pll4_mult;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return clk_register_fixed_factor(NULL, rcar_gen3_clk_names[gen3_clk],
					 parent_name, 0, mult, div);
}

static void __init rcar_gen3_cpg_clocks_init(struct device_node *np)
{
	const struct cpg_pll_config *config;
	struct rcar_gen3_cpg *cpg;
	u32 cpg_mode;
	unsigned int i;

	cpg = kzalloc(sizeof(*cpg), GFP_KERNEL);
	if (!cpg)
		return;

	cpg->reg = of_iomap(np, 0);
	if (WARN_ON(cpg->reg == NULL))
		return;

	cpg_mode = rcar_gen3_read_mode_pins();

	config = &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
	if (!config->extal_div) {
		pr_err("%s: Prohibited setting (cpg_mode=0x%x)\n",
		       __func__, cpg_mode);
		return;
	}

	for (i = 0; i < RCAR_GEN3_CLK_NR; ++i)
		cpg->clks[i] = ERR_PTR(-ENOENT);

	for (i = 0; i < RCAR_GEN3_CLK_NR; ++i) {
		struct clk *clk;
		u32 idx;
		int ret;

		ret = of_property_read_u32_index(np, "clock-indices", i, &idx);
		if (ret < 0)
			break;

		if (idx >= RCAR_GEN3_CLK_NR) {
			pr_err("%s: skipping unsupported %u\n", __func__, idx);
		} else if (cpg->clks[idx] != ERR_PTR(-ENOENT)) {
			pr_err("%s: skipping already registered %u\n",
			       __func__, idx);
		} else {
			clk = rcar_gen3_cpg_register_clk(np, cpg, config, idx);

			if (IS_ERR(clk))
				pr_err("%s: unable to register %u (%ld)\n",
				       __func__, idx, PTR_ERR(clk));
			else
				cpg->clks[idx] = clk;
		}
	}

	cpg->data.clks = cpg->clks;
	cpg->data.clk_num = i;

	of_clk_add_provider(np, of_clk_src_onecell_get, &cpg->data);
}
CLK_OF_DECLARE(rcar_gen3_cpg_clks, "renesas,rcar-gen3-cpg-clocks",
	       rcar_gen3_cpg_clocks_init);
