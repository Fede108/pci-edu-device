#include <linux/module.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <asm/atomic.h>

#define MY_BLKDEV_NAME "my_edu_bdev"
#define MAX_MINORS  1
#define KERNEL_SECTOR_SIZE 512
#define NR_SECTORS 8

#define VENDOR_ID 0x1234
#define DEVICE_ID 0x11e8
#define BAR_REGION 0

static int __init edu_bdev_init(void);
static void __exit edu_bdev_exit(void);

MODULE_LICENSE("GPL");

int major;
static atomic_t minor_counter = ATOMIC_INIT(-1);

static struct pci_device_id ids[] = {
    { PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
    { }
};

static struct edu_bdev {
    struct gendisk *disk;
    int major;
    int minor;
    spinlock_t lock;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;
    void __iomem *bar0;
    int users;
} edu_bdev;


static int alloc_bdev(struct edu_bdev **dev, void __iomem *io_addr);
static void cleanup_bdev(struct edu_bdev *dev);

static int my_open(struct gendisk *disk, blk_mode_t mode){
    struct edu_bdev *dev = disk->private_data;
    spin_lock(&dev->lock);
    dev->users++;
    spin_unlock(&dev->lock);
    pr_info("open: edublk, total users %d\n", dev->users);
    return 0;
}

static void my_release(struct gendisk *disk){
    struct edu_bdev *dev = disk->private_data;
    spin_lock(&dev->lock);
    dev->users--;
    spin_unlock(&dev->lock);
    pr_info("close: edublk, total users %d\n", dev->users);
}

static const struct block_device_operations edu_ops = {
	.owner   = THIS_MODULE,
	.open    = my_open,
    .release = my_release,
};

static blk_status_t my_queue_rq(struct blk_mq_hw_ctx *hctx, const struct blk_mq_queue_data *bd) {
    struct bio_vec bvec;
    struct req_iterator iter;

    struct request *rq = bd->rq;
    blk_mq_start_request(rq);
    
    if (blk_rq_is_passthrough(rq)) {
        printk (KERN_NOTICE "Skip non-fs request\n");
        blk_mq_end_request(rq, BLK_STS_IOERR);
        goto out;
    }

    rq_for_each_segment(bvec, rq, iter){
        sector_t sector  = iter.iter.bi_sector;
        unsigned int idx = iter.iter.bi_idx;
        pr_info("queue_rq: sector %llu for bio_vec idx %u (len: %u, offset: %u)\n",
                (unsigned long long)sector, idx, bvec.bv_len, bvec.bv_offset);
    }

    blk_mq_end_request(rq, BLK_STS_OK);

out:
    return BLK_STS_OK;
}

static struct blk_mq_ops my_queue_ops = {
    .queue_rq = my_queue_rq,
};

static int alloc_bdev(struct edu_bdev **d, void __iomem *io_addr){
    int ret;
    struct gendisk *disk;
    struct edu_bdev *dev;

    struct queue_limits lim = {
		.logical_block_size  = KERNEL_SECTOR_SIZE,
		.physical_block_size = KERNEL_SECTOR_SIZE,
		.max_hw_sectors      = NR_SECTORS,
        .max_segments        = 8,
        //.features            = BLK_FEAT_SYNCHRONOUS,
	};

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if(!dev)
       return -ENOMEM;

    spin_lock_init(&dev->lock);
    dev->major = major;
    dev->bar0  = io_addr; 
    dev->minor = atomic_inc_return(&minor_counter);

    dev->tag_set.ops          = &my_queue_ops;
    dev->tag_set.nr_hw_queues = 1;
    dev->tag_set.queue_depth  = 32;
    dev->tag_set.numa_node    = NUMA_NO_NODE;
    dev->tag_set.cmd_size     = 0;
    dev->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;
    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret) {
        printk (KERN_NOTICE "alloc_tag failure\n");
        goto err_free_dev;
    }

    disk = dev->disk = blk_mq_alloc_disk(&dev->tag_set, &lim);
    if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		dev->disk = NULL;
        printk (KERN_NOTICE "alloc_disk failure\n");
		goto err_free_tags;
	}

    disk->major       = major;
	disk->first_minor = dev->minor;
    disk->minors      = MAX_MINORS;
    disk->fops        = &edu_ops;
    disk->private_data = dev;

    sprintf(dev->disk->disk_name, "edublk%d", dev->minor);
	set_capacity(disk, 8);
    ret = add_disk(disk);
    if (ret) {
        goto err_cleanup_disk;
    }

    *d = dev;
    return 0;

err_cleanup_disk:
    put_disk(disk);
err_free_tags:
	blk_mq_free_tag_set(&dev->tag_set);
err_free_dev:
	kfree(dev);
	return ret;
}

static void cleanup_bdev(struct edu_bdev *dev){
    del_gendisk(dev->disk);
    put_disk(dev->disk);
    blk_mq_free_tag_set(&dev->tag_set);
    kfree(dev);
}

static int probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int ret;
    void __iomem *io_addr;
    struct edu_bdev *blkd;

    ret = pci_enable_device(dev);
    if (ret)
        return ret;
    
    pci_set_master(dev);

    ret = pci_request_region(dev, BAR_REGION, "edu_pci");
    if (ret)
        goto err_disable;

    io_addr = pci_iomap(dev, BAR_REGION, 0);
    if (!io_addr) {
        ret = -ENODEV;
        goto err_region;
    }

    ret = alloc_bdev(&blkd, io_addr);
    if(ret != 0)
        goto err_map;
   
    pci_set_drvdata(dev, blkd);

    pr_info("probe: edublk0\n");
    
    return 0;

err_map:    
    pci_iounmap(dev, io_addr);
err_region:
    pci_release_region(dev, BAR_REGION);
err_disable:
    pci_disable_device(dev);
    return ret;
}

static void remove(struct pci_dev *dev)
{
    struct edu_bdev *d = pci_get_drvdata(dev);

    pr_info("remove: edublk0\n");

    pci_iounmap(dev, d->bar0);
    cleanup_bdev(d);
    pci_release_region(dev, BAR_REGION);
    pci_disable_device(dev);
}

static struct pci_driver pci_driver = {
    .name = "edu_pci",
    .id_table = ids,
    .probe = probe,
    .remove = remove,
};

static int __init edu_bdev_init(){
    int ret;

    major = register_blkdev(0, MY_BLKDEV_NAME);
    if (major < 0) {
             printk(KERN_ERR "unable to register mybdev block device\n");
             return -EBUSY;
    }

    ret = pci_register_driver(&pci_driver);
    if (ret){    
        unregister_blkdev(major, MY_BLKDEV_NAME);
        return ret;
    }
    
    pr_info("edublk: module loaded\n");
    return 0;
}

static void __exit edu_bdev_exit(){
    pci_unregister_driver(&pci_driver);
    unregister_blkdev(major, MY_BLKDEV_NAME);
    pr_info("edublk: module unloaded\n");
}

module_init(edu_bdev_init);
module_exit(edu_bdev_exit);
