/*****************************************************************************
 * Copyright(c) 2020-2030  Seekwave Corporation.
 * SEEKWAVE TECH LTD..CO
 *Seekwave Platform the sdio log debug fs
 *FILENAME:skw_sdio_debugfs.c
 *DATE:2022-04-11
 *MODIFY:
 *
 **************************************************************************/
#include "skw_sdio_debugfs.h"
#include "skw_sdio_log.h"
#include "skw_sdio.h"
#include <generated/utsrelease.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
static struct dentry *skw_sdio_root_dir;
static struct dentry *skw_sdio_debugfs_root;
static struct proc_dir_entry *skw_sdio_proc_root;

static int skw_sdio_proc_show(struct seq_file *seq, void *v)
{
#define SKW_BSP_CONFIG_INT(conf)                                          \
	do {                                                          \
		seq_printf(seq, "%s=%d\n", #conf, conf);              \
	} while (0)

#define SKW_BSP_CONFIG_BOOL(conf)                                         \
	do {                                                          \
		if (IS_ENABLED(conf))                                 \
			seq_printf(seq, "%s=y\n", #conf);             \
		else                                                  \
			seq_printf(seq, "# %s is not set\n", #conf);  \
	} while (0)

#define SKW_BSP_CONFIG_STRING(conf)                                       \
	do {                                                          \
		seq_printf(seq, "%s=\"%s\"\n", #conf, conf);          \
	} while (0)

	seq_puts(seq, "\n");
#if 0
	seq_printf(seq, "Kernel Version:  \t%s\n"
			"Wi-Fi Driver:    \t%s\n"
			"Wi-Fi Branch:    \t%s\n",
			UTS_RELEASE,
			SKW_BSP_VERSION,
			SKW_BSP_BRANCH);
#endif
	seq_printf(seq, "Kernel Version:  \t%s\n",
			UTS_RELEASE);
	seq_puts(seq, "\n");

	SKW_BSP_CONFIG_BOOL(CONFIG_SKW_PCIE);
	SKW_BSP_CONFIG_BOOL(CONFIG_SEEKWAVE_BSP_DRIVERS);
	SKW_BSP_CONFIG_BOOL(CONFIG_SKW_USB);
	SKW_BSP_CONFIG_BOOL(CONFIG_SKW_SDIOHAL);
	SKW_BSP_CONFIG_BOOL(CONFIG_SKW_BSP_UCOM);
	SKW_BSP_CONFIG_BOOL(CONFIG_SKW_BSP_BOOT);
	SKW_BSP_CONFIG_BOOL(CONFIG_SEEKWAVE_PLD_RELEASE);
#ifdef CONFIG_SKW6316_RX_REORDER_TIMEOUT
	SKW_BSP_CONFIG_INT(CONFIG_SKW6316_RX_REORDER_TIMEOUT);
#endif
#ifdef CONFIG_SKW6316_CHIP_ID
	SKW_BSP_CONFIG_STRING(CONFIG_SKW6316_CHIP_ID);
#endif
	seq_puts(seq, "\n");

	return 0;
}

static int skw_sdio_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, skw_sdio_proc_show, NULL);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops skw_sdio_proc_fops = {
	.proc_open = skw_sdio_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};
#else
static const struct file_operations skw_sdio_proc_fops = {
	.open = skw_sdio_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

struct dentry *skw_sdio_debugfs_subdir(const char *name, struct dentry *parent)
{
	struct dentry *de, *pentry;

	pentry = parent ? parent : skw_sdio_debugfs_root;
	if (!pentry)
		return NULL;

	de = debugfs_create_dir(name, pentry);

	return IS_ERR(de) ? NULL : de;
}

struct dentry *skw_sdio_debugfs_file(struct dentry *parent,
				const char *name, umode_t mode,
				const struct file_operations *fops, void *data)
{
	struct dentry *de, *pentry;

	pentry = parent ? parent : skw_sdio_debugfs_root;
	if (!pentry)
		return NULL;

	de = debugfs_create_file(name, mode, pentry, data, fops);

	return IS_ERR(de) ? NULL : de;
}

struct proc_dir_entry *skw_sdio_procfs_subdir(const char *name,
				struct proc_dir_entry *parent)
{
	struct proc_dir_entry *dentry = parent ? parent : skw_sdio_proc_root;

	if (!dentry)
		return NULL;

	return proc_mkdir_data(name, 0, dentry, NULL);
}

struct proc_dir_entry *skw_sdio_procfs_file(struct proc_dir_entry *parent,
				       const char *name, umode_t mode,
				       const void *fops, void *data)
{
	struct proc_dir_entry *dentry = parent ? parent : skw_sdio_proc_root;

	if (!dentry)
		return NULL;

	return proc_create_data(name, mode, dentry, fops, data);
}

int skw_sdio_proc_init(void)
{
	skw_sdio_proc_root = proc_mkdir("skwsdio", NULL);
	if (!skw_sdio_proc_root)
		pr_err("creat proc skwsdio failed\n");

	skw_sdio_procfs_file(skw_sdio_proc_root, "profile", 0, &skw_sdio_proc_fops, NULL);
#if 0
	skw_sdio_debugfs_root = debugfs_create_dir("skwsdio_tmp", NULL);
	if (IS_ERR(skw_sdio_debugfs_root)) {
		pr_err("create skwsdio failed, ret: %ld\n",
		       PTR_ERR(skw_sdio_debugfs_root));

		skw_sdio_debugfs_root = NULL;
	}
#endif

	return 0;
}

void skw_sdio_proc_deinit(void)
{
	//debugfs_remove_recursive(skw_sdio_debugfs_root);
	proc_remove(skw_sdio_proc_root);
}
static ssize_t skw_sdio_default_read(struct file *fp, char __user *buf, size_t len,
				loff_t *offset)
{
	return 0;
}

static ssize_t skw_sdio_state_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	return len;
}

static const struct file_operations skw_sdio_state_fops = {
	.open = skw_sdio_default_open,
	.read = skw_sdio_default_read,
	.write = skw_sdio_state_write,
};

struct dentry *skw_sdio_add_debugfs(const char *name, umode_t mode, void *data,
			       const struct file_operations *fops)
{
	skw_sdio_dbg("%s:name: %s\n",__func__,name);

	return debugfs_create_file(name, mode, skw_sdio_root_dir, data, fops);
}

int skw_sdio_debugfs_init(void)
{
	skw_sdio_proc_init();
	skw_sdio_root_dir = debugfs_create_dir("skwsdio", NULL);
	if (IS_ERR(skw_sdio_root_dir))
		return PTR_ERR(skw_sdio_root_dir);

	// skw_sdio_add_debugfs("state", 0666, wiphy, &skw_sdio_state_fops);
	// skw_sdio_add_debugfs("log_level", 0444, wiphy, &skw_sdio_log_fops);

	return 0;
}

void skw_sdio_debugfs_deinit(void)
{
	skw_sdio_dbg("%s :traced\n", __func__);
	skw_sdio_proc_deinit();
	debugfs_remove_recursive(skw_sdio_root_dir);
}
