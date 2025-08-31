// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <uapi/linux/colinux_ring.h>

static unsigned long colx_base;
static unsigned long colx_size;
module_param(colx_base, ulong, 0644);
module_param(colx_size, ulong, 0644);
MODULE_PARM_DESC(colx_base, "Shared mapping base");
MODULE_PARM_DESC(colx_size, "Shared mapping size");

static void __iomem *io;
static struct gendisk *gd;
static struct request_queue *q;

static blk_qc_t colx_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd)
{
    struct request *rq = bd->rq;
    blk_mq_start_request(rq);

    if (rq_data_dir(rq) == READ) {
        /* Copy into shared DATA buffer and mark request */
    } else {
        /* Copy from BIO into shared DATA buffer */
    }

    /* Prototype: single-slot request at fixed offsets */
    if (!io) { blk_mq_end_request(rq, BLK_STS_IOERR); return BLK_QC_T_NONE; }

    struct colx_ring_idx __iomem *idx = (void __iomem *)((char __iomem *)io + COLX_VBLK_RING_OFF);
    struct colx_vblk_req __iomem *req = (void __iomem *)((char __iomem *)io + COLX_VBLK_RING_OFF + sizeof(*idx));
    void __iomem *data = (void __iomem *)((char __iomem *)io + COLX_VBLK_DATA_OFF);

    /* Aggregate request into contiguous buffer for prototype */
    u32 len = blk_rq_bytes(rq);
    if (len > COLX_VBLK_DATA_MAX || (len & 511)) { blk_mq_end_request(rq, BLK_STS_IOERR); return BLK_QC_T_NONE; }

    if (rq_data_dir(rq) == WRITE) {
        struct bio_vec bvec; struct bvec_iter iter;
        size_t off = 0; void *kmap;
        rq_for_each_segment(bvec, rq, iter) {
            kmap = kmap_local_page(bvec.bv_page);
            memcpy_toio((char __iomem *)data + off, kmap + bvec.bv_offset, bvec.bv_len);
            kunmap_local(kmap);
            off += bvec.bv_len;
        }
    }

    /* Fill request */
    writeq((u64)(uintptr_t)rq, &req->id);
    writeb(rq_data_dir(rq) == READ ? COLX_VBLK_OP_READ : COLX_VBLK_OP_WRITE, &req->op);
    writeq(blk_rq_pos(rq), &req->lba); /* sectors */
    writel(len, &req->len);
    writel(0, &req->status);

    /* Submit by bumping prod */
    writel(readl(&idx->prod) + 1, &idx->prod);

    /* Busy-wait for completion (prototype); real impl should block + be tick-driven */
    int spins = 0;
    while (readl(&req->status) == 0 && spins++ < 1000000) cpu_relax();

    if (readl(&req->status) == 1) {
        if (rq_data_dir(rq) == READ) {
            struct bio_vec bvec; struct bvec_iter iter; size_t off = 0; void *kmap;
            rq_for_each_segment(bvec, rq, iter) {
                kmap = kmap_local_page(bvec.bv_page);
                memcpy_fromio(kmap + bvec.bv_offset, (char __iomem *)data + off, bvec.bv_len);
                kunmap_local(kmap);
                off += bvec.bv_len;
            }
        }
        blk_mq_end_request(rq, BLK_STS_OK);
    } else {
        blk_mq_end_request(rq, BLK_STS_IOERR);
    }

    return BLK_QC_T_NONE;
}

static const struct blk_mq_ops mq_ops = {
    .queue_rq = colx_queue_rq,
};

static int __init colx_vblk_init(void)
{
    int ret;
    if (!colx_base || !colx_size)
        return -EINVAL;
    io = ioremap(colx_base, colx_size);
    if (!io) return -ENOMEM;

    q = blk_mq_init_sq_queue(&(struct blk_mq_tag_set){
        .ops = &mq_ops,
        .nr_hw_queues = 1,
        .queue_depth = 64,
        .cmd_size = 0,
        .numa_node = NUMA_NO_NODE,
        .flags = BLK_MQ_F_SHOULD_MERGE,
        .nr_maps = 1,
    }, &mq_ops, 64, BLK_MQ_F_SHOULD_MERGE);
    if (IS_ERR(q)) { ret = PTR_ERR(q); q = NULL; goto err_unmap; }
    blk_queue_logical_block_size(q, 512);
    blk_queue_physical_block_size(q, 512);

    gd = alloc_disk(1);
    if (!gd) { ret = -ENOMEM; goto err_q; }
    gd->major = 0;
    gd->first_minor = 0;
    gd->minors = 1;
    gd->fops = NULL;
    gd->queue = q;
    snprintf(gd->disk_name, sizeof(gd->disk_name), "colxblk0");
    set_capacity(gd, (sector_t)(COLX_VBLK_DATA_MAX / 512)); /* prototype capacity; real via host */
    add_disk(gd);
    pr_info("colx_vblk: registered /dev/%s\n", gd->disk_name);
    return 0;
err_q:
    blk_cleanup_queue(q); q = NULL;
err_unmap:
    iounmap(io); io = NULL;
    return ret;
}

static void __exit colx_vblk_exit(void)
{
    if (gd) { del_gendisk(gd); put_disk(gd); gd = NULL; }
    if (q) { blk_cleanup_queue(q); q = NULL; }
    if (io) { iounmap(io); io = NULL; }
}

module_init(colx_vblk_init);
module_exit(colx_vblk_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("coLinux vblk front-end (prototype)");
MODULE_AUTHOR("coLinux 2.0");

