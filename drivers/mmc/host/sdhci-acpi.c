/*
 * Secure Digital Host Controller Interface ACPI driver.
 *
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/compiler.h>
#include <linux/stddef.h>
#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#include <linux/mmc/host.h>
#include <linux/mmc/pm.h>
#include <linux/mmc/slot-gpio.h>

#ifdef CONFIG_X86
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/iosf_mbi.h>
#include <linux/pci.h>
#endif

#include "sdhci.h"

enum {
	SDHCI_ACPI_SD_CD		= BIT(0),
	SDHCI_ACPI_RUNTIME_PM		= BIT(1),
	SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL	= BIT(2),
};

struct sdhci_acpi_chip {
	const struct	sdhci_ops *ops;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned long	caps;
	unsigned int	caps2;
	mmc_pm_flag_t	pm_caps;
};

struct sdhci_acpi_slot {
	const struct	sdhci_acpi_chip *chip;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned long	caps;
	unsigned int	caps2;
	mmc_pm_flag_t	pm_caps;
	unsigned int	flags;
	size_t		priv_size;
	int (*probe_slot)(struct platform_device *, const char *, const char *);
	int (*remove_slot)(struct platform_device *);
	int (*free_slot)(struct platform_device *pdev);
	int (*setup_host)(struct platform_device *pdev);
};

struct sdhci_acpi_host {
	struct sdhci_host		*host;
	const struct sdhci_acpi_slot	*slot;
	struct platform_device		*pdev;
	bool				use_runtime_pm;
	unsigned long			private[0] ____cacheline_aligned;
};

static inline void *sdhci_acpi_priv(struct sdhci_acpi_host *c)
{
	return (void *)c->private;
}

static inline bool sdhci_acpi_flag(struct sdhci_acpi_host *c, unsigned int flag)
{
	return c->slot && (c->slot->flags & flag);
}

#define INTEL_DSM_HS_CAPS_SDR25		BIT(0)
#define INTEL_DSM_HS_CAPS_DDR50		BIT(1)
#define INTEL_DSM_HS_CAPS_SDR50		BIT(2)
#define INTEL_DSM_HS_CAPS_SDR104	BIT(3)

enum {
	INTEL_DSM_FNS		=  0,
	INTEL_DSM_V18_SWITCH	=  3,
	INTEL_DSM_V33_SWITCH	=  4,
	INTEL_DSM_HS_CAPS	=  8,
};

struct intel_host {
	u32	dsm_fns;
	u32	hs_caps;
};

static const guid_t intel_dsm_guid =
	GUID_INIT(0xF6C13EA5, 0x65CD, 0x461F,
		  0xAB, 0x7A, 0x29, 0xF7, 0xE8, 0xD5, 0xBD, 0x61);

static int __intel_dsm(struct intel_host *intel_host, struct device *dev,
		       unsigned int fn, u32 *result)
{
	union acpi_object *obj;
	int err = 0;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &intel_dsm_guid, 0, fn, NULL);
	if (!obj)
		return -EOPNOTSUPP;

	if (obj->type == ACPI_TYPE_INTEGER) {
		*result = obj->integer.value;
	} else if (obj->type == ACPI_TYPE_BUFFER && obj->buffer.length > 0) {
		size_t len = min_t(size_t, obj->buffer.length, 4);

		*result = 0;
		memcpy(result, obj->buffer.pointer, len);
	} else {
		dev_err(dev, "%s DSM fn %u obj->type %d obj->buffer.length %d\n",
			__func__, fn, obj->type, obj->buffer.length);
		err = -EINVAL;
	}

	ACPI_FREE(obj);

	return err;
}

static int intel_dsm(struct intel_host *intel_host, struct device *dev,
		     unsigned int fn, u32 *result)
{
	if (fn > 31 || !(intel_host->dsm_fns & (1 << fn)))
		return -EOPNOTSUPP;

	return __intel_dsm(intel_host, dev, fn, result);
}

static void intel_dsm_init(struct intel_host *intel_host, struct device *dev,
			   struct mmc_host *mmc)
{
	int err;

	intel_host->hs_caps = ~0;

	err = __intel_dsm(intel_host, dev, INTEL_DSM_FNS, &intel_host->dsm_fns);
	if (err) {
		pr_debug("%s: DSM not supported, error %d\n",
			 mmc_hostname(mmc), err);
		return;
	}

	pr_debug("%s: DSM function mask %#x\n",
		 mmc_hostname(mmc), intel_host->dsm_fns);

	intel_dsm(intel_host, dev, INTEL_DSM_HS_CAPS, &intel_host->hs_caps);
}

static int intel_start_signal_voltage_switch(struct mmc_host *mmc,
					     struct mmc_ios *ios)
{
	struct device *dev = mmc_dev(mmc);
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct intel_host *intel_host = sdhci_acpi_priv(c);
	unsigned int fn;
	u32 result = 0;
	int err;

	err = sdhci_start_signal_voltage_switch(mmc, ios);
	if (err)
		return err;

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_330:
		fn = INTEL_DSM_V33_SWITCH;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		fn = INTEL_DSM_V18_SWITCH;
		break;
	default:
		return 0;
	}

	err = intel_dsm(intel_host, dev, fn, &result);
	pr_debug("%s: %s DSM fn %u error %d result %u\n",
		 mmc_hostname(mmc), __func__, fn, err, result);

	return 0;
}

static void sdhci_acpi_int_hw_reset(struct sdhci_host *host)
{
	u8 reg;

	reg = sdhci_readb(host, SDHCI_POWER_CONTROL);
	reg |= 0x10;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	/* For eMMC, minimum is 1us but give it 9us for good measure */
	udelay(9);
	reg &= ~0x10;
	sdhci_writeb(host, reg, SDHCI_POWER_CONTROL);
	/* For eMMC, minimum is 200us but give it 300us for good measure */
	usleep_range(300, 1000);
}

static const struct sdhci_ops sdhci_acpi_ops_dflt = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_ops sdhci_acpi_ops_int = {
	.set_clock = sdhci_set_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
	.hw_reset   = sdhci_acpi_int_hw_reset,
};

static const struct sdhci_acpi_chip sdhci_acpi_chip_int = {
	.ops = &sdhci_acpi_ops_int,
};

#ifdef CONFIG_X86

static bool sdhci_acpi_byt(void)
{
	static const struct x86_cpu_id byt[] = {
		{ X86_VENDOR_INTEL, 6, INTEL_FAM6_ATOM_SILVERMONT },
		{}
	};

	return x86_match_cpu(byt);
}

static bool sdhci_acpi_cht(void)
{
	static const struct x86_cpu_id cht[] = {
		{ X86_VENDOR_INTEL, 6, INTEL_FAM6_ATOM_AIRMONT },
		{}
	};

	return x86_match_cpu(cht);
}

#define BYT_IOSF_SCCEP			0x63
#define BYT_IOSF_OCP_NETCTRL0		0x1078
#define BYT_IOSF_OCP_TIMEOUT_BASE	GENMASK(10, 8)

static void sdhci_acpi_byt_setting(struct device *dev)
{
	u32 val = 0;

	if (!sdhci_acpi_byt())
		return;

	if (iosf_mbi_read(BYT_IOSF_SCCEP, MBI_CR_READ, BYT_IOSF_OCP_NETCTRL0,
			  &val)) {
		dev_err(dev, "%s read error\n", __func__);
		return;
	}

	if (!(val & BYT_IOSF_OCP_TIMEOUT_BASE))
		return;

	val &= ~BYT_IOSF_OCP_TIMEOUT_BASE;

	if (iosf_mbi_write(BYT_IOSF_SCCEP, MBI_CR_WRITE, BYT_IOSF_OCP_NETCTRL0,
			   val)) {
		dev_err(dev, "%s write error\n", __func__);
		return;
	}

	dev_dbg(dev, "%s completed\n", __func__);
}

static bool sdhci_acpi_byt_defer(struct device *dev)
{
	if (!sdhci_acpi_byt())
		return false;

	if (!iosf_mbi_available())
		return true;

	sdhci_acpi_byt_setting(dev);

	return false;
}

static bool sdhci_acpi_cht_pci_wifi(unsigned int vendor, unsigned int device,
				    unsigned int slot, unsigned int parent_slot)
{
	struct pci_dev *dev, *parent, *from = NULL;

	while (1) {
		dev = pci_get_device(vendor, device, from);
		pci_dev_put(from);
		if (!dev)
			break;
		parent = pci_upstream_bridge(dev);
		if (ACPI_COMPANION(&dev->dev) && PCI_SLOT(dev->devfn) == slot &&
		    parent && PCI_SLOT(parent->devfn) == parent_slot &&
		    !pci_upstream_bridge(parent)) {
			pci_dev_put(dev);
			return true;
		}
		from = dev;
	}

	return false;
}

/*
 * GPDwin uses PCI wifi which conflicts with SDIO's use of
 * acpi_device_fix_up_power() on child device nodes. Identifying GPDwin is
 * problematic, but since SDIO is only used for wifi, the presence of the PCI
 * wifi card in the expected slot with an ACPI companion node, is used to
 * indicate that acpi_device_fix_up_power() should be avoided.
 */
static inline bool sdhci_acpi_no_fixup_child_power(const char *hid,
						   const char *uid)
{
	return sdhci_acpi_cht() &&
	       !strcmp(hid, "80860F14") &&
	       !strcmp(uid, "2") &&
	       sdhci_acpi_cht_pci_wifi(0x14e4, 0x43ec, 0, 28);
}

#else

static inline void sdhci_acpi_byt_setting(struct device *dev)
{
}

static inline bool sdhci_acpi_byt_defer(struct device *dev)
{
	return false;
}

static inline bool sdhci_acpi_no_fixup_child_power(const char *hid,
						   const char *uid)
{
	return false;
}

#endif

static int bxt_get_cd(struct mmc_host *mmc)
{
	int gpio_cd = mmc_gpio_get_cd(mmc);
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned long flags;
	int ret = 0;

	if (!gpio_cd)
		return 0;

	spin_lock_irqsave(&host->lock, flags);

	if (host->flags & SDHCI_DEVICE_DEAD)
		goto out;

	ret = !!(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
out:
	spin_unlock_irqrestore(&host->lock, flags);

	return ret;
}

static int intel_probe_slot(struct platform_device *pdev, const char *hid,
			    const char *uid)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct intel_host *intel_host = sdhci_acpi_priv(c);
	struct sdhci_host *host = c->host;

	if (hid && uid && !strcmp(hid, "80860F14") && !strcmp(uid, "1") &&
	    sdhci_readl(host, SDHCI_CAPABILITIES) == 0x446cc8b2 &&
	    sdhci_readl(host, SDHCI_CAPABILITIES_1) == 0x00000807)
		host->timeout_clk = 1000; /* 1000 kHz i.e. 1 MHz */

	if (hid && !strcmp(hid, "80865ACA"))
		host->mmc_host_ops.get_cd = bxt_get_cd;

	intel_dsm_init(intel_host, &pdev->dev, host->mmc);

	host->mmc_host_ops.start_signal_voltage_switch =
					intel_start_signal_voltage_switch;

	return 0;
}

static int intel_setup_host(struct platform_device *pdev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct intel_host *intel_host = sdhci_acpi_priv(c);

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_SDR25))
		c->host->mmc->caps &= ~MMC_CAP_UHS_SDR25;

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_SDR50))
		c->host->mmc->caps &= ~MMC_CAP_UHS_SDR50;

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_DDR50))
		c->host->mmc->caps &= ~MMC_CAP_UHS_DDR50;

	if (!(intel_host->hs_caps & INTEL_DSM_HS_CAPS_SDR104))
		c->host->mmc->caps &= ~MMC_CAP_UHS_SDR104;

	return 0;
}

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_emmc = {
	.chip    = &sdhci_acpi_chip_int,
	.caps    = MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE |
		   MMC_CAP_HW_RESET | MMC_CAP_1_8V_DDR |
		   MMC_CAP_CMD_DURING_TFR | MMC_CAP_WAIT_WHILE_BUSY,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.quirks  = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_STOP_WITH_TC |
		   SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400,
	.probe_slot	= intel_probe_slot,
	.setup_host	= intel_setup_host,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sdio = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION |
		   SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2 = SDHCI_QUIRK2_HOST_OFF_CARD_ON,
	.caps    = MMC_CAP_NONREMOVABLE | MMC_CAP_POWER_OFF_CARD |
		   MMC_CAP_WAIT_WHILE_BUSY,
	.flags   = SDHCI_ACPI_RUNTIME_PM,
	.pm_caps = MMC_PM_KEEP_POWER,
	.probe_slot	= intel_probe_slot,
	.setup_host	= intel_setup_host,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_int_sd = {
	.flags   = SDHCI_ACPI_SD_CD | SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL |
		   SDHCI_ACPI_RUNTIME_PM,
	.quirks  = SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC,
	.quirks2 = SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON |
		   SDHCI_QUIRK2_STOP_WITH_TC,
	.caps    = MMC_CAP_WAIT_WHILE_BUSY | MMC_CAP_AGGRESSIVE_PM,
	.probe_slot	= intel_probe_slot,
	.setup_host	= intel_setup_host,
	.priv_size	= sizeof(struct intel_host),
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_qcom_sd_3v = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.quirks2 = SDHCI_QUIRK2_NO_1_8_V,
	.caps    = MMC_CAP_NONREMOVABLE,
};

static const struct sdhci_acpi_slot sdhci_acpi_slot_qcom_sd = {
	.quirks  = SDHCI_QUIRK_BROKEN_CARD_DETECTION,
	.caps    = MMC_CAP_NONREMOVABLE,
};

/* AMD sdhci reset dll register. */
#define SDHCI_AMD_RESET_DLL_REGISTER    0x908

static int amd_select_drive_strength(struct mmc_card *card,
				     unsigned int max_dtr, int host_drv,
				     int card_drv, int *drv_type)
{
	return MMC_SET_DRIVER_TYPE_A;
}

static void sdhci_acpi_amd_hs400_dll(struct sdhci_host *host)
{
	/* AMD Platform requires dll setting */
	sdhci_writel(host, 0x40003210, SDHCI_AMD_RESET_DLL_REGISTER);
	usleep_range(10, 20);
	sdhci_writel(host, 0x40033210, SDHCI_AMD_RESET_DLL_REGISTER);
}

/*
 * For AMD Platform it is required to disable the tuning
 * bit first controller to bring to HS Mode from HS200
 * mode, later enable to tune to HS400 mode.
 */
static void amd_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	unsigned int old_timing = host->timing;

	sdhci_set_ios(mmc, ios);
	if (old_timing == MMC_TIMING_MMC_HS200 &&
	    ios->timing == MMC_TIMING_MMC_HS)
		sdhci_writew(host, 0x9, SDHCI_HOST_CONTROL2);
	if (old_timing != MMC_TIMING_MMC_HS400 &&
	    ios->timing == MMC_TIMING_MMC_HS400) {
		sdhci_writew(host, 0x80, SDHCI_HOST_CONTROL2);
		sdhci_acpi_amd_hs400_dll(host);
	}
}

static const struct sdhci_ops sdhci_acpi_ops_amd = {
	.set_clock	= sdhci_set_clock,
	.set_bus_width	= sdhci_set_bus_width,
	.reset		= sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static const struct sdhci_acpi_chip sdhci_acpi_chip_amd = {
	.ops = &sdhci_acpi_ops_amd,
};

static int sdhci_acpi_emmc_amd_probe_slot(struct platform_device *pdev,
					  const char *hid, const char *uid)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct sdhci_host *host   = c->host;

	sdhci_read_caps(host);
	if (host->caps1 & SDHCI_SUPPORT_DDR50)
		host->mmc->caps = MMC_CAP_1_8V_DDR;

	if ((host->caps1 & SDHCI_SUPPORT_SDR104) &&
	    (host->mmc->caps & MMC_CAP_1_8V_DDR))
		host->mmc->caps2 = MMC_CAP2_HS400_1_8V;

	/*
	 * There are two types of presets out in the wild:
	 * 1) Default/broken presets.
	 *    These presets have two sets of problems:
	 *    a) The clock divisor for SDR12, SDR25, and SDR50 is too small.
	 *       This results in clock frequencies that are 2x higher than
	 *       acceptable. i.e., SDR12 = 25 MHz, SDR25 = 50 MHz, SDR50 =
	 *       100 MHz.x
	 *    b) The HS200 and HS400 driver strengths don't match.
	 *       By default, the SDR104 preset register has a driver strength of
	 *       A, but the (internal) HS400 preset register has a driver
	 *       strength of B. As part of initializing HS400, HS200 tuning
	 *       needs to be performed. Having different driver strengths
	 *       between tuning and operation is wrong. It results in different
	 *       rise/fall times that lead to incorrect sampling.
	 * 2) Firmware with properly initialized presets.
	 *    These presets have proper clock divisors. i.e., SDR12 => 12MHz,
	 *    SDR25 => 25 MHz, SDR50 => 50 MHz. Additionally the HS200 and
	 *    HS400 preset driver strengths match.
	 *
	 *    Enabling presets for HS400 doesn't work for the following reasons:
	 *    1) sdhci_set_ios has a hard coded list of timings that are used
	 *       to determine if presets should be enabled.
	 *    2) sdhci_get_preset_value is using a non-standard register to
	 *       read out HS400 presets. The AMD controller doesn't support this
	 *       non-standard register. In fact, it doesn't expose the HS400
	 *       preset register anywhere in the SDHCI memory map. This results
	 *       in reading a garbage value and using the wrong presets.
	 *
	 *       Since HS400 and HS200 presets must be identical, we could
	 *       instead use the the SDR104 preset register.
	 *
	 *    If the above issues are resolved we could remove this quirk for
	 *    firmware that that has valid presets (i.e., SDR12 <= 12 MHz).
	 */
	host->quirks2 |= SDHCI_QUIRK2_PRESET_VALUE_BROKEN;

	host->mmc_host_ops.select_drive_strength = amd_select_drive_strength;
	host->mmc_host_ops.set_ios = amd_set_ios;
	return 0;
}

static const struct sdhci_acpi_slot sdhci_acpi_slot_amd_emmc = {
	.chip		= &sdhci_acpi_chip_amd,
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.quirks		= SDHCI_QUIRK_32BIT_DMA_ADDR |
			  SDHCI_QUIRK_32BIT_DMA_SIZE |
			  SDHCI_QUIRK_32BIT_ADMA_SIZE,
	.quirks2	= SDHCI_QUIRK2_BROKEN_64_BIT_DMA,
	.probe_slot     = sdhci_acpi_emmc_amd_probe_slot,
};

struct sdhci_acpi_uid_slot {
	const char *hid;
	const char *uid;
	const struct sdhci_acpi_slot *slot;
};

static const struct sdhci_acpi_uid_slot sdhci_acpi_uids[] = {
	{ "80865ACA", NULL, &sdhci_acpi_slot_int_sd },
	{ "80865ACC", NULL, &sdhci_acpi_slot_int_emmc },
	{ "80865AD0", NULL, &sdhci_acpi_slot_int_sdio },
	{ "80860F14" , "1" , &sdhci_acpi_slot_int_emmc },
	{ "80860F14" , "2" , &sdhci_acpi_slot_int_sdio },
	{ "80860F14" , "3" , &sdhci_acpi_slot_int_sd   },
	{ "80860F16" , NULL, &sdhci_acpi_slot_int_sd   },
	{ "INT33BB"  , "2" , &sdhci_acpi_slot_int_sdio },
	{ "INT33BB"  , "3" , &sdhci_acpi_slot_int_sd },
	{ "INT33C6"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "INT3436"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "INT344D"  , NULL, &sdhci_acpi_slot_int_sdio },
	{ "PNP0FFF"  , "3" , &sdhci_acpi_slot_int_sd   },
	{ "PNP0D40"  },
	{ "QCOM8051", NULL, &sdhci_acpi_slot_qcom_sd_3v },
	{ "QCOM8052", NULL, &sdhci_acpi_slot_qcom_sd },
	{ "AMDI0040", NULL, &sdhci_acpi_slot_amd_emmc },
	{ },
};

static const struct acpi_device_id sdhci_acpi_ids[] = {
	{ "80865ACA" },
	{ "80865ACC" },
	{ "80865AD0" },
	{ "80860F14" },
	{ "80860F16" },
	{ "INT33BB"  },
	{ "INT33C6"  },
	{ "INT3436"  },
	{ "INT344D"  },
	{ "PNP0D40"  },
	{ "QCOM8051" },
	{ "QCOM8052" },
	{ "AMDI0040" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, sdhci_acpi_ids);

static const struct sdhci_acpi_slot *sdhci_acpi_get_slot(const char *hid,
							 const char *uid)
{
	const struct sdhci_acpi_uid_slot *u;

	for (u = sdhci_acpi_uids; u->hid; u++) {
		if (strcmp(u->hid, hid))
			continue;
		if (!u->uid)
			return u->slot;
		if (uid && !strcmp(u->uid, uid))
			return u->slot;
	}
	return NULL;
}

static int sdhci_acpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sdhci_acpi_slot *slot;
	struct acpi_device *device, *child;
	struct sdhci_acpi_host *c;
	struct sdhci_host *host;
	struct resource *iomem;
	resource_size_t len;
	size_t priv_size;
	const char *hid;
	const char *uid;
	int err;

	device = ACPI_COMPANION(dev);
	if (!device)
		return -ENODEV;

	hid = acpi_device_hid(device);
	uid = acpi_device_uid(device);

	slot = sdhci_acpi_get_slot(hid, uid);

	/* Power on the SDHCI controller and its children */
	acpi_device_fix_up_power(device);
	if (!sdhci_acpi_no_fixup_child_power(hid, uid)) {
		list_for_each_entry(child, &device->children, node)
			if (child->status.present && child->status.enabled)
				acpi_device_fix_up_power(child);
	}

	if (sdhci_acpi_byt_defer(dev))
		return -EPROBE_DEFER;

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -ENOMEM;

	len = resource_size(iomem);
	if (len < 0x100)
		dev_err(dev, "Invalid iomem size!\n");

	if (!devm_request_mem_region(dev, iomem->start, len, dev_name(dev)))
		return -ENOMEM;

	priv_size = slot ? slot->priv_size : 0;
	host = sdhci_alloc_host(dev, sizeof(struct sdhci_acpi_host) + priv_size);
	if (IS_ERR(host))
		return PTR_ERR(host);

	c = sdhci_priv(host);
	c->host = host;
	c->slot = slot;
	c->pdev = pdev;
	c->use_runtime_pm = sdhci_acpi_flag(c, SDHCI_ACPI_RUNTIME_PM);

	platform_set_drvdata(pdev, c);

	host->hw_name	= "ACPI";
	host->ops	= &sdhci_acpi_ops_dflt;
	host->irq	= platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		err = host->irq;
		goto err_free;
	}

	host->ioaddr = devm_ioremap_nocache(dev, iomem->start,
					    resource_size(iomem));
	if (host->ioaddr == NULL) {
		err = -ENOMEM;
		goto err_free;
	}

	if (c->slot) {
		if (c->slot->probe_slot) {
			err = c->slot->probe_slot(pdev, hid, uid);
			if (err)
				goto err_free;
		}
		if (c->slot->chip) {
			host->ops            = c->slot->chip->ops;
			host->quirks        |= c->slot->chip->quirks;
			host->quirks2       |= c->slot->chip->quirks2;
			host->mmc->caps     |= c->slot->chip->caps;
			host->mmc->caps2    |= c->slot->chip->caps2;
			host->mmc->pm_caps  |= c->slot->chip->pm_caps;
		}
		host->quirks        |= c->slot->quirks;
		host->quirks2       |= c->slot->quirks2;
		host->mmc->caps     |= c->slot->caps;
		host->mmc->caps2    |= c->slot->caps2;
		host->mmc->pm_caps  |= c->slot->pm_caps;
	}

	host->mmc->caps2 |= MMC_CAP2_NO_PRESCAN_POWERUP;

	if (sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD)) {
		bool v = sdhci_acpi_flag(c, SDHCI_ACPI_SD_CD_OVERRIDE_LEVEL);

		err = mmc_gpiod_request_cd(host->mmc, NULL, 0, v, 0, NULL);
		if (err) {
			if (err == -EPROBE_DEFER)
				goto err_free;
			dev_warn(dev, "failed to setup card detect gpio\n");
			c->use_runtime_pm = false;
		}
	}

	err = sdhci_setup_host(host);
	if (err)
		goto err_free;

	if (c->slot && c->slot->setup_host) {
		err = c->slot->setup_host(pdev);
		if (err)
			goto err_cleanup;
	}

	err = __sdhci_add_host(host);
	if (err)
		goto err_cleanup;

	if (c->use_runtime_pm) {
		pm_runtime_set_active(dev);
		pm_suspend_ignore_children(dev, 1);
		pm_runtime_set_autosuspend_delay(dev, 50);
		pm_runtime_use_autosuspend(dev);
		pm_runtime_enable(dev);
	}

	device_enable_async_suspend(dev);

	return 0;

err_cleanup:
	sdhci_cleanup_host(c->host);
err_free:
	if (c->slot && c->slot->free_slot)
		c->slot->free_slot(pdev);

	sdhci_free_host(c->host);
	return err;
}

static int sdhci_acpi_remove(struct platform_device *pdev)
{
	struct sdhci_acpi_host *c = platform_get_drvdata(pdev);
	struct device *dev = &pdev->dev;
	int dead;

	if (c->use_runtime_pm) {
		pm_runtime_get_sync(dev);
		pm_runtime_disable(dev);
		pm_runtime_put_noidle(dev);
	}

	if (c->slot && c->slot->remove_slot)
		c->slot->remove_slot(pdev);

	dead = (sdhci_readl(c->host, SDHCI_INT_STATUS) == ~0);
	sdhci_remove_host(c->host, dead);

	if (c->slot && c->slot->free_slot)
		c->slot->free_slot(pdev);

	sdhci_free_host(c->host);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int sdhci_acpi_suspend(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct sdhci_host *host = c->host;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	return sdhci_suspend_host(host);
}

static int sdhci_acpi_resume(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	sdhci_acpi_byt_setting(&c->pdev->dev);

	return sdhci_resume_host(c->host);
}

#endif

#ifdef CONFIG_PM

static int sdhci_acpi_runtime_suspend(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);
	struct sdhci_host *host = c->host;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	return sdhci_runtime_suspend_host(host);
}

static int sdhci_acpi_runtime_resume(struct device *dev)
{
	struct sdhci_acpi_host *c = dev_get_drvdata(dev);

	sdhci_acpi_byt_setting(&c->pdev->dev);

	return sdhci_runtime_resume_host(c->host);
}

#endif

static const struct dev_pm_ops sdhci_acpi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_acpi_suspend, sdhci_acpi_resume)
	SET_RUNTIME_PM_OPS(sdhci_acpi_runtime_suspend,
			sdhci_acpi_runtime_resume, NULL)
};

static struct platform_driver sdhci_acpi_driver = {
	.driver = {
		.name			= "sdhci-acpi",
		.acpi_match_table	= sdhci_acpi_ids,
		.pm			= &sdhci_acpi_pm_ops,
	},
	.probe	= sdhci_acpi_probe,
	.remove	= sdhci_acpi_remove,
};

module_platform_driver(sdhci_acpi_driver);

MODULE_DESCRIPTION("Secure Digital Host Controller Interface ACPI driver");
MODULE_AUTHOR("Adrian Hunter");
MODULE_LICENSE("GPL v2");
