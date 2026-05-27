#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#define DEVICE_NAME "my_edu_pci"
#define CLASS_NAME "cdev_edu"

#define VENDOR_ID 0x1234
#define DEVICE_ID 0x11e8
#define BAR_REGION 0

static dev_t devnum;
static struct class *cl;

static struct file_operations pugs_fops;

struct my_dev {
    struct pci_dev *dev;
    struct cdev cdev;
    void __iomem *hw_regs;
    int irq;
    u32 factorial;
    u8 done;
    wait_queue_head_t my_queue;
    spinlock_t my_lock;
    struct semaphore sem;
};

static struct pci_device_id ids[] = {
    { PCI_DEVICE(VENDOR_ID, DEVICE_ID) },
    { }
};

irqreturn_t my_handler(int irq_no, void* data){
    struct my_dev *d = data;
    u32 status, result;

    status = ioread32(d->hw_regs + 0x24);   
    result = ioread32(d->hw_regs + 0x08);
    iowrite32(status, d->hw_regs + 0x64);   

    spin_lock(&d->my_lock);
    d->factorial = result;
    d->done = 1;
    spin_unlock(&d->my_lock);
    
    wake_up_interruptible(&d->my_queue); 
    up(&d->sem);
    
    printk(KERN_INFO DEVICE_NAME": interrupt trigger, result: %d\n", result);
    return IRQ_HANDLED;
}


static int probe(struct pci_dev *dev, const struct pci_device_id *id)
{
    int ret, irq;
    struct my_dev *d;
    struct device *dev_ret;

    d = kzalloc(sizeof(*d), GFP_KERNEL);
    if (!d)
        return -ENOMEM;

    d->dev = dev;
    pci_set_drvdata(dev, d);

    init_waitqueue_head(&d->my_queue);
    spin_lock_init (&d->my_lock);
    sema_init(&d->sem, 1);

    ret = pci_enable_device(dev);
    if (ret)
        goto err_free;
    pci_set_master(dev);

    ret = pci_request_region(dev, BAR_REGION, "edu_pci");
    if (ret)
        goto err_disable;

    d->hw_regs = pci_iomap(dev, BAR_REGION, 0);
    if (!d->hw_regs) {
        ret = -ENODEV;
        goto err_region;
    }

    ret = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_MSIX | PCI_IRQ_INTX);
    if (ret < 0)
        goto err_iounmap;

    irq = pci_irq_vector(dev, 0);
    d->irq = irq;

    ret = request_threaded_irq(irq, my_handler, NULL, 0, "edu_pci", d);
    if (ret)
        goto err_vectors;

    cdev_init(&d->cdev, &pugs_fops);
    d->cdev.owner = THIS_MODULE;

    ret = cdev_add(&d->cdev, devnum, 1);
    if (ret)
        goto err_irq;

    dev_ret = device_create(cl, NULL, devnum, NULL, DEVICE_NAME);
    if (IS_ERR(dev_ret)) {
        ret = PTR_ERR(dev_ret);
        goto err_cdev;
    }

    return 0;

err_cdev:
    cdev_del(&d->cdev);
err_irq:
    free_irq(d->irq, d);
err_vectors:
    pci_free_irq_vectors(dev);
err_iounmap:
    pci_iounmap(dev, d->hw_regs);
err_region:
    pci_release_region(dev, BAR_REGION);
err_disable:
    pci_disable_device(dev);
err_free:
    kfree(d);
    return ret;
}

static void remove(struct pci_dev *dev)
{
    struct my_dev *d = pci_get_drvdata(dev);

    pr_info("ownership remove\n");

    device_destroy(cl, devnum);
    cdev_del(&d->cdev);
    free_irq(d->irq, d);
    pci_free_irq_vectors(dev);
    pci_iounmap(dev, d->hw_regs);
    pci_release_region(dev, BAR_REGION);
    pci_disable_device(dev);
    kfree(d);
}

static struct pci_driver pci_driver = {
    .name = "edu_pci",
    .id_table = ids,
    .probe = probe,
    .remove = remove,
};

static int my_close(struct inode *i, struct file *f)
{
    printk(KERN_INFO DEVICE_NAME": close()\n");
    return 0;
}

static int my_open(struct inode *i, struct file *f)
{
    struct my_dev *d;

    d = container_of(i->i_cdev, struct my_dev, cdev);
    f->private_data = d;

    printk(KERN_INFO DEVICE_NAME": open()\n");
    return 0;
}

static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    struct my_dev *d = f->private_data;
    unsigned long flags;
    u32 compute;

    if (len < sizeof(compute))
        return -EINVAL;

    if (copy_from_user(&compute, buf, sizeof(compute)))
        return -EFAULT;
    
    if (down_interruptible(&d->sem))
        return -ERESTARTSYS;
    
    spin_lock_irqsave(&d->my_lock, flags);
    d->done = 0;
    spin_unlock_irqrestore(&d->my_lock, flags);

    iowrite32(0x80, d->hw_regs + 0x20);  
    iowrite32(compute, d->hw_regs + 0x08);

    return sizeof(compute);
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{ 
    unsigned long flags;
    u32 value;
    int ret;
    struct my_dev *d = f->private_data;

    ret = wait_event_interruptible(d->my_queue, READ_ONCE(d->done));   
    if (ret != 0)
        return ret;

    spin_lock_irqsave(&d->my_lock, flags);
    value = d->factorial;
    spin_unlock_irqrestore(&d->my_lock, flags);
    
    if (copy_to_user(buf, &value, sizeof(value)))
        return -EFAULT;
    
    return sizeof(value);
}

static struct file_operations pugs_fops = {
    .owner   = THIS_MODULE,
    .open    = my_open,
    .release = my_close,
    .read    = my_read,
    .write   = my_write,
};

static int __init pci_edu_init(void) {
    int ret;

    if ((ret = alloc_chrdev_region(&devnum, 0, 1, DEVICE_NAME)) < 0)
        return ret;

    cl = class_create(CLASS_NAME);
    if (IS_ERR(cl)) {
        unregister_chrdev_region(devnum, 1);
        return PTR_ERR(cl);
    }

    ret = pci_register_driver(&pci_driver);
    if (ret){
        class_destroy(cl);
        unregister_chrdev_region(devnum, 1);
        return ret;
    }
    
    return 0;
}

static void __exit pci_edu_exit(void){
    pci_unregister_driver(&pci_driver);
    class_destroy(cl);
    unregister_chrdev_region(devnum, 1); 
}

module_init(pci_edu_init);
module_exit(pci_edu_exit);
MODULE_LICENSE("GPL");
