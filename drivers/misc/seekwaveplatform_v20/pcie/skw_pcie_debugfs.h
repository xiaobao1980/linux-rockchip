/******************************************************************************
 *
 * Copyright(c) 2020-2030  Seekwave Corporation.
 *
 *****************************************************************************/
#ifndef __SKW_PCIE_DEBUGFS_H__
#define __SKW_PCIE_DEBUGFS_H__

#include <linux/fs.h>
#include <linux/debugfs.h>

static inline int skw_pcie_default_open(struct inode *node, struct file *fp)
{
	fp->private_data = node->i_private;
	return 0;
}

static inline void  skw_pcie_remove_debugfs(struct dentry *dentry)
{
	debugfs_remove(dentry);
}

struct dentry *skw_pcie_add_debugfs(const char *name, umode_t mode, void *data,
				   const struct file_operations *fops);

int skw_pcie_debugfs_init(void);
void skw_pcie_debugfs_deinit(void);

#endif
