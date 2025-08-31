// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <uapi/linux/colinux_ring.h>

static unsigned long colx_base;
static unsigned long colx_size;
module_param(colx_base, ulong, 0644);
module_param(colx_size, ulong, 0644);
MODULE_PARM_DESC(colx_base, "Physical or ioremap'able base of coLinux shared mapping");
MODULE_PARM_DESC(colx_size, "Size of mapping");

static void __iomem *colx_io;

static ssize_t colx_read(struct file *f, char __user *ubuf, size_t len, loff_t *ppos)
{
    struct colx_ring_hdr hdr;
    if (!colx_io)
        return -ENODEV;
    if (*ppos >= sizeof(hdr))
        return 0;
    if (len > sizeof(hdr) - *ppos)
        len = sizeof(hdr) - *ppos;
    memcpy_fromio(&hdr, colx_io, sizeof(hdr));
    if (copy_to_user(ubuf, (u8 *)&hdr + *ppos, len))
        return -EFAULT;
    *ppos += len;
    return len;
}

static const struct file_operations colx_fops = {
    .owner = THIS_MODULE,
    .read = colx_read,
    .llseek = default_llseek,
};

static struct miscdevice colx_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "colx0",
    .fops = &colx_fops,
    .mode = 0600,
};

static int __init colx_init(void)
{
    int ret;
    if (!colx_base || !colx_size)
        return -EINVAL;
    colx_io = ioremap(colx_base, colx_size);
    if (!colx_io)
        return -ENOMEM;
    ret = misc_register(&colx_misc);
    if (ret) {
        iounmap(colx_io);
        colx_io = NULL;
        return ret;
    }
    pr_info("colinux: mapped 0x%lx bytes at 0x%lx, /dev/%s ready\n", colx_size, colx_base, colx_misc.name);
    return 0;
}

static void __exit colx_exit(void)
{
    misc_deregister(&colx_misc);
    if (colx_io) {
        iounmap(colx_io);
        colx_io = NULL;
    }
}

module_init(colx_init);
module_exit(colx_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("coLinux 2.0 core front-end (experimental)");
MODULE_AUTHOR("coLinux 2.0");

