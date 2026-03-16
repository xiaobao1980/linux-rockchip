/* SPDX-License-Identifier: GPL-2.0 */

/******************************************************************************
 *
 * Copyright (C) 2020 SeekWave Technology Co.,Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 ******************************************************************************/
#ifndef __SKW_SDIO_DEBUGFS_H__
#define __SKW_SDIO_DEBUGFS_H__
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "skw_sdio.h"
static inline int skw_sdio_default_open(struct inode *node, struct file *fp)
{
	fp->private_data = node->i_private;
	return 0;
}
static inline void  skw_sdio_remove_debugfs(struct dentry *dentry)
{
	debugfs_remove(dentry);
}

struct dentry *skw_sdio_add_debugfs(const char *name, umode_t mode, void *data,
			       const struct file_operations *fops);

struct dentry *skw_sdio_debugfs_subdir(const char *name, struct dentry *parent);
struct dentry *skw_sdio_debugfs_file(struct dentry *parent,
				const char *name, umode_t mode,
				const struct file_operations *fops, void *data);

struct proc_dir_entry *skw_sdio_procfs_subdir(const char *name,
				struct proc_dir_entry *parent);
struct proc_dir_entry *skw_sdio_procfs_file(struct proc_dir_entry *parent,
				       const char *name, umode_t mode,
				       const void *proc_fops, void *data);

int skw_sdio_debugfs_init(void);
void skw_sdio_debugfs_deinit(void);
#endif
