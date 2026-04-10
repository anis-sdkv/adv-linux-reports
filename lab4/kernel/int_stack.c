Ы
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ioctl.h>

MODULE_LICENSE("GPL");

#define DEVICE_NAME "int_stack"

#define INT_STACK_MAGIC 'k'Ы
#define INT_STACK_SET_SIZE _IOW(INT_STACK_MAGIC, 1, int)
#define INT_STACK_GET_SIZE _IOR(INT_STACK_MAGIC, 2, int)

static dev_t dev;
static struct cdev int_stack_cdev;
static struct class *int_stack_class;

struct stack_node {
    struct list_head list;
    int value;
};

struct int_stack {
    struct list_head head;
    int size;
    int max_size;
    struct mutex lock;
};

static int int_stack_open(struct inode *inode, struct file *filp) {
    struct int_stack *stack = kmalloc(sizeof(*stack), GFP_KERNEL);
    if (!stack)
        return -ENOMEM;

    INIT_LIST_HEAD(&stack->head);
    stack->size = 0;
    stack->max_size = 0;
    mutex_init(&stack->lock);

    filp->private_data = stack;

    printk(KERN_INFO "int_stack: opened\n");
    return 0;
}

static int int_stack_release(struct inode *inode, struct file *filp) {
    struct int_stack *stack = filp->private_data;
    struct stack_node *node, *tmp;

    list_for_each_entry_safe(node, tmp, &stack->head, list) {
        list_del(&node->list);
        kfree(node);
    }

    kfree(stack);
    return 0;
}

static ssize_t int_stack_read(struct file *filp, char __user *buf, size_t count,
                              loff_t *pos) {
    struct int_stack *stack = filp->private_data;
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

static ssize_t int_stack_write(struct file *filp, const char __user *buf,
                               size_t count, loff_t *pos) {

    struct int_stack *stack = filp->private_data;
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

static long int_stack_ioctl(struct file *filp, unsigned int cmd,
                            unsigned long arg) {
    struct int_stack *stack = filp->private_data;
    int size;

    switch (cmd) {
    case INT_STACK_SET_SIZE:
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

    case INT_STACK_GET_SIZE:
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

static const struct file_operations int_stack_fops = {
    .owner = THIS_MODULE,
    .open = int_stack_open,
    .release = int_stack_release,
    .read = int_stack_read,
    .write = int_stack_write,
    .unlocked_ioctl = int_stack_ioctl,
};

static int __init int_stack_init(void) {
    if (alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME) < 0) {
        printk(KERN_ERR "int_stack: cannot allocate major number\n");
        return -1;
    }

    cdev_init(&int_stack_cdev, &int_stack_fops);
    if (cdev_add(&int_stack_cdev, dev, 1) < 0) {
        printk(KERN_ERR "int_stack: cannot add cdev\n");
        goto r_cdev;
    }

    int_stack_class = class_create(DEVICE_NAME);
    if (IS_ERR(int_stack_class)) {
        printk(KERN_ERR "int_stack: cannot create class\n");
        goto r_class;
    }

    if (IS_ERR(device_create(int_stack_class, NULL, dev, NULL, DEVICE_NAME))) {
        printk(KERN_ERR "int_stack: cannot create device\n");
        goto r_device;
    }

    printk(KERN_INFO "int_stack: loaded\n");
    return 0;

r_device:
    class_destroy(int_stack_class);
r_class:
    cdev_del(&int_stack_cdev);
r_cdev:
    unregister_chrdev_region(dev, 1);
    return -1;
}

static void __exit int_stack_exit(void) {
    device_destroy(int_stack_class, dev);
    class_destroy(int_stack_class);
    cdev_del(&int_stack_cdev);
    unregister_chrdev_region(dev, 1);
    printk(KERN_INFO "int_stack: unloaded\n");
}

module_init(int_stack_init);
module_exit(int_stack_exit);
