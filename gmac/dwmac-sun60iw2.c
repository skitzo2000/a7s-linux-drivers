// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dwmac-sun60iw2.c - out-of-tree stmmac glue for Allwinner GMAC-200/210
 * (A733 / sun60iw2), ported to Linux 6.18 APIs.
 *
 * Port of the vendor BSP driver:
 *   allwinner-bsp/drivers/stmmac/dwmac-sunxi.c
 *   Copyright(c) 2020-2023 Allwinner Technology Co.,Ltd.
 * 6.18 idioms modeled on in-tree dwmac-sun55i.c (Chen-Yu Tsai).
 *
 * Matches the vendor DT node:
 *   compatible = "allwinner,sunxi-gmac-210", "snps,dwmac-5.20";
 *   reg = <MAC>, <wrapper-syscfg>;
 *   clock-names = "stmmaceth", "pclk", "phy", "ptp_ref";
 *   reset-names = "stmmaceth", "ahb";
 *   interrupt-names = "macirq", "eth_lpi", "tx0_irq", "rx0_irq";
 *   tx-delay / rx-delay / aw,soc-phy-clk-en / aw,rgmii-clk-ext
 *   dwmac3v3-supply / phy3v3-supply
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/stmmac.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#include "dwmac-sunxi.h"

#define DWMAC_MODULE_VERSION	"0.3.2-a7s-618"

static int sunxi_dwmac200_set_syscon(struct sunxi_dwmac *chip)
{
	u32 reg_val = 0;

	switch (chip->interface) {
	case PHY_INTERFACE_MODE_MII:
		/* default */
		break;
	case PHY_INTERFACE_MODE_RGMII:
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
		reg_val |= SUNXI_DWMAC200_SYSCON_EPIT;
		reg_val |= FIELD_PREP(SUNXI_DWMAC200_SYSCON_ETCS,
					chip->rgmii_clk_ext ? SUNXI_DWMAC_ETCS_EXT_GMII : SUNXI_DWMAC_ETCS_INT_GMII);
		dev_info(chip->dev, "RGMII use %s transmit clock\n",
			 chip->rgmii_clk_ext ? "external" : "internal");
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg_val |= SUNXI_DWMAC200_SYSCON_RMII_EN;
		reg_val &= ~SUNXI_DWMAC200_SYSCON_ETCS;
		break;
	default:
		dev_err(chip->dev, "Unsupported interface mode: %s",
			phy_modes(chip->interface));
		return -EINVAL;
	}

	writel(reg_val, chip->syscfg_base + SUNXI_DWMAC200_SYSCON_REG);
	return 0;
}

static int sunxi_dwmac200_set_delaychain(struct sunxi_dwmac *chip, enum sunxi_dwmac_delaychain_dir dir, u32 delay)
{
	u32 reg_val = readl(chip->syscfg_base + SUNXI_DWMAC200_SYSCON_REG);
	int ret = -EINVAL;

	switch (dir) {
	case SUNXI_DWMAC_DELAYCHAIN_TX:
		if (delay <= chip->variant->tx_delay_max) {
			reg_val &= ~SUNXI_DWMAC200_SYSCON_ETXDC;
			reg_val |= FIELD_PREP(SUNXI_DWMAC200_SYSCON_ETXDC, delay);
			ret = 0;
		}
		break;
	case SUNXI_DWMAC_DELAYCHAIN_RX:
		if (delay <= chip->variant->rx_delay_max) {
			reg_val &= ~SUNXI_DWMAC200_SYSCON_ERXDC;
			reg_val |= FIELD_PREP(SUNXI_DWMAC200_SYSCON_ERXDC, delay);
			ret = 0;
		}
		break;
	}

	if (!ret)
		writel(reg_val, chip->syscfg_base + SUNXI_DWMAC200_SYSCON_REG);

	return ret;
}

static u32 sunxi_dwmac200_get_delaychain(struct sunxi_dwmac *chip, enum sunxi_dwmac_delaychain_dir dir)
{
	u32 delay = 0;
	u32 reg_val = readl(chip->syscfg_base + SUNXI_DWMAC200_SYSCON_REG);

	switch (dir) {
	case SUNXI_DWMAC_DELAYCHAIN_TX:
		delay = FIELD_GET(SUNXI_DWMAC200_SYSCON_ETXDC, reg_val);
		break;
	case SUNXI_DWMAC_DELAYCHAIN_RX:
		delay = FIELD_GET(SUNXI_DWMAC200_SYSCON_ERXDC, reg_val);
		break;
	default:
		dev_err(chip->dev, "Unknown delaychain dir %d\n", dir);
	}

	return delay;
}

static int sunxi_dwmac210_set_delaychain(struct sunxi_dwmac *chip, enum sunxi_dwmac_delaychain_dir dir, u32 delay)
{
	u32 reg_val = readl(chip->syscfg_base + SUNXI_DWMAC210_CFG_REG);
	int ret = -EINVAL;

	switch (dir) {
	case SUNXI_DWMAC_DELAYCHAIN_TX:
		if (delay <= chip->variant->tx_delay_max) {
			reg_val &= ~(SUNXI_DWMAC210_CFG_ETXDC_H | SUNXI_DWMAC210_CFG_ETXDC_L);
			reg_val |= FIELD_PREP(SUNXI_DWMAC210_CFG_ETXDC_H, delay >> 3);
			reg_val |= FIELD_PREP(SUNXI_DWMAC210_CFG_ETXDC_L, delay);
			ret = 0;
		}
		break;
	case SUNXI_DWMAC_DELAYCHAIN_RX:
		if (delay <= chip->variant->rx_delay_max) {
			reg_val &= ~SUNXI_DWMAC210_CFG_ERXDC;
			reg_val |= FIELD_PREP(SUNXI_DWMAC210_CFG_ERXDC, delay);
			ret = 0;
		}
		break;
	}

	if (!ret)
		writel(reg_val, chip->syscfg_base + SUNXI_DWMAC210_CFG_REG);

	return ret;
}

static u32 sunxi_dwmac210_get_delaychain(struct sunxi_dwmac *chip, enum sunxi_dwmac_delaychain_dir dir)
{
	u32 delay = 0;
	u32 tx_l, tx_h;
	u32 reg_val = readl(chip->syscfg_base + SUNXI_DWMAC210_CFG_REG);

	switch (dir) {
	case SUNXI_DWMAC_DELAYCHAIN_TX:
		tx_h = FIELD_GET(SUNXI_DWMAC210_CFG_ETXDC_H, reg_val);
		tx_l = FIELD_GET(SUNXI_DWMAC210_CFG_ETXDC_L, reg_val);
		delay = (tx_h << 3 | tx_l);
		break;
	case SUNXI_DWMAC_DELAYCHAIN_RX:
		delay = FIELD_GET(SUNXI_DWMAC210_CFG_ERXDC, reg_val);
		break;
	}

	return delay;
}

static int sunxi_dwmac_power_on(struct sunxi_dwmac *chip)
{
	int ret;

	/* set dwmac pin bank voltage to 3.3v */
	if (!IS_ERR(chip->dwmac3v3_supply)) {
		ret = regulator_set_voltage(chip->dwmac3v3_supply, 3300000, 3300000);
		if (ret) {
			dev_err(chip->dev, "Set dwmac3v3-supply voltage 3300000 failed %d\n", ret);
			goto err_dwmac3v3;
		}

		ret = regulator_enable(chip->dwmac3v3_supply);
		if (ret) {
			dev_err(chip->dev, "Enable dwmac3v3-supply failed %d\n", ret);
			goto err_dwmac3v3;
		}
	}

	/* set phy voltage to 3.3v */
	if (!IS_ERR(chip->phy3v3_supply)) {
		ret = regulator_set_voltage(chip->phy3v3_supply, 3300000, 3300000);
		if (ret) {
			dev_err(chip->dev, "Set phy3v3-supply voltage 3300000 failed %d\n", ret);
			goto err_phy3v3;
		}

		ret = regulator_enable(chip->phy3v3_supply);
		if (ret) {
			dev_err(chip->dev, "Enable phy3v3-supply failed\n");
			goto err_phy3v3;
		}
	}

	return 0;

err_phy3v3:
	if (!IS_ERR(chip->dwmac3v3_supply))
		regulator_disable(chip->dwmac3v3_supply);
err_dwmac3v3:
	return ret;
}

static void sunxi_dwmac_power_off(struct sunxi_dwmac *chip)
{
	if (!IS_ERR(chip->phy3v3_supply))
		regulator_disable(chip->phy3v3_supply);
	if (!IS_ERR(chip->dwmac3v3_supply))
		regulator_disable(chip->dwmac3v3_supply);
}

static int sunxi_dwmac_clk_init(struct sunxi_dwmac *chip)
{
	int ret;

	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE)
		reset_control_deassert(chip->hsi_rst);
	reset_control_deassert(chip->ahb_rst);

	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE) {
		ret = clk_prepare_enable(chip->hsi_ahb);
		if (ret) {
			dev_err(chip->dev, "enable hsi_ahb failed\n");
			goto err_ahb;
		}
		ret = clk_prepare_enable(chip->hsi_axi);
		if (ret) {
			dev_err(chip->dev, "enable hsi_axi failed\n");
			goto err_axi;
		}
	}

	if (chip->variant->flags & SUNXI_DWMAC_NSI_CLK_GATE) {
		ret = clk_prepare_enable(chip->nsi_clk);
		if (ret) {
			dev_err(chip->dev, "enable nsi clk failed\n");
			goto err_nsi;
		}
	}

	if (chip->soc_phy_clk_en) {
		ret = clk_prepare_enable(chip->phy_clk);
		if (ret) {
			dev_err(chip->dev, "Enable phy clk failed\n");
			goto err_phy;
		}
	}

	return 0;

err_phy:
	if (chip->variant->flags & SUNXI_DWMAC_NSI_CLK_GATE)
		clk_disable_unprepare(chip->nsi_clk);
err_nsi:
	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE) {
		clk_disable_unprepare(chip->hsi_axi);
err_axi:
		clk_disable_unprepare(chip->hsi_ahb);
	}
err_ahb:
	reset_control_assert(chip->ahb_rst);
	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE)
		reset_control_assert(chip->hsi_rst);
	return ret;
}

static void sunxi_dwmac_clk_exit(struct sunxi_dwmac *chip)
{
	if (chip->soc_phy_clk_en)
		clk_disable_unprepare(chip->phy_clk);
	if (chip->variant->flags & SUNXI_DWMAC_NSI_CLK_GATE)
		clk_disable_unprepare(chip->nsi_clk);
	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE) {
		clk_disable_unprepare(chip->hsi_axi);
		clk_disable_unprepare(chip->hsi_ahb);
	}
	reset_control_assert(chip->ahb_rst);
	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE)
		reset_control_assert(chip->hsi_rst);
}

static int sunxi_dwmac_hw_init(struct sunxi_dwmac *chip)
{
	int ret;

	ret = chip->variant->set_syscon(chip);
	if (ret < 0) {
		dev_err(chip->dev, "Set syscon failed\n");
		goto err;
	}

	ret = chip->variant->set_delaychain(chip, SUNXI_DWMAC_DELAYCHAIN_TX, chip->tx_delay);
	if (ret < 0) {
		dev_err(chip->dev, "Invalid TX clock delay: %d\n", chip->tx_delay);
		goto err;
	}

	ret = chip->variant->set_delaychain(chip, SUNXI_DWMAC_DELAYCHAIN_RX, chip->rx_delay);
	if (ret < 0) {
		dev_err(chip->dev, "Invalid RX clock delay: %d\n", chip->rx_delay);
		goto err;
	}

err:
	return ret;
}

static void sunxi_dwmac_hw_exit(struct sunxi_dwmac *chip)
{
	writel(0, chip->syscfg_base);
}

static int sunxi_dwmac_init(struct platform_device *pdev, void *priv)
{
	struct sunxi_dwmac *chip = priv;
	int ret;

	ret = sunxi_dwmac_power_on(chip);
	if (ret) {
		dev_err(&pdev->dev, "Power on dwmac failed\n");
		return ret;
	}

	ret = sunxi_dwmac_clk_init(chip);
	if (ret) {
		dev_err(&pdev->dev, "Clk init dwmac failed\n");
		goto err_clk;
	}

	ret = sunxi_dwmac_hw_init(chip);
	if (ret)
		dev_warn(&pdev->dev, "Hw init dwmac failed\n");

	return 0;

err_clk:
	sunxi_dwmac_power_off(chip);
	return ret;
}

static void sunxi_dwmac_exit(struct platform_device *pdev, void *priv)
{
	struct sunxi_dwmac *chip = priv;

	sunxi_dwmac_hw_exit(chip);
	sunxi_dwmac_clk_exit(chip);
	sunxi_dwmac_power_off(chip);
}

static void sunxi_dwmac_request_mtl_irq(struct platform_device *pdev, struct sunxi_dwmac *chip,
		struct plat_stmmacenet_data *plat_dat)
{
	u32 queues;
	char int_name[16];

	for (queues = 0; queues < plat_dat->tx_queues_to_use; queues++) {
		snprintf(int_name, sizeof(int_name), "tx%d_irq", queues);
		chip->res->tx_irq[queues] = platform_get_irq_byname_optional(pdev, int_name);
		if (chip->res->tx_irq[queues] < 0)
			chip->res->tx_irq[queues] = 0;
	}

	for (queues = 0; queues < plat_dat->rx_queues_to_use; queues++) {
		snprintf(int_name, sizeof(int_name), "rx%d_irq", queues);
		chip->res->rx_irq[queues] = platform_get_irq_byname_optional(pdev, int_name);
		if (chip->res->rx_irq[queues] < 0)
			chip->res->rx_irq[queues] = 0;
	}
}

static int sunxi_dwmac_resource_get(struct platform_device *pdev, struct sunxi_dwmac *chip,
		struct plat_stmmacenet_data *plat_dat)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(dev, "Get syscfg memory failed\n");
		return -ENODEV;
	}

	chip->syscfg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(chip->syscfg_base)) {
		dev_err(dev, "Syscfg memory mapping failed\n");
		return PTR_ERR(chip->syscfg_base);
	}

	chip->rgmii_clk_ext	= of_property_read_bool(np, "aw,rgmii-clk-ext");
	chip->soc_phy_clk_en = of_property_read_bool(np, "aw,soc-phy-clk-en") ||
							of_property_read_bool(np, "aw,soc-phy25m");
	if (chip->soc_phy_clk_en) {
		chip->phy_clk = devm_clk_get(dev, "phy");
		if (IS_ERR(chip->phy_clk)) {
			chip->phy_clk = devm_clk_get(dev, "phy25m");
			if (IS_ERR(chip->phy_clk)) {
				dev_err(dev, "Get phy25m clk failed\n");
				return -EINVAL;
			}
		}
		dev_info(dev, "Phy use soc fanout\n");
	} else {
		dev_info(dev, "Phy use ext osc\n");
	}

	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE) {
		chip->hsi_ahb = devm_clk_get(dev, "hsi_ahb");
		if (IS_ERR(chip->hsi_ahb)) {
			dev_err(dev, "Get hsi_ahb clk failed\n");
			return -EINVAL;
		}
		chip->hsi_axi = devm_clk_get(dev, "hsi_axi");
		if (IS_ERR(chip->hsi_axi)) {
			dev_err(dev, "Get hsi_axi clk failed\n");
			return -EINVAL;
		}
	}

	if (chip->variant->flags & SUNXI_DWMAC_NSI_CLK_GATE) {
		chip->nsi_clk = devm_clk_get(dev, "nsi");
		if (IS_ERR(chip->nsi_clk)) {
			dev_err(dev, "Get nsi clk failed\n");
			return -EINVAL;
		}
	}

	if (chip->variant->flags & SUNXI_DWMAC_MEM_ECC) {
		dev_info(dev, "Support mem ecc\n");
		chip->res->sfty_ce_irq = platform_get_irq_byname_optional(pdev, "mac_eccirq");
		if (chip->res->sfty_ce_irq < 0) {
			dev_err(&pdev->dev, "Get ecc irq failed\n");
			return -EINVAL;
		}
	}

	if (chip->variant->flags & SUNXI_DWMAC_HSI_CLK_GATE) {
		chip->hsi_rst = devm_reset_control_get_shared(chip->dev, "hsi");
		if (IS_ERR(chip->hsi_rst)) {
			dev_err(dev, "Get hsi reset failed\n");
			return -EINVAL;
		}
	}

	chip->ahb_rst = devm_reset_control_get_optional_shared(chip->dev, "ahb");
	if (IS_ERR(chip->ahb_rst)) {
		dev_err(dev, "Get mac reset failed\n");
		return -EINVAL;
	}

	chip->dwmac3v3_supply = devm_regulator_get_optional(&pdev->dev, "dwmac3v3");
	if (IS_ERR(chip->dwmac3v3_supply))
		dev_warn(dev, "Not found dwmac3v3-supply\n");

	chip->phy3v3_supply = devm_regulator_get_optional(&pdev->dev, "phy3v3");
	if (IS_ERR(chip->phy3v3_supply))
		dev_warn(dev, "Not found phy3v3-supply\n");

	ret = of_property_read_u32(np, "tx-delay", &chip->tx_delay);
	if (ret) {
		dev_warn(dev, "Get gmac tx-delay failed, use default 0\n");
		chip->tx_delay = 0;
	}

	ret = of_property_read_u32(np, "rx-delay", &chip->rx_delay);
	if (ret) {
		dev_warn(dev, "Get gmac rx-delay failed, use default 0\n");
		chip->rx_delay = 0;
	}

	if (chip->variant->flags & SUNXI_DWMAC_MULTI_MSI)
		sunxi_dwmac_request_mtl_irq(pdev, chip, plat_dat);

	return 0;
}

static int sunxi_dwmac_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	struct sunxi_dwmac *chip;
	struct device *dev = &pdev->dev;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->variant = device_get_match_data(&pdev->dev);
	if (!chip->variant) {
		dev_err(&pdev->dev, "Missing dwmac-sunxi variant\n");
		return -EINVAL;
	}

	chip->dev = dev;
	chip->res = &stmmac_res;

	plat_dat = devm_stmmac_probe_config_dt(pdev, stmmac_res.mac);
	if (IS_ERR(plat_dat))
		return PTR_ERR(plat_dat);

	ret = sunxi_dwmac_resource_get(pdev, chip, plat_dat);
	if (ret < 0)
		return ret;

	plat_dat->bsp_priv = chip;
	plat_dat->init = sunxi_dwmac_init;
	plat_dat->exit = sunxi_dwmac_exit;
	/* must use 0~4G space */
	plat_dat->host_dma_width = 32;
	if (chip->variant->flags & SUNXI_DWMAC_SPH_DISABLE)
		plat_dat->flags |= STMMAC_FLAG_SPH_DISABLE;
	if (chip->variant->flags & SUNXI_DWMAC_MULTI_MSI)
		plat_dat->flags |= STMMAC_FLAG_MULTI_MSI_EN;
	chip->interface = plat_dat->phy_interface;
	plat_dat->clk_csr = 4; /* MDC = AHB(200M)/102 = 2M */
	/* EEE/LPI hangs the SoC on 6.18: the idle link enters Low-Power-Idle
	 * and the wrapper's TX clock gating never wakes the bus (board hard-hangs
	 * minutes after link-up while idle). The vendor kernel gates this path
	 * differently. Strip every LPI enable the DT sneaks in. */
	plat_dat->flags &= ~(STMMAC_FLAG_EN_TX_LPI_CLOCKGATING |
			     STMMAC_FLAG_EN_TX_LPI_CLK_PHY_CAP);
	if (plat_dat->axi)
		plat_dat->axi->axi_lpi_en = false;

	ret = devm_stmmac_pltfr_probe(pdev, plat_dat, &stmmac_res);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "probe success (Version %s)\n", DWMAC_MODULE_VERSION);

	return 0;
}

static const struct sunxi_dwmac_variant dwmac200_variant = {
	.interface = PHY_INTERFACE_MODE_RMII | PHY_INTERFACE_MODE_RGMII,
	.flags = SUNXI_DWMAC_SPH_DISABLE,
	.rx_delay_max = 31,
	.tx_delay_max = 7,
	.set_syscon = sunxi_dwmac200_set_syscon,
	.set_delaychain = sunxi_dwmac200_set_delaychain,
	.get_delaychain = sunxi_dwmac200_get_delaychain,
};

static const struct sunxi_dwmac_variant dwmac210_variant = {
	.interface = PHY_INTERFACE_MODE_RMII | PHY_INTERFACE_MODE_RGMII,
	/* MULTI_MSI dropped: per-queue tx0/rx0 irqs panic the 6.18 stmmac core
	 * on link-up; single macirq (sun55i-style) is stable. */
	.flags = SUNXI_DWMAC_SPH_DISABLE,
	.rx_delay_max = 31,
	.tx_delay_max = 31,
	.set_syscon = sunxi_dwmac200_set_syscon,
	.set_delaychain = sunxi_dwmac210_set_delaychain,
	.get_delaychain = sunxi_dwmac210_get_delaychain,
};

static const struct of_device_id sunxi_dwmac_match[] = {
	{ .compatible = "allwinner,sunxi-gmac-200", .data = &dwmac200_variant },
	{ .compatible = "allwinner,sunxi-gmac-210", .data = &dwmac210_variant },
	{ }
};
MODULE_DEVICE_TABLE(of, sunxi_dwmac_match);

static struct platform_driver sunxi_dwmac_driver = {
	.probe = sunxi_dwmac_probe,
	.driver = {
		.name			= "dwmac-sun60iw2",
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = sunxi_dwmac_match,
	},
};
module_platform_driver(sunxi_dwmac_driver);

MODULE_DESCRIPTION("Allwinner GMAC-210 (sun60iw2/A733) DWMAC glue, 6.18 port");
MODULE_AUTHOR("wujiayi <wujiayi@allwinnertech.com>");
MODULE_AUTHOR("xuminghui <xuminghui@allwinnertech.com>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DWMAC_MODULE_VERSION);
