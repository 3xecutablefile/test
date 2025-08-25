#include "cooperative.h"

#ifdef __KERNEL__

#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/netdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

/* Cooperative yield syscall */
SYSCALL_DEFINE0(coop_yield)
{
    schedule();
    return 0;
}

/* Shared memory mapping stubs */
int coop_alloc_shared(struct file *file, size_t size)
{
    return -ENOSYS;
}

int coop_map_guest_pages(struct file *file, unsigned long addr, size_t size)
{
    return -ENOSYS;
}

/* In-kernel ring buffer used for console I/O */
#define COOP_RING_SIZE 4096

static unsigned char coop_ring_buf[COOP_RING_SIZE];
static size_t coop_ring_head;
static size_t coop_ring_tail;
static DEFINE_MUTEX(coop_ring_lock);
static DECLARE_WAIT_QUEUE_HEAD(coop_ring_wq);

static size_t coop_ring_avail(void)
{
    return (coop_ring_head + COOP_RING_SIZE - coop_ring_tail) % COOP_RING_SIZE;
}

static size_t coop_ring_space(void)
{
    return COOP_RING_SIZE - coop_ring_avail() - 1;
}

static ssize_t coop_send(const unsigned char *buf, size_t len)
{
    size_t i;

    mutex_lock(&coop_ring_lock);
    for (i = 0; i < len && coop_ring_space(); ++i) {
        coop_ring_buf[coop_ring_head] = buf[i];
        coop_ring_head = (coop_ring_head + 1) % COOP_RING_SIZE;
    }
    mutex_unlock(&coop_ring_lock);

    if (i)
        wake_up_interruptible(&coop_ring_wq);

    return i;
}

static ssize_t coop_recv(unsigned char *buf, size_t maxlen)
{
    size_t i;

    mutex_lock(&coop_ring_lock);
    for (i = 0; i < maxlen && coop_ring_avail(); ++i) {
        buf[i] = coop_ring_buf[coop_ring_tail];
        coop_ring_tail = (coop_ring_tail + 1) % COOP_RING_SIZE;
    }
    mutex_unlock(&coop_ring_lock);

    return i;
}

/* Console device implementation */
static int coop_console_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t coop_console_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    ssize_t ret;
    unsigned char *kbuf;

    if (len == 0)
        return 0;

    if (wait_event_interruptible(coop_ring_wq, coop_ring_head != coop_ring_tail))
        return -ERESTARTSYS;

    kbuf = kmalloc(len, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    ret = coop_recv(kbuf, len);
    if (ret > 0 && copy_to_user(buf, kbuf, ret))
        ret = -EFAULT;

    kfree(kbuf);
    return ret;
}

static ssize_t coop_console_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
    ssize_t ret;
    unsigned char *kbuf;

    if (len == 0)
        return 0;

    kbuf = memdup_user(buf, len);
    if (IS_ERR(kbuf))
        return PTR_ERR(kbuf);

    ret = coop_send(kbuf, len);
    kfree(kbuf);
    return ret;
}

static int coop_console_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations coop_console_fops = {
    .owner = THIS_MODULE,
    .open = coop_console_open,
    .read = coop_console_read,
    .write = coop_console_write,
    .release = coop_console_release,
};

static struct miscdevice coop_console_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "coop_console",
    .fops = &coop_console_fops,
};

int coop_console_init(void)
{
    return misc_register(&coop_console_dev);
}

void coop_console_exit(void)
{
    misc_deregister(&coop_console_dev);
}

/* Block device skeleton */
int coop_block_init(void)
{
    /* TODO: register /dev/coop_root block device */
    return 0;
}

void coop_block_exit(void)
{
    /* TODO: unregister block device */
}

/* Network device skeleton */
int coop_net_init(void)
{
    /* TODO: register /dev/coop_net0 network device */
    return 0;
}

void coop_net_exit(void)
{
    /* TODO: unregister network device */
}

static int __init coop_module_init(void)
{
    int ret;

    ret = coop_console_init();
    if (ret)
        return ret;

    ret = coop_block_init();
    if (ret)
        goto err_console;

    ret = coop_net_init();
    if (ret)
        goto err_block;

    return 0;

err_block:
    coop_block_exit();
err_console:
    coop_console_exit();
    return ret;
}

static void __exit coop_module_exit(void)
{
    coop_net_exit();
    coop_block_exit();
    coop_console_exit();
}

module_init(coop_module_init);
module_exit(coop_module_exit);

MODULE_LICENSE("GPL");

#else

#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

struct coop_ring {
    sem_t slots; // free buffer slots
    sem_t items; // filled buffer slots
    size_t head;
    size_t tail;
    size_t size; // capacity of data[]
    unsigned char data[];
};

static struct coop_ring *g_ring = NULL;
static void *g_shared = NULL;
static size_t g_size = 0;
static int g_fd = -1;
static char g_name[64];

int coop_init(const char *name, size_t size) {
    if (!name || name[0] != '/') {
        fprintf(stderr, "coop_init: shared memory name must begin with '/'\n");
        return -1;
    }

    g_fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (g_fd < 0) {
        perror("shm_open");
        return -1;
    }
    size_t total = sizeof(struct coop_ring) + size;
    if (ftruncate(g_fd, total) != 0) {
        perror("ftruncate");
        close(g_fd);
        g_fd = -1;
        return -1;
    }
    g_shared = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0);
    if (g_shared == MAP_FAILED) {
        perror("mmap");
        close(g_fd);
        g_fd = -1;
        return -1;
    }
    g_ring = (struct coop_ring *)g_shared;
    g_ring->head = g_ring->tail = 0;
    g_ring->size = size;
    sem_init(&g_ring->slots, 1, size);
    sem_init(&g_ring->items, 1, 0);
    g_size = total;
    strncpy(g_name, name, sizeof(g_name) - 1);
    g_name[sizeof(g_name) - 1] = '\0';
    return 0;
}

int coop_yield(void) {
    return syscall(__NR_coop_yield);
}

ssize_t coop_send(const void *buf, size_t len) {
    if (!g_ring || !buf)
        return -1;
    const unsigned char *cbuf = buf;
    for (size_t i = 0; i < len; ++i) {
        sem_wait(&g_ring->slots);
        g_ring->data[g_ring->head] = cbuf[i];
        g_ring->head = (g_ring->head + 1) % g_ring->size;
        sem_post(&g_ring->items);
    }
    return (ssize_t)len;
}

ssize_t coop_recv(void *buf, size_t maxlen) {
    if (!g_ring || !buf)
        return -1;
    unsigned char *cbuf = buf;
    size_t i;
    for (i = 0; i < maxlen; ++i) {
        if (sem_trywait(&g_ring->items) != 0)
            break;
        cbuf[i] = g_ring->data[g_ring->tail];
        g_ring->tail = (g_ring->tail + 1) % g_ring->size;
        sem_post(&g_ring->slots);
    }
    return (ssize_t)i;
}

void coop_cleanup(void) {
    if (g_ring) {
        sem_destroy(&g_ring->slots);
        sem_destroy(&g_ring->items);
        g_ring = NULL;
    }
    if (g_shared && g_shared != MAP_FAILED) {
        munmap(g_shared, g_size);
        g_shared = NULL;
        g_size = 0;
    }
    if (g_fd >= 0) {
        close(g_fd);
        shm_unlink(g_name);
        g_fd = -1;
        g_name[0] = '\0';
    }
}

#endif // __KERNEL__
