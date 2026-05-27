#include <linux/fs.h>
#include <linux/genhd.h>

#define MY_BLKDEV_NAME   "my_edu_bdev"

static struct my_edu_device {
    struct gendisk *disk;
    spinlock_t lock;
    struct request_queue *queue;
    struct blk_mq_tag_set tag_set;
    void __iomem *io_base;
}

static struct blk_mq_ops my_mq_ops = {
    .queue_rq = queue_rq,
};

static int __my_init(){
    int major, ret;
    struct my_edu_dev *dev;
    struct gendisk *disk;

    struct queue_limits lim = {
		.logical_block_size  = 512,
		.physical_block_size = 512,
		.max_hw_sectors      = 8,
        .max_segments        = 8,
        .features            = 0,
	};

    major = register_blkdev(0, MY_BLKDEV_NAME);
    if (major < 0) {
             printk(KERN_ERR "unable to register mybdev block device\n");
             return -EBUSY;
     }

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if(!dev)
       return -ENOMEM;

    spin_lock_init(&dev->lock);

    dev->tag_set.ops          = &my_queue_ops;
    dev->tag_set.nr_hw_queues = 1;
    dev->tag_set.queue_depth  = 32;
    dev->tag_set.numa_node    = NUMA_NO_NODE;
    dev->tag_set.cmd_size     = 0;
    dev->tag_set.flags        = BLK_MQ_F_SHOULD_MERGE;
    ret = blk_mq_alloc_tag_set(&dev->tag_set);
    if (ret) {
        goto out_err;
    }

    disk = dev->disk = blk_mq_alloc_disk(&dev->tag_set, &lim, dev);
    if (IS_ERR(disk)) {
		ret = PTR_ERR(disk);
		dev->disk = NULL;
        printk (KERN_NOTICE "alloc_disk failure\n");
		goto err_free_tags;
	}

    disk->major       = major;
	disk->first_minor = 0;
    strcpy(edu->disk->disk_name, "edublk0");
	set_capacity(disk, 8);
	add_disk(dev->disk);

err_free_tags:
	blk_mq_free_tag_set(&gdev->tag_set);
err_free_dev:
	kfree(gdev);
	gdev = NULL;
	return ret;
}



