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
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include "skw_edma_drv.h"
#include "skw_pcie_drv.h"
#include "skw_pcie_log.h"
#include "skw_edma_reg.h"


extern u32 last_sent_wifi_cmd[3];
extern u64 last_sent_time;

volatile u32 int_cnt = 0;
static u32 dl_req_cnt = 0, std_complete_cnt = 0;
static int cp_log_status = 0;
extern int send_modem_assert_command(void);
static int skw_check_cp_ready(void);
int skw_pcie_elbi_writeb(unsigned int address, unsigned char value);
int skw_pcie_elbi_writed(unsigned int address, u32 value);
int skw_pcie_cp_reset(void);
int skw_pcie_slp_feature_en(unsigned int address, unsigned int slp_en);
int skw_pcie_boot_cp(int boot_mode);

int skw_pcie_elbi_writeb(unsigned int address, unsigned char value)
{
	int ret;
	struct wcn_pcie_info *priv = get_pcie_device_info();
	ret=pci_write_config_byte(priv->dev,address, value);
	PCIE_DBG("line:%d (address,value)-(0x%x,0x%x)\n",__LINE__,address, value);
	return ret;
}

int skw_pcie_elbi_writed(unsigned int address, u32 value)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();
	pci_write_config_dword(priv->dev,address, value);
	PCIE_DBG("line:%d (address,value)-(0x%x,0x%x)\n",__LINE__,address, value);
	return value;
}

/*
 *svc_op:0-wifi start, 1-wifi stop, 2-bt start, 3-bt stop
 */
static int send_modem_service_command(u16 service, u16 command)
{
	int ret = 0;
	u16 cmd;
	struct wcn_pcie_info *priv = get_pcie_device_info();
	PCIE_DBG("line:%d (ser,cmd)-(%d,%d)\n",__LINE__, service, command);
	if(command)
		priv->service_state_map&= ~(1<<service);
	//command = 1;
	cmd = (service<<1)|command;
	cmd = 1 << cmd;

	if(priv->cp_state){
		PCIE_DBG("the cp_state = %d ---line:%d",priv->cp_state, __LINE__);
		return ret;
	}
	if(cmd>>8)
		ret = skw_pcie_elbi_writeb(SKW_AP2CP_IRQ_REG, cmd & 0xff);
	if(ret || !(cmd&0xff))
		return ret;
	PCIE_INFO("line:%d (ser,cmd)-(%d,0x%x)\n",__LINE__, service, cmd);
	return skw_pcie_elbi_writeb(SKW_AP2CP_IRQ_REG, cmd & 0xff);
}


int download_fw_complete_cb(void *context, u64 header, u64 tailed, int node_count)
{
	u32 val;
	u32 i;
	struct edma_chn_info *edma_chp = get_edma_channel_info(0);
	struct wcn_pcie_info *priv= get_pcie_device_info();

	std_complete_cnt += 1;
	PCIE_DBG("1. dl_req_cnt=%d, std_complete_cnt=%d\n", dl_req_cnt, std_complete_cnt);
	if (dl_req_cnt == std_complete_cnt) {
		std_complete_cnt = 0;
		dl_req_cnt = 0;
		int_cnt = 0;
		 PCIE_DBG("2. dl_req_cnt=%d, std_complete_cnt=%d\n", dl_req_cnt, std_complete_cnt);
		//set crc value
		for (i = 0;i < 7;i++) {
			skw_pcie_write32(FW_DATA_CRC_BASE + i * 4, 0);
		}
		if (priv->iram_crc_en) {
			skw_pcie_write32(FW_DATA_CRC_BASE +0x0, priv->iram_crc_offset);//iram crc offset
			skw_pcie_write32(FW_DATA_CRC_BASE +0x4, priv->iram_dl_size);//iram crc size
			skw_pcie_write32(FW_DATA_CRC_BASE +0x8, priv->iram_crc);//iram crc
			val = 3;//iram crc en
		}
		if (priv->dram_crc_en) {
			skw_pcie_write32(FW_DATA_CRC_BASE +0xc, priv->dram_crc_offset);//dram crc offset
			skw_pcie_write32(FW_DATA_CRC_BASE +0x10, priv->dram_dl_size);//dram crc size
			skw_pcie_write32(FW_DATA_CRC_BASE +0x14, priv->dram_crc);//dram crc
			val |= (3<<2);//dram crc en
		}
		if (priv->iram_crc_en || priv->dram_crc_en)
			skw_pcie_write32(FW_DATA_CRC_BASE +0x18, val);

		//set boot addr
		skw_pcie_write32(FW_BOOT_REG_BASE, 0x100000);

		//disable boot chn0 eb
		val = skw_pcie_read32(DMA_CFG(0));
		val &= ~0x1;
		skw_pcie_write32(DMA_CFG(0), val);

		//set dl done
		val = skw_pcie_read32(FW_DL_DONE_REG_BASE);
		val |= 0x3;
		skw_pcie_write32(FW_DL_DONE_REG_BASE, val);
		dma_unmap_single(&priv->dev->dev, edma_chp->map_pld_addr, priv->iram_dl_size, DMA_TO_DEVICE);
		dma_unmap_single(&priv->dev->dev, edma_chp->map_skb_addr, priv->dram_dl_size, DMA_TO_DEVICE);
	}

	complete(&priv->edma_blk_dl_done);
	return 0;
}

static int pcie_download_fw(u32 dst_addr, void *buf, u32 len)
{
	struct edma_chn_info *edma_chp = get_edma_channel_info(0);
	struct wcn_pcie_info *priv= get_pcie_device_info();
	DMA_DST_DSCR_PTR_HIGH_S dst_addr_h8 = {0};
	DMA_DST_DSCR_PTR_LOW_S dst_addr_l32 = {0};
	int i;


	mutex_lock(&priv->dl_lock);
	edma_chp->chn_cfg.direction = EDMA_TX;
	edma_chp->chn_cfg.req_mode = EDMA_STD_MODE;
	edma_chp->chn_cfg.complete_callback = download_fw_complete_cb;
	edma_chp->chn_cfg.trsc_len = 0xffff;

	for (i = 0; i < len / 0xffff; i++) {
		reinit_completion(&priv->edma_blk_dl_done);
		//src addr
		edma_chp->chn_cfg.header = edma_virtaddr_to_pcieaddr((char *)buf + i * 0xFFFF);
		edma_channel_init(0, &edma_chp->chn_cfg, NULL);

		//dst addr
		dst_addr_l32.dst_next_dscr_ptr_low = (dst_addr + i * 0xFFFF) & 0xffffffff;
		skw_pcie_write32(DMA_DST_DSCR_PTR_LOW(0), dst_addr_l32.u32);
		dst_addr_h8.dst_next_dscr_ptr_high = 0;
		skw_pcie_write32(DMA_DST_DSCR_PTR_HIGH(0), dst_addr_h8.u32);

		int_cnt++;
		//PCIE_DBG("2. %d: std_complete_cnt=%d, int_cnt=%d\n", __LINE__, std_complete_cnt, int_cnt);
		edma_adma_send(0, NULL, 0, 0);
		//dst req
		skw_pcie_setbit(DMA_DST_REQ(0), BIT(0));
		wait_for_completion(&priv->edma_blk_dl_done);
	}
	if (len % 0xffff) {
		reinit_completion(&priv->edma_blk_dl_done);
		edma_chp->chn_cfg.trsc_len = len % 0xffff;
		//src addr
		edma_chp->chn_cfg.header = edma_virtaddr_to_pcieaddr((char *)buf + i * 0xFFFF);
		//edma_chp->chn_cfg.header = edma_phyaddr_to_pcieaddr((dma_addr_t)buf + i * 0xFFFF);
		edma_channel_init(0, &edma_chp->chn_cfg, NULL);

		//dst addr
		dst_addr_l32.dst_next_dscr_ptr_low = (dst_addr + i * 0xFFFF) & 0xffffffff;
		skw_pcie_write32(DMA_DST_DSCR_PTR_LOW(0), dst_addr_l32.u32);
		dst_addr_h8.dst_next_dscr_ptr_high = 0;
		skw_pcie_write32(DMA_DST_DSCR_PTR_HIGH(0), dst_addr_h8.u32);
		int_cnt++;
		//PCIE_DBG("4. %d: std_complete_cnt=%d, int_cnt=%d\n", __LINE__, std_complete_cnt, int_cnt);
		edma_adma_send(0, NULL, 0, 0);
		//dst req
		skw_pcie_setbit(DMA_DST_REQ(0), BIT(0));
		wait_for_completion(&priv->edma_blk_dl_done);
	}
	mutex_unlock(&priv->dl_lock);
	return 0;
}

int skw_WIFI_service_start(void)
{
	int count=90;
	int ret =0;
	struct wcn_pcie_info *priv= get_pcie_device_info();
	PCIE_INFO("Enter STARTWIFI %d\n",priv->cp_state);
	if (priv->cp_state) {
		while(priv->cp_state&&count--)
			msleep(10);
	}
	if (priv->service_state_map & (1<<WIFI_SERVICE))
		return 0;

#ifdef CONFIG_SEEKWAVE_PLD_RELEASE
	//release version close the cP log
	skw_pcie_cp_log(1);
#endif

	skw_reinit_completion(priv->download_done);
	ret= send_modem_service_command(WIFI_SERVICE, SERVICE_START);
	if(!ret)
		ret = skw_check_cp_ready();

	return ret;

}

int skw_WIFI_service_stop(void)
{
	int count=50;
	struct wcn_pcie_info *priv= get_pcie_device_info();
	PCIE_DBG("Enter,STOPWIFI  cp_state:%d",priv->cp_state);
	if (priv->cp_state) {
		priv->service_state_map &= ~(1<<WIFI_SERVICE);
		while(priv->cp_state&& count--)
			msleep(10);
		return 0;
	}
	if (priv->service_state_map & (1<<WIFI_SERVICE))
		return send_modem_service_command(WIFI_SERVICE, SERVICE_STOP);
	return 0;

}

int skw_BT_service_start(void)
{
	int count=90;
	int ret;
	struct wcn_pcie_info *priv= get_pcie_device_info();
	PCIE_INFO("Enter STARTBT %d\n",priv->cp_state);
	if (priv->cp_state) {
		while(priv->cp_state&&count--)
			msleep(10);
	}
	if (priv->service_state_map & (1<<BT_SERVICE))
		return 0;

#ifdef CONFIG_SEEKWAVE_PLD_RELEASE
	//release version close the cP log
	skw_pcie_cp_log(1);
#endif

	skw_reinit_completion(priv->download_done);
	ret = send_modem_service_command(BT_SERVICE, SERVICE_START);
	if(!ret)
		ret = skw_check_cp_ready();

	return ret;
}

int skw_BT_service_stop(void)
{
	int count=50;
	struct wcn_pcie_info *priv= get_pcie_device_info();
	PCIE_INFO("Enter,STOPBT  cp_state:%d",priv->cp_state);
	if (priv->cp_state) {
		priv->service_state_map &= ~(1<<BT_SERVICE);
		while(priv->cp_state&& count--)
			msleep(10);
		return 0;
	}
	if (priv->service_state_map & (1<<BT_SERVICE)) {
		skw_reinit_completion(priv->download_done);
		send_modem_service_command(BT_SERVICE, SERVICE_STOP);
		wait_for_completion_interruptible_timeout(&priv->download_done, msecs_to_jiffies(100));
	}
	return 0;
}

int skw_pcie_cp_log(int disable)
{
	int ret =0;
	struct wcn_pcie_info *priv = get_pcie_device_info();
	PCIE_DBG("-----CP LOG--SWITCH-----!!!\n");
	cp_log_status = disable;
	if(priv->cp_state)
		return ret;
	ret =skw_pcie_elbi_writeb(SKWPCIE_AP2CP_SIG1, disable);
	if(ret <0){
		PCIE_ERR("switch the log signal send fail ret=%d\n", ret);
		return ret;
	}
	skw_pcie_elbi_writeb(SKW_AP2CP_IRQ_REG, 0x20);
	return ret;
}
int skw_pcie_cp_log_status(void)
{
	return cp_log_status;
}

int skw_pcie_recovery_disable(int disable)
{
	 struct wcn_pcie_info *priv= get_pcie_device_info();
	 priv->recovery_dis_state = disable;
	 PCIE_DBG("The recovery dis state = %d \n", disable);
	 return 0;
}

int skw_pcie_recovery_debug_status(void)
{
	 struct wcn_pcie_info *priv= get_pcie_device_info();
	 return priv->recovery_dis_state;
}

int skw_recovery_mode(void)
{
	 int ret;
	 struct wcn_pcie_info *priv= get_pcie_device_info();

	 ret=skw_pcie_cp_reset();
	 if(ret!=0){
		 PCIE_ERR("CP RESET fail \n");
		 return -1;
	 }
	 //skw_pcie_bind_bt_driver(priv->dev);
	 ret = skw_pcie_boot_cp(RECOVERY_BOOT);
	 if(ret!=0){
		 PCIE_ERR("CP RESET fail \n");
		 return -1;
	 }
	 skw_pcie_bind_wifi_driver(priv->dev);
	 PCIE_INFO("Recovery ok\n");
	 return 0;
}

int skw_pcie_cp_reset(void)
{
	int ret=0;

	skw_pcie_rescan_bus();
	if (ret < 0) {
		PCIE_ERR("enable func1 err!!! ret is %d\n", ret);
		return -1;
	}
	PCIE_DBG("CP RESET OK!\n");
	return 0;
}

int skw_pcie_slp_feature_en(unsigned int address, unsigned int slp_en)
{
	int ret =0;
	//ret = skw_pcie_elbi_writeb()
	if(ret !=0)
	{
		PCIE_ERR("support en write fail ret=%d\n",ret);
		return -1;
	}
	PCIE_INFO("nslp support enable:%d\n", slp_en);
	return 0;
}
/****************************************************************
*Description:
*Func:used the ap boot cp interface;
*Output:the dloader the bin to cp
*Return0:pass; other : fail
*Author:JUNWEI.JIANG
*Date:2023-06-07
****************************************************************/
int skw_pcie_boot_cp(int boot_mode)
{
	int ret =0;
	struct wcn_pcie_info *priv= get_pcie_device_info();
	struct edma_chn_info *edma_chp = get_edma_channel_info(0);
	skw_pcie_slp_feature_en(priv->boot_data->slp_disable_addr,
			priv->boot_data->slp_disable);

	dl_req_cnt = (priv->boot_data->iram_dl_size / 0xffff) + ((!!(priv->boot_data->iram_dl_size % 0xffff)) ? 1 : 0);
	dl_req_cnt += (priv->boot_data->dram_dl_size / 0xffff) + ((!!(priv->boot_data->dram_dl_size % 0xffff)) ? 1 : 0);
	//PCIE_DBG("dl_req_cnt=%d \n", dl_req_cnt);
	//print_hex_dump(KERN_ERR, "PACKET ERR:", 0, 16, 1, boot_data->iram_img_data, 0x100, 1);

	PCIE_INFO("1. PCIe BOOT.DEUBG..LINE %d \n", __LINE__);
	edma_chp->map_skb_addr = dma_map_single(&priv->dev->dev, (void *)priv->boot_data->dram_img_data,
			priv->boot_data->dram_dl_size, DMA_TO_DEVICE);
	//PCIE_DBG("2. PCIe FIRST BOOT... \n");

	edma_chp->map_pld_addr = dma_map_single(&priv->dev->dev, (void *)priv->boot_data->iram_img_data,
			priv->boot_data->iram_dl_size, DMA_TO_DEVICE);
	//PCIE_DBG("3. PCIe FIRST BOOT... \n");

	skw_pcie_host_irq_init(priv->boot_data->gpio_in);
	skw_reinit_completion(priv->download_done);
	ret |= pcie_download_fw(priv->boot_data->iram_dl_addr, priv->boot_data->iram_img_data, priv->boot_data->iram_dl_size);
	ret |= pcie_download_fw(priv->boot_data->dram_dl_addr, priv->boot_data->dram_img_data, priv->boot_data->dram_dl_size);
	ret |=skw_check_cp_ready();
	PCIE_INFO("1. PCIe BOOT.DEUBG..LINE %d \n", __LINE__);
	if(ret !=0)
		goto FAIL;
	return 0;
FAIL:
	PCIE_ERR("line:%d  fail ret=%d\n",__LINE__, ret);
	return ret;
}

int skw_pcie_cp_service_ops(int service_ops)
{
	int ret =0;
	switch(service_ops)
	{
		case SKW_WIFI_START:
			ret=skw_WIFI_service_start();
			skw_pcie_dbg("-----WIFI SERIVCE START\n");
		break;
		case SKW_WIFI_STOP:
			ret =skw_WIFI_service_stop();
			skw_pcie_dbg("----WIFI SERVICE---STOP\n");
		break;
		case SKW_BT_START:
		{
			ret=skw_BT_service_start();
			skw_pcie_dbg("-----BT SERIVCE --START\n");
		}
		break;
		case SKW_BT_STOP:
			ret =skw_BT_service_stop();
			skw_pcie_dbg("-----BT SERVICE --STOP\n");
		break;
		default:
			skw_pcie_err("service not support %d !\n", service_ops);
		break;
	}
	return ret;
}

int skw_boot_loader(struct seekwave_device *boot_data)
{
	int ret =0;
	struct wcn_pcie_info *priv= get_pcie_device_info();
	struct edma_chn_info *edma_chp = get_edma_channel_info(0);
	priv->boot_data= boot_data;
	if(boot_data->dl_module == RECOVERY_BOOT&&boot_data->first_dl_flag){
		schedule_delayed_work(&priv->skw_pcie_recovery_work , msecs_to_jiffies(2000));
		//skw_recovery_mode();
	}
	PCIE_DBG("iram_size=0x%x, iram_addr:0x%x, dram_size=0x%x, dram_addr=0x%x\n",
		boot_data->iram_dl_size,
		boot_data->iram_dl_addr,
		boot_data->dram_dl_size,
		boot_data->dram_dl_addr);

	PCIE_DBG("iram_img_data=0x%p, dram_img_data:0x%p\n",
		boot_data->iram_img_data,
		boot_data->dram_img_data);

	priv->chip_en = boot_data->chip_en;
	if (!boot_data->first_dl_flag && boot_data->iram_dl_size && boot_data->dram_dl_size) {

		if (boot_data->chip_en > 0){
			gpio_set_value(boot_data->chip_en, 1);
			priv->chip_en = boot_data->chip_en;
		}
		PCIE_INFO("PCIe FIRST BOOT... \n");
		priv->iram_dl_size = boot_data->iram_dl_size;
		priv->dram_dl_size = boot_data->dram_dl_size;
		priv->iram_crc_offset = boot_data->iram_crc_offset;
		priv->dram_crc_offset = boot_data->dram_crc_offset;
		priv->iram_crc = boot_data->iram_crc_val;
		priv->dram_crc = boot_data->dram_crc_val;
		priv->iram_crc_en = 0;//boot_data->iram_crc_en;
		priv->dram_crc_en = 0;//boot_data->dram_crc_en;
		skw_pcie_host_irq_init(boot_data->gpio_in);
		dl_req_cnt = (boot_data->iram_dl_size / 0xffff) + ((!!(boot_data->iram_dl_size % 0xffff)) ? 1 : 0);
		dl_req_cnt += (boot_data->dram_dl_size / 0xffff) + ((!!(boot_data->dram_dl_size % 0xffff)) ? 1 : 0);
		//PCIE_DBG("dl_req_cnt=%d \n", dl_req_cnt);
		//print_hex_dump(KERN_ERR, "PACKET ERR:", 0, 16, 1, boot_data->iram_img_data, 0x100, 1);

		//PCIE_DBG("1. PCIe FIRST BOOT... \n");
		edma_chp->map_skb_addr = dma_map_single(&priv->dev->dev, (void *)boot_data->dram_img_data,
				boot_data->dram_dl_size, DMA_TO_DEVICE);
		//PCIE_DBG("2. PCIe FIRST BOOT... \n");

		edma_chp->map_pld_addr = dma_map_single(&priv->dev->dev, (void *)boot_data->iram_img_data,
				boot_data->iram_dl_size, DMA_TO_DEVICE);
		//PCIE_DBG("3. PCIe FIRST BOOT... \n");
		ret |= pcie_download_fw(boot_data->iram_dl_addr, boot_data->iram_img_data, boot_data->iram_dl_size);
		ret |= pcie_download_fw(boot_data->dram_dl_addr, boot_data->dram_img_data, boot_data->dram_dl_size);
		ret |=skw_check_cp_ready();
	} else {
		PCIE_INFO("The FW BOOT From CP!!!!\n");
	}

	if (!boot_data->first_dl_flag && !ret) {
		skw_pcie_bind_wifi_driver(priv->dev);
		skw_pcie_bind_bt_driver(priv->dev);
	}
	ret = skw_pcie_cp_service_ops(boot_data->service_ops);
	if(ret < 0)
		return -1;
	else
		return 0;
}

void reboot_to_change_bt_uart1(char *mode)
{
	struct wcn_pcie_info *priv= get_pcie_device_info();
	struct seekwave_device *boot_data = priv->boot_data;
	u32 *data = (u32 *) &boot_data->iram_img_data[boot_data->head_addr-4];

	if(data[0] & 0x80000000)
		data[0] |=  0x0000008;
	else
		data[0] = 0x80000008;
	//skw_recovery_mode();
	send_modem_assert_command();

}

int skw_reset_bus_dev(void)
{
	return 0;
}
void get_bt_antenna_mode(char *mode)
{
	struct wcn_pcie_info *priv= get_pcie_device_info();
	struct seekwave_device *boot_data = priv->boot_data;
	u32 bt_antenna = boot_data->bt_antenna;

	if(bt_antenna==0)
		return;
	bt_antenna--;
	if(!mode)
		return;
	if (bt_antenna)
		sprintf(mode,"bt_antenna : alone\n");
	else
		sprintf(mode,"bt_antenna : share\n");
}

void reboot_to_change_bt_antenna_mode(char *mode)
{
	struct wcn_pcie_info *priv= get_pcie_device_info();
	struct seekwave_device *boot_data = priv->boot_data;
	u32 *data = (u32 *) &boot_data->iram_img_data[boot_data->head_addr-4];
	u32 bt_antenna;

	if(boot_data->bt_antenna == 0)
		return;
	bt_antenna = boot_data->bt_antenna - 1;
		bt_antenna = 1 - bt_antenna;
	data[0] = (bt_antenna) | 0x80000000;
	if(!mode)
		return;
	if (bt_antenna==1) {
		boot_data->bt_antenna = 2;
		sprintf(mode,"bt_antenna : alone\n");
	} else {
		boot_data->bt_antenna = 1;
		sprintf(mode,"bt_antenna : share\n");
	}
	send_modem_assert_command();
}
void *skw_get_bus_dev(void)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();
	return &priv->dev->dev;
}

static int skw_check_cp_ready(void)
{
	struct wcn_pcie_info *priv = get_pcie_device_info();

	//skw_reinit_completion(priv->download_done);
	PCIE_INFO("check CP-ready Enter!!\n");
	if (wait_for_completion_timeout(&priv->download_done,
		msecs_to_jiffies(3000)) == 0) {
		 PCIE_ERR("check CP-ready time out\n");
		 return -ETIME;
	}
	return 0;
}

