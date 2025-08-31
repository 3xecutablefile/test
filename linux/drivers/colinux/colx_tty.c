// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <uapi/linux/colinux_ring.h>

static unsigned long colx_base;
static unsigned long colx_size;
module_param(colx_base, ulong, 0644);
module_param(colx_size, ulong, 0644);
MODULE_PARM_DESC(colx_base, "Shared mapping base");
MODULE_PARM_DESC(colx_size, "Shared mapping size");

static void __iomem *io;
static struct tty_driver *drv;
static struct workqueue_struct *wq;

struct colx_tty_port {
    struct tty_port port;
    struct delayed_work rx_work;
};

static struct colx_tty_port gport;

static void vtty_rx_work(struct work_struct *ws)
{
    struct colx_tty_port *cp = container_of(to_delayed_work(ws), struct colx_tty_port, rx_work);
    struct tty_port *port = &cp->port;
    if (!io) return;
    struct colx_vtty_ring __iomem *rx = (void __iomem *)((char __iomem *)io + COLX_VTTY_RX_OFF);
    u32 head = readl(&rx->head), tail = readl(&rx->tail), cap = readl(&rx->cap); if (!cap) cap = COLX_VTTY_CAP;
    u32 used = (head - tail) & (cap - 1);
    if (used) {
        u32 first = min(used, cap - (tail & (cap - 1)));
        int room = tty_buffer_request_room(port->tty, used);
        if (room > 0) {
            u32 n = min_t(u32, room, used);
            u32 f2 = min(n, first);
            tty_insert_flip_string(port, (const unsigned char __force __iomem *)&rx->buf[tail & (cap - 1)], f2);
            if (n > f2)
                tty_insert_flip_string(port, (const unsigned char __force __iomem *)&rx->buf[0], n - f2);
            writel((tail + n) & (cap - 1), &rx->tail);
            tty_flip_buffer_push(port);
        }
    }
    queue_delayed_work(wq, &cp->rx_work, msecs_to_jiffies(10));
}

static int colx_tty_open(struct tty_struct *tty, struct file *filp)
{
    tty->driver_data = &gport;
    tty_port_tty_set(&gport.port, tty);
    return 0;
}

static void colx_tty_close(struct tty_struct *tty, struct file *filp)
{
    tty_port_tty_set(&gport.port, NULL);
}

static int colx_tty_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
    if (!io) return -ENODEV;
    struct colx_vtty_ring __iomem *tx = (void __iomem *)((char __iomem *)io + COLX_VTTY_TX_OFF);
    u32 head = readl(&tx->head), tail = readl(&tx->tail), cap = readl(&tx->cap); if (!cap) cap = COLX_VTTY_CAP;
    u32 used = (head - tail) & (cap - 1);
    u32 free = cap - used - 1;
    u32 n = min_t(u32, free, count);
    u32 first = min(n, cap - (head & (cap - 1)));
    memcpy_toio(&tx->buf[head & (cap - 1)], buf, first);
    if (n > first)
        memcpy_toio(&tx->buf[0], buf + first, n - first);
    wmb();
    writel((head + n) & (cap - 1), &tx->head);
    return n;
}

static unsigned int colx_tty_write_room(struct tty_struct *tty)
{
    if (!io) return 0;
    struct colx_vtty_ring __iomem *tx = (void __iomem *)((char __iomem *)io + COLX_VTTY_TX_OFF);
    u32 head = readl(&tx->head), tail = readl(&tx->tail), cap = readl(&tx->cap); if (!cap) cap = COLX_VTTY_CAP;
    u32 used = (head - tail) & (cap - 1);
    u32 free = cap - used - 1;
    return free;
}

static const struct tty_operations ops = {
    .open = colx_tty_open,
    .close = colx_tty_close,
    .write = colx_tty_write,
    .write_room = colx_tty_write_room,
};

static int __init colx_tty_init(void)
{
    if (!colx_base || !colx_size)
        return -EINVAL;
    io = ioremap(colx_base, colx_size);
    if (!io) return -ENOMEM;
    drv = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV);
    if (IS_ERR(drv)) { iounmap(io); io = NULL; return PTR_ERR(drv); }
    drv->driver_name = "colx_tty";
    drv->name = "ttyCOLX";
    drv->major = 0; drv->minor_start = 0;
    drv->type = TTY_DRIVER_TYPE_CONSOLE;
    drv->init_termios = tty_std_termios;
    tty_set_operations(drv, &ops);
    tty_port_init(&gport.port);
    if (tty_register_driver(drv)) { put_tty_driver(drv); tty_port_destroy(&gport.port); iounmap(io); io = NULL; return -EINVAL; }
    tty_port_register_device(&gport.port, drv, 0, NULL);
    wq = alloc_workqueue("colx_tty", WQ_UNBOUND|WQ_MEM_RECLAIM, 1);
    INIT_DELAYED_WORK(&gport.rx_work, vtty_rx_work);
    queue_delayed_work(wq, &gport.rx_work, msecs_to_jiffies(10));
    pr_info("colx_tty: /dev/ttyCOLX0 registered\n");
    return 0;
}

static void __exit colx_tty_exit(void)
{
    cancel_delayed_work_sync(&gport.rx_work);
    if (wq) { destroy_workqueue(wq); wq = NULL; }
    tty_unregister_device(drv, 0);
    tty_unregister_driver(drv);
    tty_port_destroy(&gport.port);
    put_tty_driver(drv);
    if (io) { iounmap(io); io = NULL; }
}

module_init(colx_tty_init);
module_exit(colx_tty_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("coLinux vtty front-end (prototype)");
MODULE_AUTHOR("coLinux 2.0");

