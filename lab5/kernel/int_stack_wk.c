#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/usb.h>

MODULE_LICENSE("GPL");

#define DEVICE_NAME "int_stack_wk"
#define DRIVER_NAME "int_stack_wk"

#define INT_STACK_WK_MAGIC 'k'
#define INT_STACK_WK_SET_SIZE _IOW(INT_STACK_WK_MAGIC, 1, int)
#define INT_STACK_WK_GET_SIZE _IOR(INT_STACK_WK_MAGIC, 2, int)

#define USB_KEY_VID 0x2357
#define USB_KEY_PID 0x011f

static dev_t dev;
static struct cdev int_stack_wk_cdev;
static struct class *int_stack_wk_class;
static atomic_t usb_key_present = ATOMIC_INIT(0);

struct stack_node {
    struct list_head list;
    int value;
};

struct int_stack_wk {
    struct list_head head;
    int size;
    int max_size;
    struct mutex lock;
};

static int int_stack_wk_open(struct inode *inode, struct file *filp) {
    if (!atomic_read(&usb_key_present))
        return -ENODEV;

    struct int_stack_wk *stack = kmalloc(sizeof(*stack), GFP_KERNEL);
    if (!stack)
        return -ENOMEM;

    INIT_LIST_HEAD(&stack->head);
    stack->size = 0;
    stack->max_size = 0;
    mutex_init(&stack->lock);

    filp->private_data = stack;

    pr_info(DRIVER_NAME ": opened\n");
    return 0;
}

static int int_stack_wk_release(struct inode *inode, struct file *filp) {
    struct int_stack_wk *stack = filp->private_data;
    struct stack_node *node, *tmp;

    list_for_each_entry_safe(node, tmp, &stack->head, list) {
        list_del(&node->list);
        kfree(node);
    }

    kfree(stack);
    return 0;
}

static ssize_t int_stack_wk_read(struct file *filp, char __user *buf, size_t count,
                                  loff_t *pos) {
    if (!atomic_read(&usb_key_present))
        return -ENODEV;

    struct int_stack_wk *stack = filp->private_data;
    struct stack_node *node;
    int value;

    if (count < sizeof(int))
        return -EINVAL;

    mutex_lock(&stack->lock);

    if (list_empty(&stack->head)) {
        mutex_unlock(&stack->lock);
        return 0;
    }

    node = list_first_entry(&stack->head, struct stack_node, list);
    value = node->value;

    list_del(&node->list);
    kfree(node);
    stack->size--;

    mutex_unlock(&stack->lock);

    if (copy_to_user(buf, &value, sizeof(int)))
        return -EFAULT;

    return sizeof(int);
}

static ssize_t int_stack_wk_write(struct file *filp, const char __user *buf,
                                   size_t count, loff_t *pos) {
    if (!atomic_read(&usb_key_present))
        return -ENODEV;

    struct int_stack_wk *stack = filp->private_data;
    struct stack_node *node;
    int value;

    if (count < sizeof(int))
        return -EINVAL;

    if (copy_from_user(&value, buf, sizeof(int)))
        return -EFAULT;

    mutex_lock(&stack->lock);

    if (stack->max_size > 0 && stack->size >= stack->max_size) {
        mutex_unlock(&stack->lock);
        return -ERANGE;
    }

    node = kmalloc(sizeof(*node), GFP_KERNEL);
    if (!node) {
        mutex_unlock(&stack->lock);
        return -ENOMEM;
    }

    node->value = value;
    list_add(&node->list, &stack->head);
    stack->size++;

    mutex_unlock(&stack->lock);

    return sizeof(int);
}

static long int_stack_wk_ioctl(struct file *filp, unsigned int cmd,
                                unsigned long arg) {
    if (!atomic_read(&usb_key_present))
        return -ENODEV;

    struct int_stack_wk *stack = filp->private_data;
    int size;

    switch (cmd) {
    case INT_STACK_WK_SET_SIZE:
        if (copy_from_user(&size, (int __user *)arg, sizeof(int)))
            return -EFAULT;
        if (size <= 0)
            return -EINVAL;
        mutex_lock(&stack->lock);
        if (size < stack->size) {
            mutex_unlock(&stack->lock);
            return -ERANGE;
        }
        stack->max_size = size;
        mutex_unlock(&stack->lock);
        return 0;

    case INT_STACK_WK_GET_SIZE:
        mutex_lock(&stack->lock);
        size = stack->size;
        mutex_unlock(&stack->lock);
        if (copy_to_user((int __user *)arg, &size, sizeof(int)))
            return -EFAULT;
        return 0;

    default:
        return -ENOTTY;
    }
}

static const struct file_operations int_stack_wk_fops = {
    .owner = THIS_MODULE,
    .open = int_stack_wk_open,
    .release = int_stack_wk_release,
    .read = int_stack_wk_read,
    .write = int_stack_wk_write,
    .unlocked_ioctl = int_stack_wk_ioctl,
};

static struct usb_device_id key_table[] = {
    {USB_DEVICE(USB_KEY_VID, USB_KEY_PID)}, {}};
MODULE_DEVICE_TABLE(usb, key_table);

static int key_probe(struct usb_interface *intf,
                     const struct usb_device_id *id) {
    if (IS_ERR(device_create(int_stack_wk_class, NULL, dev, NULL, DEVICE_NAME))) {
        pr_err(DRIVER_NAME ": cannot create device\n");
        return -ENODEV;
    }

    atomic_set(&usb_key_present, 1);
    pr_info(DRIVER_NAME ": USB key inserted, /dev/%s available\n", DEVICE_NAME);
    return 0;
}

static void key_disconnect(struct usb_interface *intf) {
    atomic_set(&usb_key_present, 0);
    device_destroy(int_stack_wk_class, dev);
    pr_info(DRIVER_NAME ": USB key removed, /dev/%s unavailable\n", DEVICE_NAME);
}

static struct usb_driver key_driver = {
    .name = DRIVER_NAME,
    .id_table = key_table,
    .probe = key_probe,
    .disconnect = key_disconnect,
};

static int __init int_stack_wk_init(void) {
    if (alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME) < 0) {
        pr_err(DRIVER_NAME ": cannot allocate major number\n");
        return -1;
    }

    cdev_init(&int_stack_wk_cdev, &int_stack_wk_fops);
    if (cdev_add(&int_stack_wk_cdev, dev, 1) < 0) {
        pr_err(DRIVER_NAME ": cannot add cdev\n");
        goto r_cdev;
    }

    int_stack_wk_class = class_create(DRIVER_NAME);
    if (IS_ERR(int_stack_wk_class)) {
        pr_err(DRIVER_NAME ": cannot create class\n");
        goto r_class;
    }

    if (usb_register(&key_driver) < 0) {
        pr_err(DRIVER_NAME ": cannot register USB driver\n");
        goto r_usb;
    }

    pr_info(DRIVER_NAME ": loaded\n");
    return 0;

r_usb:
    class_destroy(int_stack_wk_class);
r_class:
    cdev_del(&int_stack_wk_cdev);
r_cdev:
    unregister_chrdev_region(dev, 1);
    return -1;
}

static void __exit int_stack_wk_exit(void) {
    usb_deregister(&key_driver);
    if (atomic_read(&usb_key_present))
        device_destroy(int_stack_wk_class, dev);

    class_destroy(int_stack_wk_class);
    cdev_del(&int_stack_wk_cdev);
    unregister_chrdev_region(dev, 1);
    pr_info(DRIVER_NAME ": unloaded\n");
}

module_init(int_stack_wk_init);
module_exit(int_stack_wk_exit);