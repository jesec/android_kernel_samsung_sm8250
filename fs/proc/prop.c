// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int prop_proc_show(struct seq_file *m, void *v)
{
	seq_puts(m, "ro.config.knox=v00");
	seq_putc(m, '\n');
	seq_puts(m, "ro.config.tima=0");
	seq_putc(m, '\n');
	seq_puts(m, "ro.config.iccc_version=iccc_disabled");
	seq_putc(m, '\n');
	return 0;
}

static int __init proc_prop_init(void)
{
	proc_create_single("prop", 0, NULL, prop_proc_show);
	return 0;
}
fs_initcall(proc_prop_init);
