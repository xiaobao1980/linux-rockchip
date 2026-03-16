/**************************************************************************
 * Copyright(c) 2020-2030  Seekwave Corporation.
 * SEEKWAVE TECH LTD..CO
 *
 *Seekwave Platform the sdio log debug fs
 *FILENAME:skw_sdio_log.c
 *DATE:2022-04-11
 *MODIFY:
 *Author:Jones.Jiang
 **************************************************************************/
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "skw_sdio.h"
#include "skw_sdio_log.h"
#include "skw_sdio_debugfs.h"

extern char firmware_version[];
static unsigned long skw_sdio_dbg_level;
static unsigned long skw_sdio_channel_record = 0;

unsigned long skw_sdio_log_level(void)
{
	return skw_sdio_dbg_level;
}

static void skw_sdio_set_log_level(int level)
{
	unsigned long dbg_level;

	dbg_level = skw_sdio_log_level() & 0xffff0000;
	dbg_level |= ((level << 1) - 1);

	xchg(&skw_sdio_dbg_level, dbg_level);
}

static void skw_sdio_enable_func_log(int func, bool enable)
{
	unsigned long dbg_level = skw_sdio_log_level();

	if (enable)
		dbg_level |= func;
	else
		dbg_level &= (~func);

	xchg(&skw_sdio_dbg_level, dbg_level);
}

static int skw_sdio_log_show(struct seq_file *seq, void *data)
{
#define SKW_SDIO_LOG_STATUS(s) (level & (s) ? "enable" : "disable")

	int i;
	u32 level = skw_sdio_log_level();
	u8 *log_name[] = {"NONE", "ERROR", "WARNNING", "INFO", "DEBUG"};

	for (i = 0; i < 5; i++) {
		if (!(level & BIT(i)))
			break;
	}

	seq_printf(seq, "\nlog   level: %s\n", log_name[i]);

	seq_puts(seq, "\n");
	seq_printf(seq, "port0 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT0));
	seq_printf(seq, "port1 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT1));
	seq_printf(seq, "port2 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT2));
	seq_printf(seq, "port3 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT3));
	seq_printf(seq, "port4 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT4));
	seq_printf(seq, "port5 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT5));
	seq_printf(seq, "port6 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT6));
	seq_printf(seq, "port7 log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_PORT7));
	seq_printf(seq, "savelog  : %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_SAVELOG));
	seq_printf(seq, "dump  log: %s\n", SKW_SDIO_LOG_STATUS(SKW_SDIO_DUMP));

	return 0;
}

static int skw_sdio_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, &skw_sdio_log_show, inode->i_private);
}

static int skw_sdio_log_control(const char *cmd, bool enable)
{
	if (!strcmp("dump", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_DUMP, enable);
	else if (!strcmp("port0", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT0, enable);
	else if (!strcmp("port1", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT1, enable);
	else if (!strcmp("port2", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT2, enable);
	else if (!strcmp("port3", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT3, enable);
	else if (!strcmp("port4", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT4, enable);
	else if (!strcmp("port5", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT5, enable);
    else if (!strcmp("port6", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT6, enable);
	else if (!strcmp("port7", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_PORT7, enable);
    else if (!strcmp("savelog", cmd))
		skw_sdio_enable_func_log(SKW_SDIO_SAVELOG, enable);
	else if (!strcmp("debug", cmd))
		skw_sdio_set_log_level(SKW_SDIO_DEBUG);
	else if (!strcmp("info", cmd))
		skw_sdio_set_log_level(SKW_SDIO_INFO);
	else if (!strcmp("warn", cmd))
		skw_sdio_set_log_level(SKW_SDIO_WARNING);
	else if (!strcmp("error", cmd))
		skw_sdio_set_log_level(SKW_SDIO_ERROR);
	else
		return -EINVAL;

	return 0;
}

static ssize_t skw_sdio_log_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	int i, idx;
	char cmd[32];
	bool enable = false;

	for (idx = 0, i = 0; i < len; i++) {
		char c;

		if (get_user(c, buffer))
			return -EFAULT;

		switch (c) {
		case ' ':
			break;

		case ':':
			cmd[idx] = 0;
			if (!strcmp("enable", cmd))
				enable = true;
			else
				enable = false;

			idx = 0;
			break;

		case '|':
		case '\0':
		case '\n':
			cmd[idx] = 0;
			skw_sdio_log_control(cmd, enable);
			idx = 0;
			break;

		default:
			cmd[idx++] = c;
			idx %= 32;

			break;
		}

		buffer++;
	}

	return len;
}

static const struct file_operations skw_sdio_log_fops = {
	.owner = THIS_MODULE,
	.open = skw_sdio_log_open,
	.read = seq_read,
	.release = single_release,
	.write = skw_sdio_log_write,
};

static int skw_version_show(struct seq_file *seq, void *data)
{
	seq_printf(seq, "firmware info: %s\n", firmware_version );
	return 0;
}
static int skw_version_open(struct inode *inode, struct file *file)
{
	return single_open(file, &skw_version_show, inode->i_private);
}


static const struct file_operations skw_version_fops = {
	.owner = THIS_MODULE,
	.open = skw_version_open,
	.read = seq_read,
	.release = single_release,
};

static int skw_port_statistic_show(struct seq_file *seq, void *data)
{
        char *statistic = kzalloc(2048, GFP_KERNEL);

        skw_get_port_statistic(statistic, 2048);
	seq_printf(seq, "Statistic:\n %s\n", statistic);
	skw_get_assert_print_info(statistic, 2048);
	seq_printf(seq, "sdio last irqs information:\n%s", statistic);
	skw_get_sdio_debug_info(statistic, 2048);
	seq_printf(seq, "\nsdio debug information:\n%s", statistic);
	kfree(statistic);
        return 0;
}
static int skw_port_statistic_open(struct inode *inode, struct file *file)
{
        return single_open(file, &skw_port_statistic_show, inode->i_private);
}


static const struct file_operations skw_port_statistic_fops = {
        .owner = THIS_MODULE,
        .open = skw_port_statistic_open,
        .read = seq_read,
        .release = single_release,
};

static int skw_cp_log_show(struct seq_file *seq, void *data)
{
	if (!skw_sdio_cp_log_status())
		seq_printf(seq, "Enabled ");
	else
		seq_printf(seq, "Disabled ");
        return 0;
}
static int skw_cp_log_open(struct inode *inode, struct file *file)
{
        return single_open(file, &skw_cp_log_show, inode->i_private);
}


static ssize_t skw_cp_log_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	char cmd[16]={0};

	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buffer, len))
		return -EFAULT;
	if (!strncmp("enable", cmd, 6))
		skw_sdio_cp_log(0);
	else if (!strncmp("disable", cmd, 7))
		skw_sdio_cp_log(1);
	return len;
}

static const struct file_operations skw_cp_log_fops = {
	.owner = THIS_MODULE,
	.open = skw_cp_log_open,
	.read = seq_read,
	.release = single_release,
	.write = skw_cp_log_write,
};
static int skw_bluetooth_UART1_open(struct inode *inode, struct file *file)
{
        return single_open(file, NULL, inode->i_private);
}


static ssize_t skw_bluetooth_UART1_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	char cmd[32]={0};

	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buffer, len))
		return -EFAULT;
	if (!strncmp("enable", cmd, 6)) {
		memset(cmd, 0, sizeof(cmd));
		reboot_to_change_bt_uart1(cmd);
		printk("%s UART-HCI\n", cmd);
	}
	return len;
}

static const struct file_operations skw_bluetooth_UART1_fops = {
	.owner = THIS_MODULE,
	.open = skw_bluetooth_UART1_open,
	.release = single_release,
	.write = skw_bluetooth_UART1_write,
};

static int skw_bluetooth_antenna_show(struct seq_file *seq, void *data)
{
	char result[32];

	memset(result, 0, sizeof(result));
	get_bt_antenna_mode(result);
	if(strlen(result))
		seq_printf(seq, result);
        return 0;
}
static int skw_bluetooth_antenna_open(struct inode *inode, struct file *file)
{
        return single_open(file, &skw_bluetooth_antenna_show, inode->i_private);
}


static ssize_t skw_bluetooth_antenna_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	char cmd[32]={0};

	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buffer, len))
		return -EFAULT;
	if (!strncmp("switch", cmd, 6)) {
		memset(cmd, 0, sizeof(cmd));
		reboot_to_change_bt_antenna_mode(cmd);
		printk("%s\n", cmd);
	}
	return len;
}

static const struct file_operations skw_bluetooth_antenna_fops = {
	.owner = THIS_MODULE,
	.open = skw_bluetooth_antenna_open,
	.read = seq_read,
	.release = single_release,
	.write = skw_bluetooth_antenna_write,
};


static int skw_recovery_debug_show(struct seq_file *seq, void *data)
{
	if (skw_sdio_recovery_debug_status())
		seq_printf(seq, "Disabled");
	else
		seq_printf(seq, "Enabled");
        return 0;
}
static int skw_recovery_debug_open(struct inode *inode, struct file *file)
{
        return single_open(file, &skw_recovery_debug_show, inode->i_private);
}


static ssize_t skw_recovery_debug_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	char cmd[16]={0};

	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buffer, len))
		return -EFAULT;
	if (!strncmp("disable", cmd, 7))
		skw_sdio_recovery_debug(1);
	else if (!strncmp("enable", cmd, 6))
		skw_sdio_recovery_debug(0);

	return len;
}

static const struct file_operations skw_recovery_debug_fops = {
	.owner = THIS_MODULE,
	.open = skw_recovery_debug_open,
	.read = seq_read,
	.release = single_release,
	.write = skw_recovery_debug_write,
};

static int skw_dump_mem_show(struct seq_file *seq, void *data)
{
        return 0;
}
static int skw_dump_mem_open(struct inode *inode, struct file *file)
{
        return single_open(file, &skw_dump_mem_show, inode->i_private);
}

static ssize_t skw_dump_mem_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	char cmd[16]={0};

	if (len >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, buffer, len))
		return -EFAULT;
	if (!strncmp("dump", cmd, 4))
		skw_sdio_dumpmem(1);
	else if (!strncmp("stop", cmd, 4))
		skw_sdio_dumpmem(0);

	return len;
}

static const struct file_operations skw_dump_mem_fops = {
	.owner = THIS_MODULE,
	.open = skw_dump_mem_open,
	.read = seq_read,
	.release = single_release,
	.write = skw_dump_mem_write,
};


static int skw_sdio_wifi_show(struct seq_file *seq, void *data)
{
	if (skw_sdio_wifi_status())
		seq_printf(seq, "PowerOn");
	else
		seq_printf(seq, "PowerOff");
	return 0;
}
static int skw_sdio_wifi_open(struct inode *inode, struct file *file)
{
	return single_open(file, &skw_sdio_wifi_show, inode->i_private);
}


static ssize_t skw_sdio_wifi_poweron(struct file *fp, const char __user *buffer,
                                  size_t len, loff_t *offset)
{
          char cmd[16]={0};

          if (len >= sizeof(cmd))
                  return -EINVAL;
          if (copy_from_user(cmd, buffer, len))
                  return -EFAULT;
          if (!strncmp("on", cmd, 2))
                  skw_sdio_wifi_power_on(1);
          else if (!strncmp("off", cmd, 3))
                skw_sdio_wifi_power_on(0);

          return len;
}

static const struct file_operations skw_sdio_wifi_fops = {
          .owner = THIS_MODULE,
          .open = skw_sdio_wifi_open,
          .read = seq_read,
          .release = single_release,
          .write = skw_sdio_wifi_poweron,
};


unsigned long skw_sdio_channel_record_get(void)
{
	return skw_sdio_channel_record;
}

static void skw_sdio_channel_record_enable(u32 chn, bool enable)
{
	unsigned long record = skw_sdio_channel_record_get();

	record = skw_sdio_channel_record_get();
	if (enable)
		record |= BIT(chn);
	else
		record &= ~BIT(chn);

	skw_sdio_info("port%d %s\n", chn, record & BIT(chn) ? "enable" : "disable");
	xchg(&skw_sdio_channel_record, record);
}

void skw_sdio_channel_record_enable_all(void)
{
	unsigned long record = skw_sdio_channel_record_get();
	int i = 0;

	record = skw_sdio_channel_record_get();
	for (i = 0; i < SDIO2_MAX_CH_NUM; i++)
		record |= BIT(i);

	skw_sdio_info("all port enable 0x%lu\n", record);
	xchg(&skw_sdio_channel_record, record);
}

void skw_sdio_channel_record_disable_all(void)
{
	skw_sdio_channel_record = 0;
}

static int skw_sdio_channel_record_show(struct seq_file *seq, void *data)
{
#define SKW_SDIO_CHANNEL_RECORD(s) (skw_sdio_channel_record & (1<<s) ? "enable" : "disable")
#define RECORD_BUFFER_DEPTH (6*1024)

	int i;
        char *records = kzalloc(RECORD_BUFFER_DEPTH, GFP_KERNEL);
	for (i = 0; i < SDIO2_MAX_CH_NUM; i++) {
		seq_printf(seq, "port%d record: %s\n", i, SKW_SDIO_CHANNEL_RECORD(i));
	}

	skw_get_channel_record(records, RECORD_BUFFER_DEPTH);
        seq_printf(seq, "Records:\n %s", records);
        kfree(records);

	return 0;
}

static int skw_sdio_channel_record_open(struct inode *inode, struct file *file)
{
	return single_open(file, &skw_sdio_channel_record_show, inode->i_private);
}

static int skw_sdio_channel_record_control(const char *cmd, bool enable)
{
	u32 port_num = 0;

	port_num = (u32)(cmd[4] - '0');
	if (!strncmp("port", cmd, 4))
		skw_sdio_channel_record_enable(port_num, enable);
	else if (!strncmp("all", cmd, 3))
		skw_sdio_channel_record_enable_all();
	else
		return -EINVAL;

	return 0;
}

static ssize_t skw_sdio_channel_record_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	int i, idx;
	char cmd[32];
	bool enable = false;

	for (idx = 0, i = 0; i < len; i++) {
		char c;

		if (get_user(c, buffer))
			return -EFAULT;

		switch (c) {
		case ' ':
			break;

		case ':':
			cmd[idx] = 0;
			if (!strcmp("enable", cmd))
				enable = true;
			else
				enable = false;
			idx = 0;
			break;

		case '|':
		case '\0':
		case '\n':
			cmd[idx] = 0;
			skw_sdio_channel_record_control(cmd, enable);
			idx = 0;
			break;

		default:
			cmd[idx++] = c;
			idx %= 32;

			break;
		}

		buffer++;
	}

	return len;
}

static const struct file_operations skw_sdio_channel_record_fops = {
	.owner = THIS_MODULE,
	.open = skw_sdio_channel_record_open,
	.read = seq_read,
	.release = single_release,
	.write = skw_sdio_channel_record_write,
};

void skw_sdio_log_level_init(void)
{
	skw_sdio_set_log_level(SKW_SDIO_INFO);

	skw_sdio_enable_func_log(SKW_SDIO_DUMP, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT0, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT1, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT2, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT3, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT4, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT5, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT6, false);
	skw_sdio_enable_func_log(SKW_SDIO_SAVELOG, false);
	skw_sdio_enable_func_log(SKW_SDIO_PORT7, false);
	skw_sdio_add_debugfs("log_level", 0666, NULL, &skw_sdio_log_fops);
	skw_sdio_add_debugfs("Version", 0666, NULL, &skw_version_fops);
	skw_sdio_add_debugfs("Statistic", 0666, NULL, &skw_port_statistic_fops);
	skw_sdio_add_debugfs("CPLog", 0666, NULL, &skw_cp_log_fops);
	skw_sdio_add_debugfs("recovery", 0666, NULL, &skw_recovery_debug_fops);
	skw_sdio_add_debugfs("dumpmem", 0666, NULL, &skw_dump_mem_fops);
	skw_sdio_add_debugfs("BT_ANT", 0666, NULL, &skw_bluetooth_antenna_fops);
	skw_sdio_add_debugfs("BT_UART1", 0666, NULL, &skw_bluetooth_UART1_fops);
	skw_sdio_add_debugfs("WiFi", 0666, NULL, &skw_sdio_wifi_fops);
	skw_sdio_add_debugfs("CHN_REC", 0666, NULL, &skw_sdio_channel_record_fops);
	skw_sdio_procfs_file(NULL, "log_level", 0666, &skw_sdio_log_fops, NULL);
	skw_sdio_procfs_file(NULL,"Version", 0666, &skw_version_fops,NULL);
	skw_sdio_procfs_file(NULL, "Statistic", 0666, &skw_port_statistic_fops, NULL);
	skw_sdio_procfs_file(NULL,"CPLog", 0666, &skw_cp_log_fops,NULL);
	skw_sdio_procfs_file(NULL,"WiFi", 0666, &skw_sdio_wifi_fops,NULL);
	skw_sdio_procfs_file(NULL,"recovery", 0666, &skw_recovery_debug_fops,NULL);
	skw_sdio_procfs_file(NULL,"dumpmem", 0666, &skw_dump_mem_fops,NULL);
	skw_sdio_procfs_file(NULL,"BT_ANT", 0666, &skw_bluetooth_antenna_fops,NULL);
	skw_sdio_procfs_file(NULL,"BT_UART1", 0666, &skw_bluetooth_UART1_fops,NULL);
	skw_sdio_procfs_file(NULL,"CHN_REC", 0666, &skw_sdio_channel_record_fops,NULL);
}
