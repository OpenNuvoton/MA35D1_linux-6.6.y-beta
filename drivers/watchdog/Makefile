# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the WatchDog device drivers.
#

# The WatchDog Timer Driver Core.
obj-$(CONFIG_WATCHDOG_CORE)	+= watchdog.o

watchdog-objs	+= watchdog_core.o watchdog_dev.o

watchdog-$(CONFIG_WATCHDOG_PRETIMEOUT_GOV)	+= watchdog_pretimeout.o
watchdog-$(CONFIG_WATCHDOG_HRTIMER_PRETIMEOUT)	+= watchdog_hrtimer_pretimeout.o

obj-$(CONFIG_WATCHDOG_PRETIMEOUT_GOV_NOOP)	+= pretimeout_noop.o
obj-$(CONFIG_WATCHDOG_PRETIMEOUT_GOV_PANIC)	+= pretimeout_panic.o

# Only one watchdog can succeed. We probe the ISA/PCI/USB based
# watchdog-cards first, then the architecture specific watchdog
# drivers and then the architecture independent "softdog" driver.
# This means that if your ISA/PCI/USB card isn't detected that
# you can fall back to an architecture specific driver and if
# that also fails then you can fall back to the software watchdog
# to give you some cover.

# ISA-based Watchdog Cards
obj-$(CONFIG_PCWATCHDOG) += pcwd.o
obj-$(CONFIG_MIXCOMWD) += mixcomwd.o
obj-$(CONFIG_WDT) += wdt.o

# PCI-based Watchdog Cards
obj-$(CONFIG_PCIPCWATCHDOG) += pcwd_pci.o
obj-$(CONFIG_WDTPCI) += wdt_pci.o

# USB-based Watchdog Cards
obj-$(CONFIG_USBPCWATCHDOG) += pcwd_usb.o

# ALPHA Architecture

# ARM Architecture
obj-$(CONFIG_ARM_SP805_WATCHDOG) += sp805_wdt.o
obj-$(CONFIG_ARM_SBSA_WATCHDOG) += sbsa_gwdt.o
obj-$(CONFIG_ARMADA_37XX_WATCHDOG) += armada_37xx_wdt.o
obj-$(CONFIG_ASM9260_WATCHDOG) += asm9260_wdt.o
obj-$(CONFIG_AT91RM9200_WATCHDOG) += at91rm9200_wdt.o
obj-$(CONFIG_AT91SAM9X_WATCHDOG) += at91sam9_wdt.o
obj-$(CONFIG_CADENCE_WATCHDOG) += cadence_wdt.o
obj-$(CONFIG_OMAP_WATCHDOG) += omap_wdt.o
obj-$(CONFIG_TWL4030_WATCHDOG) += twl4030_wdt.o
obj-$(CONFIG_21285_WATCHDOG) += wdt285.o
obj-$(CONFIG_977_WATCHDOG) += wdt977.o
obj-$(CONFIG_FTWDT010_WATCHDOG) += ftwdt010_wdt.o
obj-$(CONFIG_IXP4XX_WATCHDOG) += ixp4xx_wdt.o
obj-$(CONFIG_S3C2410_WATCHDOG) += s3c2410_wdt.o
obj-$(CONFIG_SA1100_WATCHDOG) += sa1100_wdt.o
obj-$(CONFIG_SAMA5D4_WATCHDOG) += sama5d4_wdt.o
obj-$(CONFIG_DW_WATCHDOG) += dw_wdt.o
obj-$(CONFIG_EP93XX_WATCHDOG) += ep93xx_wdt.o
obj-$(CONFIG_PNX4008_WATCHDOG) += pnx4008_wdt.o
obj-$(CONFIG_DAVINCI_WATCHDOG) += davinci_wdt.o
obj-$(CONFIG_K3_RTI_WATCHDOG) += rti_wdt.o
obj-$(CONFIG_ORION_WATCHDOG) += orion_wdt.o
obj-$(CONFIG_SUNXI_WATCHDOG) += sunxi_wdt.o
obj-$(CONFIG_RN5T618_WATCHDOG) += rn5t618_wdt.o
obj-$(CONFIG_NPCM7XX_WATCHDOG) += npcm_wdt.o
obj-$(CONFIG_STMP3XXX_RTC_WATCHDOG) += stmp3xxx_rtc_wdt.o
obj-$(CONFIG_TS4800_WATCHDOG) += ts4800_wdt.o
obj-$(CONFIG_TS72XX_WATCHDOG) += ts72xx_wdt.o
obj-$(CONFIG_IMX2_WDT) += imx2_wdt.o
obj-$(CONFIG_IMX_SC_WDT) += imx_sc_wdt.o
obj-$(CONFIG_IMX7ULP_WDT) += imx7ulp_wdt.o
obj-$(CONFIG_DB500_WATCHDOG) += db8500_wdt.o
obj-$(CONFIG_RETU_WATCHDOG) += retu_wdt.o
obj-$(CONFIG_BCM2835_WDT) += bcm2835_wdt.o
obj-$(CONFIG_MOXART_WDT) += moxart_wdt.o
obj-$(CONFIG_ST_LPC_WATCHDOG) += st_lpc_wdt.o
obj-$(CONFIG_QCOM_WDT) += qcom-wdt.o
obj-$(CONFIG_BCM_KONA_WDT) += bcm_kona_wdt.o
obj-$(CONFIG_TEGRA_WATCHDOG) += tegra_wdt.o
obj-$(CONFIG_MESON_GXBB_WATCHDOG) += meson_gxbb_wdt.o
obj-$(CONFIG_MESON_WATCHDOG) += meson_wdt.o
obj-$(CONFIG_MEDIATEK_WATCHDOG) += mtk_wdt.o
obj-$(CONFIG_DIGICOLOR_WATCHDOG) += digicolor_wdt.o
obj-$(CONFIG_LPC18XX_WATCHDOG) += lpc18xx_wdt.o
obj-$(CONFIG_BCM7038_WDT) += bcm7038_wdt.o
obj-$(CONFIG_RENESAS_WDT) += renesas_wdt.o
obj-$(CONFIG_RENESAS_RZAWDT) += rza_wdt.o
obj-$(CONFIG_RENESAS_RZN1WDT) += rzn1_wdt.o
obj-$(CONFIG_RENESAS_RZG2LWDT) += rzg2l_wdt.o
obj-$(CONFIG_ASPEED_WATCHDOG) += aspeed_wdt.o
obj-$(CONFIG_STM32_WATCHDOG) += stm32_iwdg.o
obj-$(CONFIG_UNIPHIER_WATCHDOG) += uniphier_wdt.o
obj-$(CONFIG_RTD119X_WATCHDOG) += rtd119x_wdt.o
obj-$(CONFIG_SPRD_WATCHDOG) += sprd_wdt.o
obj-$(CONFIG_PM8916_WATCHDOG) += pm8916_wdt.o
obj-$(CONFIG_ARM_SMC_WATCHDOG) += arm_smc_wdt.o
obj-$(CONFIG_GXP_WATCHDOG) += gxp-wdt.o
obj-$(CONFIG_VISCONTI_WATCHDOG) += visconti_wdt.o
obj-$(CONFIG_MSC313E_WATCHDOG) += msc313e_wdt.o
obj-$(CONFIG_APPLE_WATCHDOG) += apple_wdt.o
obj-$(CONFIG_SUNPLUS_WATCHDOG) += sunplus_wdt.o
obj-$(CONFIG_MARVELL_GTI_WDT) += marvell_gti_wdt.o
obj-$(CONFIG_MA35D1_WDT) += ma35d1_wdt.o
obj-$(CONFIG_MA35D1_WWDT) += ma35d1_wwdt.o
obj-$(CONFIG_MA35D0_WDT) += ma35d0_wdt.o
obj-$(CONFIG_MA35D0_WWDT) += ma35d0_wwdt.o
obj-$(CONFIG_MA35H0_WDT) += ma35h0_wdt.o
obj-$(CONFIG_MA35H0_WWDT) += ma35h0_wwdt.o

# X86 (i386 + ia64 + x86_64) Architecture
obj-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
obj-$(CONFIG_ADVANTECH_WDT) += advantechwdt.o
obj-$(CONFIG_ADVANTECH_EC_WDT) += advantech_ec_wdt.o
obj-$(CONFIG_ALIM1535_WDT) += alim1535_wdt.o
obj-$(CONFIG_ALIM7101_WDT) += alim7101_wdt.o
obj-$(CONFIG_EBC_C384_WDT) += ebc-c384_wdt.o
obj-$(CONFIG_EXAR_WDT) += exar_wdt.o
obj-$(CONFIG_F71808E_WDT) += f71808e_wdt.o
obj-$(CONFIG_SP5100_TCO) += sp5100_tco.o
obj-$(CONFIG_GEODE_WDT) += geodewdt.o
obj-$(CONFIG_SC520_WDT) += sc520_wdt.o
obj-$(CONFIG_SBC_FITPC2_WATCHDOG) += sbc_fitpc2_wdt.o
obj-$(CONFIG_EUROTECH_WDT) += eurotechwdt.o
obj-$(CONFIG_IB700_WDT) += ib700wdt.o
obj-$(CONFIG_IBMASR) += ibmasr.o
obj-$(CONFIG_WAFER_WDT) += wafer5823wdt.o
obj-$(CONFIG_I6300ESB_WDT) += i6300esb.o
obj-$(CONFIG_IE6XX_WDT) += ie6xx_wdt.o
obj-$(CONFIG_ITCO_WDT) += iTCO_wdt.o
ifeq ($(CONFIG_ITCO_VENDOR_SUPPORT),y)
obj-$(CONFIG_ITCO_WDT) += iTCO_vendor_support.o
endif
obj-$(CONFIG_IT8712F_WDT) += it8712f_wdt.o
obj-$(CONFIG_IT87_WDT) += it87_wdt.o
obj-$(CONFIG_HP_WATCHDOG) += hpwdt.o
obj-$(CONFIG_KEMPLD_WDT) += kempld_wdt.o
obj-$(CONFIG_SC1200_WDT) += sc1200wdt.o
obj-$(CONFIG_SCx200_WDT) += scx200_wdt.o
obj-$(CONFIG_PC87413_WDT) += pc87413_wdt.o
obj-$(CONFIG_NV_TCO) += nv_tco.o
obj-$(CONFIG_RDC321X_WDT) += rdc321x_wdt.o
obj-$(CONFIG_60XX_WDT) += sbc60xxwdt.o
obj-$(CONFIG_SBC8360_WDT) += sbc8360.o
obj-$(CONFIG_SBC7240_WDT) += sbc7240_wdt.o
obj-$(CONFIG_CPU5_WDT) += cpu5wdt.o
obj-$(CONFIG_SMSC_SCH311X_WDT) += sch311x_wdt.o
obj-$(CONFIG_SMSC37B787_WDT) += smsc37b787_wdt.o
obj-$(CONFIG_TQMX86_WDT) += tqmx86_wdt.o
obj-$(CONFIG_VIA_WDT) += via_wdt.o
obj-$(CONFIG_W83627HF_WDT) += w83627hf_wdt.o
obj-$(CONFIG_W83877F_WDT) += w83877f_wdt.o
obj-$(CONFIG_W83977F_WDT) += w83977f_wdt.o
obj-$(CONFIG_MACHZ_WDT) += machzwd.o
obj-$(CONFIG_SBC_EPX_C3_WATCHDOG) += sbc_epx_c3.o
obj-$(CONFIG_INTEL_MID_WATCHDOG) += intel-mid_wdt.o
obj-$(CONFIG_INTEL_MEI_WDT) += mei_wdt.o
obj-$(CONFIG_NI903X_WDT) += ni903x_wdt.o
obj-$(CONFIG_NIC7018_WDT) += nic7018_wdt.o
obj-$(CONFIG_MLX_WDT) += mlx_wdt.o
obj-$(CONFIG_KEEMBAY_WATCHDOG) += keembay_wdt.o
obj-$(CONFIG_SIEMENS_SIMATIC_IPC_WDT) += simatic-ipc-wdt.o

# M68K Architecture
obj-$(CONFIG_M54xx_WATCHDOG) += m54xx_wdt.o

# MicroBlaze Architecture
obj-$(CONFIG_XILINX_WATCHDOG) += of_xilinx_wdt.o
obj-$(CONFIG_XILINX_WINDOW_WATCHDOG) += xilinx_wwdt.o

# MIPS Architecture
obj-$(CONFIG_ATH79_WDT) += ath79_wdt.o
obj-$(CONFIG_BCM47XX_WDT) += bcm47xx_wdt.o
obj-$(CONFIG_RC32434_WDT) += rc32434_wdt.o
obj-$(CONFIG_INDYDOG) += indydog.o
obj-$(CONFIG_JZ4740_WDT) += jz4740_wdt.o
obj-$(CONFIG_WDT_MTX1) += mtx-1_wdt.o
obj-$(CONFIG_SIBYTE_WDOG) += sb_wdog.o
obj-$(CONFIG_AR7_WDT) += ar7_wdt.o
obj-$(CONFIG_TXX9_WDT) += txx9wdt.o
obj-$(CONFIG_OCTEON_WDT) += octeon-wdt.o
octeon-wdt-y := octeon-wdt-main.o octeon-wdt-nmi.o
obj-$(CONFIG_LANTIQ_WDT) += lantiq_wdt.o
obj-$(CONFIG_LOONGSON1_WDT) += loongson1_wdt.o
obj-$(CONFIG_RALINK_WDT) += rt2880_wdt.o
obj-$(CONFIG_IMGPDC_WDT) += imgpdc_wdt.o
obj-$(CONFIG_MT7621_WDT) += mt7621_wdt.o
obj-$(CONFIG_PIC32_WDT) += pic32-wdt.o
obj-$(CONFIG_PIC32_DMT) += pic32-dmt.o
obj-$(CONFIG_REALTEK_OTTO_WDT) += realtek_otto_wdt.o

# PARISC Architecture

# POWERPC Architecture
obj-$(CONFIG_GEF_WDT) += gef_wdt.o
obj-$(CONFIG_8xxx_WDT) += mpc8xxx_wdt.o
obj-$(CONFIG_PIKA_WDT) += pika_wdt.o
obj-$(CONFIG_BOOKE_WDT) += booke_wdt.o
obj-$(CONFIG_MEN_A21_WDT) += mena21_wdt.o

# PPC64 Architecture
obj-$(CONFIG_PSERIES_WDT) += pseries-wdt.o
obj-$(CONFIG_WATCHDOG_RTAS) += wdrtas.o

# RISC-V Architecture
obj-$(CONFIG_STARFIVE_WATCHDOG) += starfive-wdt.o

# S390 Architecture
obj-$(CONFIG_DIAG288_WATCHDOG) += diag288_wdt.o

# SUPERH (sh + sh64) Architecture
obj-$(CONFIG_SH_WDT) += shwdt.o

# SPARC Architecture

# SPARC64 Architecture

obj-$(CONFIG_WATCHDOG_RIO)		+= riowd.o
obj-$(CONFIG_WATCHDOG_CP1XXX)		+= cpwd.o
obj-$(CONFIG_WATCHDOG_SUN4V)		+= sun4v_wdt.o

# XTENSA Architecture

# Xen
obj-$(CONFIG_XEN_WDT) += xen_wdt.o

# Architecture Independent
obj-$(CONFIG_BD957XMUF_WATCHDOG) += bd9576_wdt.o
obj-$(CONFIG_DA9052_WATCHDOG) += da9052_wdt.o
obj-$(CONFIG_DA9055_WATCHDOG) += da9055_wdt.o
obj-$(CONFIG_DA9062_WATCHDOG) += da9062_wdt.o
obj-$(CONFIG_DA9063_WATCHDOG) += da9063_wdt.o
obj-$(CONFIG_GPIO_WATCHDOG)	+= gpio_wdt.o
obj-$(CONFIG_WDAT_WDT) += wdat_wdt.o
obj-$(CONFIG_WM831X_WATCHDOG) += wm831x_wdt.o
obj-$(CONFIG_WM8350_WATCHDOG) += wm8350_wdt.o
obj-$(CONFIG_MAX63XX_WATCHDOG) += max63xx_wdt.o
obj-$(CONFIG_MAX77620_WATCHDOG) += max77620_wdt.o
obj-$(CONFIG_ZIIRAVE_WATCHDOG) += ziirave_wdt.o
obj-$(CONFIG_SOFT_WATCHDOG) += softdog.o
obj-$(CONFIG_MENF21BMC_WATCHDOG) += menf21bmc_wdt.o
obj-$(CONFIG_MENZ069_WATCHDOG) += menz69_wdt.o
obj-$(CONFIG_RAVE_SP_WATCHDOG) += rave-sp-wdt.o
obj-$(CONFIG_STPMIC1_WATCHDOG) += stpmic1_wdt.o
obj-$(CONFIG_SL28CPLD_WATCHDOG) += sl28cpld_wdt.o
