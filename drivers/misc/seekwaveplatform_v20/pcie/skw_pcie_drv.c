/*
 * Copyright (C) 2022 Seekwave Tech Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/platform_device.h>
#include <uapi/linux/sched/types.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/version.h>
#include <linux/semaphore.h>
#include <linux/pm_runtime.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include "skw_edma_drv.h"
#include "skw_pcie_drv.h"
#include "skw_pcie_log.h"
#include "skw_edma_reg.h"
#include "skw_pcie_debugfs.h"

#define SWT6652_V2
//#define SBDRSM_DEBUG

static struct wcn_pcie_info *g_pcie_dev;

static int pcie_int = 1;
module_param(pcie_int, int, S_IRUGO);
MODULE_PARM_DESC(pcie_int, "1-msi, 2-legacy, 3-msix, if no param, msi default");


extern void skw_pcie_exception_work(struct work_struct *work);

struct wcn_pcie_info *get_pcie_device_info(void)
{
	return g_pcie_dev;
}

static int inline is_msi_irq_wifi_takeover(struct wcn_pcie_info *priv, int irq)
{
	int ch_id;

	if (priv->msix_en == 1) {
		ch_id = irq/2;
	} else
		ch_id = irq;

	if (ch_id == EDMA_WIFI_TX0_FREE_ADDR || ch_id == EDMA_WIFI_TX1_FREE_ADDR
		|| ch_id == EDMA_WIFI_RX0_PKT_ADDR || ch_id == EDMA_WIFI_RX1_PKT_ADDR
		|| ch_id == EDMA_WIFI_RX0_FILTER_DATA_CHN || ch_id == EDMA_WIFI_RX1_FILTER_DATA_CNH
		|| ch_id == EDMA_WIFI_TX0_PACKET_ADDR || ch_id == EDMA_WIFI_TX1_PACKET_ADDR)

		return 1;
	else
		return 0;
}

static int skw_pcie_msi_irq(int irq, void *arg)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();

	//PCIE_DBG("vector=%d\n", irq);
	irq = *(u16 *)arg;
	PCIE_DBG("irq_num=%d\n", irq);
	//skw_edma_lock_event();
	if (is_msi_irq_wifi_takeover(priv, irq))
		msi_irq_wifi_takeover_handler(irq);
	else
		msi_edma_channel_irq_handler(irq);

	return IRQ_HANDLED;
}

static int legacy_pcie_irq_handle(struct wcn_pcie_info *priv)
{
	u32 val;

	val = (skw_pcie_read32(0x40190050) & 0xff00) >> 8;
	PCIE_DBG("pcie legacy int sts=0x%x\n", val);
	skw_pcie_write32(0x4019004c, val);
	return IRQ_HANDLED;
}

static int pcie_legacy_irq(int irq, void *arg)
{
	struct wcn_pcie_info *priv = arg;

	PCIE_DBG("irq_num=%d\n", irq);
	if (skw_pcie_read32(0x40188004))
		legacy_edma_irq_handle();
	else if (skw_pcie_read32(0x40190050) & 0xff00)
		legacy_pcie_irq_handle(priv);

	return IRQ_HANDLED;
}

static int ep_address_mapping(struct wcn_pcie_info *priv)
{
	u64 val;
	//ib: map 2M(0x2000000) (0x40000000-0x401fffff)
	val = priv->mem_pciaddr;
	writel_relaxed((val & 0xffff0000) | 20, priv->pciaux + IBREG0_OFFSET_ADDR + 4);
	writel_relaxed((val >> 32) & 0xffffffff, priv->pciaux + IBREG0_OFFSET_ADDR + 8);
	writel_relaxed(0x40000000, priv->pciaux + IBREG0_OFFSET_ADDR + 0xc);
	writel_relaxed(1, priv->pciaux + IBREG0_OFFSET_ADDR + 0);

#ifdef SWT6652_V2
	//ob0
	writel_relaxed(31, priv->pciaux + OBREG0_OFFSET_ADDR + 4);
	writel_relaxed(0x00000080, priv->pciaux + OBREG0_OFFSET_ADDR + 8);
	writel_relaxed(0x00000000, priv->pciaux + OBREG0_OFFSET_ADDR + 0xc);
	writel_relaxed(0x00000000, priv->pciaux + OBREG0_OFFSET_ADDR + 0x10);
	writel_relaxed(0xff, priv->pciaux + OBREG0_OFFSET_ADDR + 0x18);
	writel_relaxed(1, priv->pciaux + OBREG0_OFFSET_ADDR + 0);
#else
	//ob0
	writel_relaxed(31, priv->pciaux + OBREG0_OFFSET_ADDR + 4);
	writel_relaxed(0x00000080, priv->pciaux + OBREG0_OFFSET_ADDR + 8);
	writel_relaxed(0x00000000, priv->pciaux + OBREG0_OFFSET_ADDR + 0xc);
	writel_relaxed(0x00000000, priv->pciaux + OBREG0_OFFSET_ADDR + 0x10);
	writel_relaxed(1, priv->pciaux + OBREG0_OFFSET_ADDR + 0);

	//ob1
	writel_relaxed(31, priv->pciaux + OBREG1_OFFSET_ADDR + 4);
	writel_relaxed(0x00000081, priv->pciaux + OBREG1_OFFSET_ADDR + 8);
	writel_relaxed(0x00000000, priv->pciaux + OBREG1_OFFSET_ADDR + 0xc);
	writel_relaxed(0x00000001, priv->pciaux + OBREG1_OFFSET_ADDR + 0x10);
	writel_relaxed(1, priv->pciaux + OBREG1_OFFSET_ADDR + 0);
#endif
	return 0;
}


u32 skw_pcie_read32(u32 reg_addr)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();
	char *address = priv->pcimem;

	reg_addr -= 0x40000000;
	address += reg_addr;
	rmb();
	return readl_relaxed(address);
}

void skw_pcie_write32(u32 reg_addr, u32 value)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();
	char *address = priv->pcimem;

	reg_addr -= 0x40000000;
	address += reg_addr;
	writel_relaxed(value, address);
	wmb();
}

void skw_pcie_setbit(u32 reg_addr, u32 bits)
{
	int val;
	struct wcn_pcie_info *priv = get_pcie_device_info();
	char *address = priv->pcimem;

	reg_addr -= 0x40000000;
	address += reg_addr;
	val = readl_relaxed(address);
	val |= bits;
	writel_relaxed(val, address);
}

void skw_pcie_clrbit(u32 reg_addr, u32 bits)
{
	int val;
	struct wcn_pcie_info *priv = get_pcie_device_info();
	char *address = priv->pcimem;

	reg_addr -= 0x40000000;
	address += reg_addr;
	val = readl_relaxed(address);
	val &= ~bits;
	writel_relaxed(val, address);
}

u64 edma_phyaddr_to_pcieaddr(u64 phy_addr)
{
	u64 val;

	val = 0x8000000000 + phy_addr;
	return val;
}

u64 edma_virtaddr_to_pcieaddr(void *virt_addr)
{
	u64 val;
	u64 phy_addr;

	phy_addr = virt_to_phys(virt_addr);
	val = 0x8000000000 + phy_addr;
	return val;
}

u64 edma_pcieaddr_to_phyaddr(u64 phy_addr)
{
	u64 val;

	val = phy_addr - 0x8000000000;
	return val;
}

u64 edma_pcieaddr_to_virtaddr(u64 phy_addr)
{
	u64 val;
	u64 virt_addr;

	val = phy_addr - 0x8000000000;
	virt_addr = (u64)phys_to_virt(val);
	return virt_addr;
}

static void skw_pcie_remove(struct pci_dev *pdev)
{
	int i;
	struct wcn_pcie_info *priv;

	PCIE_DBG("[+]\n");
	priv = (struct wcn_pcie_info *) pci_get_drvdata(pdev);

	skw_edma_deinit();

	if (priv->legacy_en == 1) {
		PCIE_INFO("free INTx int");
		free_irq(priv->irq, (void *)priv);
	}
	if (priv->msi_en == 1) {
		PCIE_INFO("free MSI");
#ifndef CONFIG_SKW_MSI_AS_LEGACY
		for (i = 0; i < priv->irq_num; i++)
			free_irq(priv->irq + i, &priv->msix_vec_idx[i]);
#else
		free_irq(priv->irq, (void *)priv);
#endif
		pci_disable_msi(pdev);
	}
	if (priv->msix_en == 1) {
		PCIE_INFO("free MSI-X");
		for (i = 0; i < priv->irq_num; i++)
			free_irq(priv->msix[i].vector, &priv->msix[i].entry);

		pci_disable_msix(pdev);
		kfree(priv->msix);
	}

	free_irq(priv->gpio_irq_num, NULL);
	disable_irq_wake(priv->gpio_irq_num);

	iounmap(priv->pcimem);
	iounmap(priv->pciaux);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	skw_pcie_remove_loopcheck_thread(5);
	PCIE_DBG("[-]\n");
}

int get_service_busy_sts(void)
{
	int ret;
	u32 status;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	ret = readl_poll_timeout(priv->pcimem + 0x108160,
				 status, ((status & WIFI_DO_SUSPEND_MASK) == 0), 10, 2000);
	if (ret) {
		PCIE_ERR("Service doesn't allow suspend!!!\n");
		return -1;
	}

	return 0;
}

int skw_notify_ep_enter_l2(void)
{
	u32 val, status;
	int ret;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	val = skw_pcie_read32(SKW_CP_PMU_SW_REG);
	val &= ~L2_SHAKE_MASK;
	val |= 0xd2;
	skw_pcie_write32(SKW_CP_PMU_SW_REG, val);
	ret = readl_poll_timeout(priv->pcimem + 0x108160,
				 status, ((status & L2_SHAKE_MASK) == 0xd2), 5, 100);
	if (ret) {
		PCIE_ERR("Failed to read SKW_CP_PMU_SW_REG\n");
		return -1;
	}
	/* for cp no edma req */
	mdelay(1);
	return 0;
}

void skw_notify_ep_exit_l2(void)
{
	u32 val;

	val = skw_pcie_read32(SKW_CP_PMU_SW_REG);
	val &= ~L2_SHAKE_MASK;
	skw_pcie_write32(SKW_CP_PMU_SW_REG, val);
}

static int skw_ep_suspend(struct device *dev)
{
	int ret;
#ifndef SWT6652_V2
	int i;
	int pba_entries_num;
	u32 pba_table_offset;
	u8 pba_bir;
	u32 val;
#endif

	struct pci_dev *pdev = to_pci_dev(dev);
	struct wcn_pcie_info *priv = pci_get_drvdata(pdev);

	if (!pdev)
		return -ENODEV;

	PCIE_INFO("[+]\n");

	ret = get_service_busy_sts();
	if (ret)
		return -EBUSY;

	/* notify CP enter L2 */
	ret = skw_notify_ep_enter_l2();
	if (ret)
		goto busy;

	/* pause edma */
	ret = skw_edma_pause();
	if (ret) {
		skw_edma_restore();
		goto busy;
	}
#ifndef SWT6652_V2
	/* close L1sub */
		/* disable L1SS */
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	val &= ~PCI_L1SS_CTL1_L1SS_MASK;
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, val);
		/* disable L1 Entry & CLKREQ */
	pcie_capability_clear_and_set_word(pdev, PCI_EXP_LNKCTL,
					PCI_EXP_LNKCTL_ASPMC | PCI_EXP_LNKCTL_CLKREQ_EN, 0);

	/* clear PBA */
	pci_read_config_dword(pdev, pdev->msix_cap + PCI_MSIX_PBA,
			      &pba_table_offset);
	PCIE_INFO("pdev->msix_cap=0x%x\n", pdev->msix_cap);
	PCIE_INFO("pba_table_offset=0x%x\n", pba_table_offset);
	pba_bir = (u8)(pba_table_offset & PCI_MSIX_PBA_BIR);

	pba_entries_num = (priv->irq_num % 64)?(priv->irq_num/64 + 1):(priv->irq_num/64);
	PCIE_INFO("pba_entries_num=0x%x\n", pba_entries_num);
	PCIE_INFO("priv->irq_num=0x%x\n", priv->irq_num);

	for (i=0;i<pba_entries_num*2;i++) {
		PCIE_INFO("1. pba[%d]=0x%08x\n", i, readl_relaxed(priv->pciaux+ pba_table_offset + i*4));
		writel_relaxed(0, priv->pciaux + pba_table_offset + i*4);
		PCIE_INFO("2. pba[%d]=0x%08x\n", i, readl_relaxed(priv->pciaux+ pba_table_offset + i*4));
	}
#endif
	pci_save_state(to_pci_dev(dev));
	priv->saved_state = pci_store_saved_state(to_pci_dev(dev));
	ret = pci_enable_wake(pdev, PCI_D3hot, 1);
	PCIE_INFO("pci_enable_wake(PCI_D3hot) ret %d\n", ret);
	ret = pci_set_power_state(pdev, PCI_D3hot);
	PCIE_INFO("pci_set_power_state(PCI_D3hot) ret %d\n", ret);

#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	PCIE_INFO("[-]\n");
	return 0;
busy:
	skw_notify_ep_exit_l2();
	return -EBUSY;
}

static int skw_ep_resume(struct device *dev)
{
	int ret;
#ifndef SWT6652_V2
	int i;
	u32 val;
	u32 pba_table_offset;
	int pba_entries_num;
	u8 pba_bir;
#endif
	struct pci_dev *pdev = to_pci_dev(dev);
	struct wcn_pcie_info *priv = pci_get_drvdata(pdev);

	PCIE_INFO("[+]\n");

	if (!pdev) {
		return -ENODEV;
	}

#ifndef SWT6652_V2
	pci_read_config_dword(pdev, pdev->msix_cap + PCI_MSIX_PBA,
			      &pba_table_offset);
	PCIE_INFO("pdev->msix_cap=0x%x\n", pdev->msix_cap);
	PCIE_INFO("pba_table_offset=0x%x\n", pba_table_offset);
	pba_bir = (u8)(pba_table_offset & PCI_MSIX_PBA_BIR);
	pba_entries_num = (priv->irq_num % 64)?(priv->irq_num/64 + 1):(priv->irq_num/64);
	for (i=0;i<pba_entries_num*2;i++) {
		//writel_relaxed(0,priv->pciaux + pba_table_offset + i*4);
		PCIE_INFO("pba[%d]=0x%08x\n", i, readl_relaxed(priv->pciaux + pba_table_offset + i*4));
	}
#endif
	pci_load_and_free_saved_state(to_pci_dev(dev), &priv->saved_state);
#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	pci_restore_state(to_pci_dev(dev));
#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	ret = pci_set_power_state(pdev, PCI_D0);
#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	PCIE_INFO("pci_set_power_state(PCI_D0) ret %d\n", ret);
	ret = pci_enable_wake(pdev, PCI_D0, 0);
#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	PCIE_INFO("pci_enable_wake(PCI_D0) ret %d\n", ret);

	ep_address_mapping(priv);

#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	skw_edma_restore();
	skw_notify_ep_exit_l2();

#ifndef SWT6652_V2
	/* enable L1sub */
	//enable L1SS
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	val |= PCI_L1SS_CTL1_L1SS_MASK;
	pci_write_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, val);

	//enable L1 Entry & CLKREQ
	pcie_capability_clear_and_set_word(pdev, PCI_EXP_LNKCTL,
					0,
					PCI_EXP_LNKCTL_ASPMC | PCI_EXP_LNKCTL_CLKREQ_EN);
#endif
#ifdef SBDRSM_DEBUG
	pci_read_config_dword(pdev, pdev->l1ss + PCI_L1SS_CTL1, &val);
	PCIE_INFO("[PCI_L1SS_CTL1]=0x%x\n", val);
	pcie_capability_read_dword(pdev, PCI_EXP_LNKCTL, &val);
	PCIE_INFO("[PCI_EXP_LNKCTL]=0x%x\n", val);
#endif
	PCIE_INFO("[-]\n");
	return 0;
}

irqreturn_t skw_gpio_irq_handler(int irq, void *dev_id) //interrupt
{
	return IRQ_HANDLED;
}

int skw_pcie_host_irq_init(unsigned int irq_gpio_num)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();
	int ret = 0;

	PCIE_INFO("gpio_pewake:%d\n", irq_gpio_num);
	if (irq_gpio_num < 0)
		return -EINVAL;

	priv->gpio_irq_num = gpio_to_irq(irq_gpio_num);
	priv->irq_trigger_type = IRQF_TRIGGER_FALLING;
	if (priv->gpio_irq_num) {
		ret = request_irq(priv->gpio_irq_num, skw_gpio_irq_handler,
				priv->irq_trigger_type | IRQF_ONESHOT, "skw-pewake", NULL);
		if (ret != 0) {
			free_irq(priv->gpio_irq_num, NULL);
			PCIE_ERR("request gpio irq fail ret=%d\n", ret);
			return -1;
		} else {
			PCIE_DBG("gpio request_irq=%d!\n", priv->gpio_irq_num);
		}
	}
	enable_irq_wake(priv->gpio_irq_num);
	return ret;
}

void skw_pcie_rescan_bus(void)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();

	PCIE_INFO("[+]\n");
	pci_stop_and_remove_bus_device_locked(priv->dev);
	PCIE_INFO("\n");
	pci_lock_rescan_remove();
	PCIE_INFO("\n");
	pci_rescan_bus(priv->dev->bus);
	PCIE_INFO("\n");
	pci_unlock_rescan_remove();
	PCIE_INFO("\n");
	PCIE_INFO("[-]\n");
}

void skw_pcie_recovery_work(struct work_struct *work)
{
	int ret;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	skw_pcie_rescan_bus();

	ret = skw_pcie_boot_cp(RECOVERY_BOOT);
	if(ret!=0){
		PCIE_ERR("CP RESET fail \n");
		return;
	}
	skw_pcie_bind_wifi_driver(priv->dev);
	PCIE_INFO("SKW PCIe Recovery ok\n");
}

int check_chipid(void)
{
	int ret=0;
	unsigned int tmp_chipid0;
	unsigned int tmp_chipid1;
	unsigned int tmp_chipid2;
	unsigned int tmp_chipid3;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	tmp_chipid0 =  skw_pcie_read32(SKW_CHIP_ID0);
	tmp_chipid1 =  skw_pcie_read32(SKW_CHIP_ID1);
	tmp_chipid2 =  skw_pcie_read32(SKW_CHIP_ID2);
	tmp_chipid3 =  skw_pcie_read32(SKW_CHIP_ID3);
	if (tmp_chipid0 ==0x33365653 && tmp_chipid1==0x3631) {
		//sprintf((char *)priv->chip_id, "%s", "SV6316");
		memcpy(&priv->chip_id, &tmp_chipid0,4);
		memcpy(&priv->chip_id[1], &tmp_chipid1,4);
		memcpy(&priv->chip_id[2], &tmp_chipid2,4);
		memcpy(&priv->chip_id[3], &tmp_chipid3,4);
		print_hex_dump(KERN_ERR, "CHIP ID: ", 0, 16, 1,priv->chip_id, 32, 1);
	} else {
		PCIE_ERR("Wrong Chip ID:%s,%s\n",(char *)&tmp_chipid0,(char *)&tmp_chipid1);
		return -1;
	}

	if (ret<0) {
		skw_pcie_err("Get Chip ID fail!\n");
		return ret;
	}
    if (!strncmp((char *)priv->chip_id, "SV6316", 6)){
            PCIE_INFO("Chip ID:%s\n", (char *)priv->chip_id);
    }

	PCIE_INFO("Chip ID:%s\n", (char *)priv->chip_id);
	return 0;
}

static int skw_pcie_legacy_int_init(struct pci_dev *pdev)
{
	int ret = 0;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	ret = request_irq(priv->irq,
			(irq_handler_t) (&pcie_legacy_irq),
			IRQF_SHARED,
			DRV_NAME, (void *)priv);
	if (ret) {
		PCIE_ERR("request_irq(%d), error %d\n", priv->irq, ret);
		return -1;
	}
	PCIE_DBG("request_irq(%d) ok\n", priv->irq);

	return ret;
}

static int skw_pcie_msi_int_init(struct pci_dev *pdev)
{
	int ret = 0;
	struct wcn_pcie_info *priv = get_pcie_device_info();
#ifndef CONFIG_SKW_MSI_AS_LEGACY
	int i;

	priv->irq_num = pci_msi_vec_count(pdev);
	PCIE_DBG("pci_msix_vec_count ret %d\n", priv->irq_num);

	ret = pci_alloc_irq_vectors(pdev, 1, priv->irq_num, PCI_IRQ_MSI);
#else
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
#endif
	if (ret < 0) {
		PCIE_ERR("pci_enable_msi_range err=%d\n", ret);
		goto err_out;
	}
#ifndef CONFIG_SKW_MSI_AS_LEGACY
	priv->irq = pdev->irq;
	for (i = 0; i < priv->irq_num; i++) {
		priv->msix_vec_idx[i] = i;
		ret = request_irq(priv->irq + i,
				(irq_handler_t) (&skw_pcie_msi_irq),
				IRQF_SHARED, DRV_NAME, &priv->msix_vec_idx[i]);
		if (ret) {
			PCIE_ERR("request_irq(%d), error %d\n",
				priv->irq + i, ret);
			break;
		}
		PCIE_DBG("request_irq(%d) ok\n", priv->irq + i);
	}
#else /* CONFIG_SKW_MSI_AS_LEGACY */
#if defined(IRQF_SHARED)
	priv->irq = pdev->irq;
	ret = request_irq(pdev->irq, (irq_handler_t) (&pcie_legacy_irq), IRQF_SHARED, DRV_NAME, priv);
#else /* IRQF_SHARED */
	ret = request_irq(pdev->irq, (irq_handler_t) (&pcie_legacy_irq), SA_SHIRQ, DRV_NAME, priv);
#endif /* IRQF_SHARED */
#endif /* CONFIG_SKW_MSI_AS_LEGACY */

err_out:
	return ret;
}

static int skw_pcie_msix_int_init(struct pci_dev *pdev)
{
	int ret = 0, i;
	int vectors;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	vectors = pci_msix_vec_count(pdev);
	PCIE_DBG("vectors=0x%x\n", vectors);
	priv->msix = kzalloc((sizeof(struct msix_entry) * vectors), GFP_KERNEL);
	if (!priv->msix) {
		ret = -ENOMEM;
		goto err_out;
	}
	for (i = 0; i < vectors; i++)
		priv->msix[i].entry = i;
	priv->irq_num = pci_enable_msix_range(pdev, priv->msix, 1, vectors);
	if (priv->irq_num < 0) {
		PCIE_ERR("pci_enable_msix_range %d err\n", priv->irq_num);
		kfree(priv->msix);
		goto err_out;
	}
	priv->irq = priv->msix[0].vector;

	for (i = 0; i < priv->irq_num; i++) {
		PCIE_DBG("priv->irq=0x%x\n", priv->irq);
		PCIE_DBG("priv->irq_num=0x%x\n", priv->irq_num);
		PCIE_DBG("priv->msix[i].vector=0x%x\n", priv->msix[i].vector);
		ret = request_irq(priv->msix[i].vector,
				(irq_handler_t) (&skw_pcie_msi_irq),
				IRQF_SHARED, DRV_NAME, &priv->msix[i].entry);
		if (ret) {
			PCIE_ERR("request_irq(%d), error %d\n",
				priv->msix[i].vector, ret);
			break;
		}
		PCIE_DBG("request_irq(%d) ok\n", priv->msix[i].vector);
	}
err_out:
	return ret;
}

static int skw_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();
	unsigned long mem_len, aux_len;
	int ret = -ENODEV;
	int val;

	PCIE_INFO("[+]\n");
	priv->dev = pdev;
	pci_set_drvdata(pdev, priv);

	if (pci_enable_device(pdev)) {
		PCIE_ERR("cannot enable device:%s\n", pci_name(pdev));
		goto err_out;
	}
	pci_set_master(pdev);
	ret = pci_request_regions(pdev, DRV_NAME);
	if (ret) {
		goto err_out;
	}

	priv->mem_start = pci_resource_start(pdev, 2);
	mem_len = pci_resource_len(pdev, 2);
	priv->aux_start = pci_resource_start(pdev, 0);
	aux_len = pci_resource_len(pdev, 0);
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_2, (u32 *)&priv->mem_barl);
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_3, (u32 *)&priv->mem_barh);
	priv->mem_pciaddr = ((priv->mem_barh << 32) | priv->mem_barl) & ~0xf;
	PCIE_INFO("mem_pciaddr:0x%llx\n", priv->mem_pciaddr);

	priv->pcimem = ioremap(priv->mem_start, mem_len);
	if (!priv->pcimem) {
		PCIE_ERR("%s:Couldn't map region %x[%x]",
			pci_name(pdev), (int)priv->mem_start, (int)mem_len);
		ret = -1;
		goto free_region;
	}

	priv->pciaux = ioremap(priv->aux_start, aux_len);
	if (!priv->pciaux) {
		PCIE_ERR("%s:Couldn't map region %x[%x]",
			pci_name(pdev), (int)priv->aux_start, (int)aux_len);
		ret = -1;
		goto free_memmap1;
	}
	PCIE_INFO("BAR(0)(auxmem) (0x%llx 0x%lx)\n", priv->aux_start, aux_len);
	PCIE_INFO("BAR(2)(mem)   [0x%llx 0x%lx)\n", priv->mem_start, mem_len);

	priv->irq = pdev->irq;
	if (pcie_int == 1)
		priv->msi_en = 1;
	else if (pcie_int == 2)
		priv->legacy_en = 1;
	else if (pcie_int == 3)
		priv->msix_en = 1;
	else
		priv->msi_en = 1;

	PCIE_DBG("dev->irq %d\n", pdev->irq);
	PCIE_INFO("legacy %d msi_en %d, msix_en %d\n",
		priv->legacy_en, priv->msi_en, priv->msix_en);

	if (priv->legacy_en == 1) {
		ret = skw_pcie_legacy_int_init(pdev);
		if (ret)
			goto free_memmap2;
	} else if (priv->msi_en == 1) {
		ret = skw_pcie_msi_int_init(pdev);
		if (ret)
			goto free_memmap2;
	} else if (priv->msix_en == 1) {
		ret = skw_pcie_msix_int_init(pdev);
		if (ret)
			goto free_memmap2;
	}

	device_wakeup_enable(&(pdev->dev));
	ep_address_mapping(priv);
	skw_edma_init();
	init_completion(&priv->download_done);
	check_chipid();
	init_completion(&priv->edma_blk_dl_done);
	skw_pcie_bind_platform_driver(pdev);
	skw_pcie_create_loopcheck_thread(5);
	if(priv->cp_state)
		skw_pcie_bind_bt_driver(priv->dev);

	priv->service_state_map = 0;
	PCIE_INFO("ok\n");
	/* fix debug boot issue */
	val = skw_pcie_read32(0x40100030);
	val &= ~0xff00;
	val |= 0x5a00;
	skw_pcie_write32(0x40100030, val);
	return 0;


free_memmap2:
	iounmap(priv->pciaux);
free_memmap1:
	iounmap(priv->pcimem);
free_region:
	pci_release_regions(pdev);
err_out:
	kfree(priv);

	return ret;
}

const struct dev_pm_ops skw_ep_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(skw_ep_suspend, skw_ep_resume)
};


static struct pci_device_id skw_pcie_tbl[] = {
	{PCI_DEVICE(0x0043, 0x834d)},
	{PCI_DEVICE(0x1FFE, 0x6316)},
	{}
};
MODULE_DEVICE_TABLE(pci, skw_pcie_tbl);

static struct pci_driver skw_pcie_driver = {
	.name = "skw_pcie",
	.id_table = skw_pcie_tbl,
	.probe = skw_pcie_probe,
	.remove = skw_pcie_remove,
	.driver = {
		.pm = &skw_ep_pm_ops,
	},
};

static int __init skw_pcie_init(void)
{
	int ret = 0;
	struct wcn_pcie_info *priv;
	skw_pcie_debugfs_init();
	skw_pcie_log_level_init();
	PCIE_INFO("[+]\n");
	priv = kzalloc(sizeof(struct wcn_pcie_info), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	g_pcie_dev = priv;

	priv->recovery_dis_state =0;
	INIT_DELAYED_WORK(&priv->skw_pcie_recovery_work, skw_pcie_recovery_work);
	INIT_DELAYED_WORK(&priv->skw_except_work, skw_pcie_exception_work);
	INIT_DELAYED_WORK(&priv->check_dumpdone_work, check_dumpdone_work);
	ret = pci_register_driver(&skw_pcie_driver);
	if(!ret)
		seekwave_boot_init();
	else
		PCIE_ERR("pci_register_driver fail %d\n", ret);
	mutex_init(&priv->except_mutex);
	mutex_init(&priv->dl_lock);
	PCIE_INFO("[-]\n");

	return ret;
}

static void __exit skw_pcie_exit(void)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();

	PCIE_INFO("[+]\n");
	seekwave_boot_exit();
	mutex_destroy(&priv->except_mutex);
	mutex_destroy(&priv->dl_lock);
	pci_unregister_driver(&skw_pcie_driver);
	cancel_delayed_work_sync(&priv->skw_except_work);
	cancel_delayed_work_sync(&priv->skw_pcie_recovery_work);
	cancel_delayed_work_sync(&priv->check_dumpdone_work);
	gpio_set_value(priv->chip_en,0);
	msleep(20);
	gpio_set_value(priv->chip_en, 1);
	msleep(100);
	skw_pcie_rescan_bus();
	kfree(priv);
	skw_pcie_debugfs_deinit();
	PCIE_INFO("[-]\n");
}

module_init(skw_pcie_init);
module_exit(skw_pcie_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("seekwave pcie/edma drv");
