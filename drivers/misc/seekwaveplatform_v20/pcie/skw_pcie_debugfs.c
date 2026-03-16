/*****************************************************************************
 * Copyright(c) 2020-2030  Seekwave Corporation.
 * SEEKWAVE TECH LTD..CO
 *Seekwave Platform the pcie log debug fs
 *FILENAME:skw_pcie_debugfs.c
 *DATE:2022-04-11
 *MODIFY:
 *
 **************************************************************************/

#include "skw_pcie_debugfs.h"
#include "skw_pcie_log.h"
#include "skw_pcie_drv.h"

static struct dentry *skw_pcie_root_dir;

static ssize_t skw_pcie_default_read(struct file *fp, char __user *buf, size_t len,
				loff_t *offset)
{
	return 0;
}

static ssize_t skw_pcie_state_write(struct file *fp, const char __user *buffer,
				size_t len, loff_t *offset)
{
	return len;
}

static const struct file_operations skw_pcie_state_fops = {
	.open = skw_pcie_default_open,
	.read = skw_pcie_default_read,
	.write = skw_pcie_state_write,
};

struct dentry *skw_pcie_add_debugfs(const char *name, umode_t mode, void *data,
				   const struct file_operations *fops)
{
	skw_pcie_dbg("%s:name: %s\n",__func__,name);

	return debugfs_create_file(name, mode, skw_pcie_root_dir, data, fops);
}

int skw_pcie_debugfs_init(void)
{
	skw_pcie_root_dir = debugfs_create_dir("skwpcie", NULL);
	if (IS_ERR(skw_pcie_root_dir))
		return PTR_ERR(skw_pcie_root_dir);

	// skw_pcie_add_debugfs("state", 0666, wiphy, &skw_pcie_state_fops);
	// skw_pcie_add_debugfs("log_level", 0444, wiphy, &skw_pcie_log_fops);

	return 0;
}

void skw_pcie_debugfs_deinit(void)
{
	skw_pcie_dbg("%s :traced\n", __func__);

	debugfs_remove_recursive(skw_pcie_root_dir);
}
