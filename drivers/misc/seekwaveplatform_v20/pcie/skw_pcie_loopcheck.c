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
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/version.h>
#include <linux/notifier.h>
#include <linux/semaphore.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/semaphore.h>
#include "skw_pcie_drv.h"
#include "skw_edma_drv.h"

#define MODEM_OFF  0
#define MODEM_ON	1
#define MODEM_HALT 2

extern struct edma_port edma_ports[MAX_PORT_NUM];
int modem_status;
static struct task_struct *loop_thread;
static int loop_state;
static struct semaphore loop_sem;
static char firmware_version[128];
static int loop_portno;
int send_modem_assert_command(void);
extern u32 last_sent_wifi_cmd[3];
int skw_pcie_elbi_writeb(unsigned int address, unsigned char value);
int skw_pcie_elbi_writed(unsigned int address, u32 value);
static BLOCKING_NOTIFIER_HEAD(modem_notifier_list);
static u8 port_assert_idx[5] = {
	EDMA_AT_PORT,
	EDMA_LOG_PORT,
	EDMA_BTCMD_PORT,
	EDMA_BTAUDIO_PORT,
	EDMA_BTACL_PORT
};

void skw_pcie_setup_service_devices(void)
{
}
void modem_register_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_register(&modem_notifier_list, nb);
}
void modem_unregister_notify(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&modem_notifier_list, nb);
}
static void modem_notify_event(int event)
{
	blocking_notifier_call_chain(&modem_notifier_list, event, NULL);
}

void skw_pcie_exception_work(struct work_struct *work)
{
	int i=0;
	struct wcn_pcie_info *priv = get_pcie_device_info();
	PCIE_INFO(" ENTER...\n");
	mutex_lock(&priv->except_mutex);
	if(priv->cp_state !=0)
	{
		PCIE_INFO("the assert coming!!\n");
		mutex_unlock(&priv->except_mutex);
		return;
	}
	priv->cp_state = DEVICE_BLOCKED_EVENT;
	mutex_unlock(&priv->except_mutex);
	modem_notify_event(DEVICE_BLOCKED_EVENT);
	for (i=0; i<5; i++)
	{
		if(!edma_ports[port_assert_idx[i]].state || edma_ports[port_assert_idx[i]].state==PORT_STATE_CLSE)
			continue;

		edma_ports[i].state = PORT_STATE_ASST;
	}
	recovery_close_all_ports();
	gpio_set_value(priv->chip_en, 0);
	PCIE_INFO("recv:---- ----chipen=%d\n", gpio_get_value(priv->chip_en));
	msleep(1000);
	gpio_set_value(priv->chip_en, 1);
	PCIE_INFO("recv:---- ----chipen=%d\n", gpio_get_value(priv->chip_en));
	priv->service_state_map=0;
	skw_recovery_mode();
}

/*skw_ap2cp_irq_reg bit4 modem assert*/
int send_modem_assert_command(void)
{
	int ret =0;
	u32 *cmd = last_sent_wifi_cmd;
	struct wcn_pcie_info *priv = get_pcie_device_info();

	PCIE_DBG(" ENTER !!!\n");
	if(priv->cp_state)
		return ret;

	//priv->cp_state=1;/*cp except set value*/
	ret =skw_pcie_elbi_writeb(SKW_AP2CP_IRQ_REG, 0x10);
	PCIE_ERR("%s ret=%d cmd: 0x%x 0x%x 0x%x\n", __func__,
			 ret, cmd[0], cmd[1], cmd[2]);
#ifdef CONFIG_SEEKWAVE_PLD_RELEASE
	schedule_delayed_work(&priv->skw_except_work , msecs_to_jiffies(2000));
#else
	if(!priv->recovery_dis_state)
		schedule_delayed_work(&priv->skw_except_work , msecs_to_jiffies(6000));
#endif
	return ret;
}

void check_dumpdone_work(struct work_struct *work)
{
	struct edma_port *port = get_edma_port_info(EDMA_LOOPCHECK_PORT);

	if (!completion_done(&port->rx_done)) {
		complete(&port->rx_done);
		PCIE_INFO("force complete the loopcheck rx_done\n");
	}
}

int skw_pcie_loopcheck_entry(void *para)
{
	struct wcn_pcie_info *skw_pcie;
	int portno = *(int *)para;
	char *buffer;
	int read, size;
	int count= 0, timeout=100;
	int i;

	PCIE_DBG("\n");
	size = 512;
	buffer = kzalloc(size, GFP_KERNEL);
	skw_pcie = get_pcie_device_info();
	while(loop_state && buffer){
		read = 0;
		memset(buffer,0,size);
		do {
			if(loop_state==0)
				break;
			read = recv_data(portno, buffer, 256);
		} while(!read);

		if(read < 0 || !loop_state) {
			PCIE_DBG("bulkin read_len=%d\n",read);
			break;
		}

		PCIE_INFO("recv(%d): %s\n", read, buffer);
#if 0
		if(strncmp(buffer, "BSPREADY", read))
			PCIE_INFO("recv(%d): %s\n", read, buffer);
#endif
		memcpy(buffer+256, "LOOPCHECK", 9);
		if (read==8 && !strncmp(buffer, "BSPREADY", read)) {
			PCIE_INFO("BSP READY!!!\n");
			;//send_data(portno, buffer+256, 9);
		} else if (read==9 && !strncmp(buffer, "WIFIREADY", read)) {
			skw_pcie->service_state_map |=1;
			complete(&skw_pcie->download_done);
			timeout=500;
			PCIE_DBG("SEND THE LOOPCHECK CMD !!!\n");
			send_data(portno, buffer+256, 9);
		} else if (read==6 && !strncmp(buffer, "BTEXIT", read)) {
			complete(&skw_pcie->download_done);
		} else if (read==7 && !strncmp(buffer, "BTREADY", read)) {
			skw_pcie->service_state_map |=2;
			complete(&skw_pcie->download_done);
			send_data(portno, buffer+256, 9);
		} else if (!strncmp(buffer, "BSPASSERT", 9)) {
			if(skw_pcie->cp_state==1)
				cancel_delayed_work_sync(&skw_pcie->skw_except_work);

			mutex_lock(&skw_pcie->except_mutex);
			if(skw_pcie->cp_state == DEVICE_BLOCKED_EVENT){
				mutex_unlock(&skw_pcie->except_mutex);
				break;
			}
			skw_pcie->cp_state = 1;//TODO
			mutex_unlock(&skw_pcie->except_mutex);

			memset(buffer, 0, read);
			modem_status = MODEM_HALT;
			//show_assert_context();
			modem_notify_event(DEVICE_ASSERT_EVENT);
#ifndef CONFIG_SEEKWAVE_PLD_RELEASE
			if(edma_ports[EDMA_LOG_PORT].state == PORT_STATE_OPEN) {
				schedule_delayed_work(&skw_pcie->check_dumpdone_work , msecs_to_jiffies(5000));
				read = recv_data(portno, buffer, 256);
				cancel_delayed_work_sync(&skw_pcie->check_dumpdone_work);
				PCIE_INFO(" bspassert after recv(%d): %s\n", read, buffer);
				msleep(5000);//wait for CP to dump assert log
			}
#endif
			modem_notify_event(DEVICE_DUMPDONE_EVENT);

			for (i=0; i<5; i++) {
				if((edma_ports[port_assert_idx[i]].state == PORT_STATE_IDLE) ||
							(edma_ports[port_assert_idx[i]].state==PORT_STATE_CLSE))
					continue;
				edma_ports[port_assert_idx[i]].state = PORT_STATE_ASST;
			}

			if(skw_pcie->recovery_dis_state)
				break;

			recovery_close_all_ports();
			PCIE_INFO("recv(%d):---- %s----chipen=%d\n", read, buffer, skw_pcie->chip_en);
			gpio_set_value(skw_pcie->chip_en, 0);
			PCIE_INFO("recv:---- ----chipen=%d\n", gpio_get_value(skw_pcie->chip_en));
			msleep(1000);
			gpio_set_value(skw_pcie->chip_en, 1);
			PCIE_INFO("recv:---- ----chipen=%d\n", gpio_get_value(skw_pcie->chip_en));
			schedule_delayed_work(&skw_pcie->skw_pcie_recovery_work , msecs_to_jiffies(2000));
			//skw_recovery_mode();
			//PCIE rescan bus
			break;
		} else if (!strncmp("trunk_W", buffer, 7)) {
			//if(!skw_pcie->cp_state)
			complete(&skw_pcie->download_done);

			//assert_info_print = 0;
			skw_pcie->cp_state = 0;
			modem_status = MODEM_ON;
			memset(firmware_version, 0 , sizeof(firmware_version));
			strncpy(firmware_version, buffer, read);
			PCIE_DBG("---debug---,@@Line:%d, Func:%s@@\n", __LINE__, __func__);
			modem_notify_event(DEVICE_BSPREADY_EVENT);
			PCIE_DBG("---debug---,@@Line:%d, Func:%s@@\n", __LINE__, __func__);

			count = 0;
			skw_pcie_setup_service_devices();
		}
		msleep(timeout);
	}
	PCIE_INFO("loopcheck thread is exit\n");

	kfree(buffer);
	up(&loop_sem);
	return 0;
}

int skw_pcie_create_loopcheck_thread(int portno)
{
	int ret;

	loop_thread = NULL;
	modem_status = MODEM_OFF;
	loop_state = 0;
	ret = open_edma_port(portno, NULL, NULL);
	if (ret==0) {
		loop_portno = portno;
		loop_thread = kthread_create(skw_pcie_loopcheck_entry, &loop_portno, "LOOP");
	}
	if(loop_thread) {
		loop_state = 1;
		sema_init(&loop_sem, 0);
		wake_up_process(loop_thread);
	}
	return 0;
}

/************************************************************************
 *Decription:
 *Author:jiayong.yang
 *Date:2021-05-27
 *Modfiy:
 *
 ********************************************************************* */
int skw_pcie_remove_loopcheck_thread(int portno)
{
	int ret;

	if (loop_state && loop_thread) {
		loop_state = 0;
		//close_edma_port(portno);
		ret = down_interruptible(&loop_sem);
		if (ret==0)
			loop_thread = NULL;
	}
	return 0;
}
