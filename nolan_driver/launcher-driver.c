/*
 * Missle Launcher driver
 *
 * Derrived from USB Skeleton Driver:
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver is based on the 2.6.3 version of drivers/usb/usb-skeleton.c
 * but has been rewritten to be easier to read and use.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#define LAUNCHER_VENDOR_ID              0x2123
#define LAUNCHER_PRODUCT_ID             0x1010

#define LAUNCHER_NODE                   "launcher"
#define LAUNCHER_CTRL_BUFFER_SIZE       8
#define LAUNCHER_CTRL_REQUEST_TYPE      0x21
#define LAUNCHER_CTRL_REQUEST           0x09
#define LAUNCHER_CTRL_VALUE             0x0        
#define LAUNCHER_CTRL_INDEX             0x0
#define LAUNCHER_CTRL_COMMAND_PREFIX    0x02

/* Define these values to match your devices */
#define USB_MISS_LAUNCH_VENDOR_ID LAUNCHER_VENDOR_ID
#define USB_MISS_LAUNCH_PRODUCT_ID LAUNCHER_PRODUCT_ID

/* table of devices that work with this driver */
static const struct usb_device_id miss_launch_table[] = {
    {USB_DEVICE(USB_MISS_LAUNCH_VENDOR_ID, USB_MISS_LAUNCH_PRODUCT_ID)},
    {} /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, miss_launch_table);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Missile launcher for CPRE488 MP-3");
MODULE_AUTHOR("Eastburn, Ohnesorge");

/* Get a minor range for your devices from the usb maintainer */
#define USB_MISS_LAUNCH_MINOR_BASE 192

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER (PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT 8
/* arbitrarily chosen */

/* Structure to hold all of our device specific stuff */
struct usb_miss_launch
{
    struct usb_device *udev;         /* the usb device for this device */
    struct usb_interface *interface; /* the interface for this device */
    struct semaphore limit_sem;      /* limiting the number of writes in progress */
    struct urb *int_in_urb;          /* the urb to read data with */
    struct usb_endpoint_descriptor *int_in_endpoint;
    int errors;          /* the last request tanked */
    spinlock_t err_lock; /* lock for errors */
    struct kref kref;
    struct mutex io_mutex;                /* synchronize I/O with disconnect */
};
#define to_miss_launch_dev(d) container_of(d, struct usb_miss_launch, kref)

static struct usb_driver miss_launch_driver;
static void miss_launch_draw_down(struct usb_miss_launch *dev);

static void miss_launch_delete(struct kref *kref)
{
    struct usb_miss_launch *dev = to_miss_launch_dev(kref);

    usb_free_urb(dev->int_in_urb);
    usb_put_dev(dev->udev);
    kfree(dev);
}

static int miss_launch_open(struct inode *inode, struct file *file)
{
    struct usb_miss_launch *dev;
    struct usb_interface *interface;
    int subminor;
    int retval = 0;

    subminor = iminor(inode);

    interface = usb_find_interface(&miss_launch_driver, subminor);
    if (!interface)
    {
        pr_err("%s - error, can't find device for minor %d\n",
               __func__, subminor);
        retval = -ENODEV;
        goto exit;
    }

    dev = usb_get_intfdata(interface);
    if (!dev)
    {
        retval = -ENODEV;
        goto exit;
    }

    retval = usb_autopm_get_interface(interface);
    if (retval)
        goto exit;

    /* increment our usage count for the device */
    kref_get(&dev->kref);

    /* save our object in the file's private structure */
    file->private_data = dev;

exit:
    return retval;
}

static int miss_launch_release(struct inode *inode, struct file *file)
{
    struct usb_miss_launch *dev;

    dev = file->private_data;
    if (dev == NULL)
        return -ENODEV;

    /* allow the device to be autosuspended */
    mutex_lock(&dev->io_mutex);
    if (dev->interface)
        usb_autopm_put_interface(dev->interface);
    mutex_unlock(&dev->io_mutex);

    /* decrement the count on our device */
    kref_put(&dev->kref, miss_launch_delete);
    return 0;
}

static int miss_launch_flush(struct file *file, fl_owner_t id)
{
    struct usb_miss_launch *dev;
    int res;

    dev = file->private_data;
    if (dev == NULL)
        return -ENODEV;

    /* wait for io to stop */
    mutex_lock(&dev->io_mutex);
    miss_launch_draw_down(dev);

    /* read out errors, leave subsequent opens a clean slate */
    spin_lock_irq(&dev->err_lock);
    res = dev->errors ? (dev->errors == -EPIPE ? -EPIPE : -EIO) : 0;
    dev->errors = 0;
    spin_unlock_irq(&dev->err_lock);

    mutex_unlock(&dev->io_mutex);

    return res;
}

static ssize_t miss_launch_read(struct file *file, char *buffer, size_t count,
                                loff_t *ppos)
{
    // No reads are to be done, so simply return EOF.
    return 0;
}

static ssize_t miss_launch_write(struct file *file, const char *user_buffer,
                                 size_t count, loff_t *ppos)
{
    struct usb_miss_launch *dev;
    int retval = 0;
    struct urb *urb = NULL;
    char *buf = NULL;
    size_t writesize = min(count, (size_t)MAX_TRANSFER);

    pr_alert("Attempting a write operation!\n");

    dev = file->private_data;

    /* verify that we actually have some data to write */
    if (count == 0)
        goto exit;

    /*
     * limit the number of URBs in flight to stop a user from using up all
     * RAM
     */
    if (!(file->f_flags & O_NONBLOCK))
    {
        if (down_interruptible(&dev->limit_sem))
        {
            retval = -ERESTARTSYS;
            goto exit;
        }
    }
    else
    {
        if (down_trylock(&dev->limit_sem))
        {
            retval = -EAGAIN;
            goto exit;
        }
    }

    spin_lock_irq(&dev->err_lock);
    retval = dev->errors;
    if (retval < 0)
    {
        /* any error is reported once */
        dev->errors = 0;
        /* to preserve notifications about reset */
        retval = (retval == -EPIPE) ? retval : -EIO;
    }
    spin_unlock_irq(&dev->err_lock);
    if (retval < 0)
        goto error;

    /* create a urb, and a buffer for it, and copy the data to the urb */
    urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!urb)
    {
        retval = -ENOMEM;
        goto error;
    }

    // Allocate space for buffer
    buf = kmalloc(writesize, GFP_KERNEL);

    // Zeroize the newly allocated space.
    memset(buf, 0, writesize);

    if (!buf)
    {
        retval = -ENOMEM;
        goto error;
    }

    if (copy_from_user(buf, user_buffer, writesize))
    {
        retval = -EFAULT;
        goto error;
    }

    /* this lock makes sure we don't submit URBs to gone devices */
    mutex_lock(&dev->io_mutex);
    if (!dev->interface)
    { /* disconnect() was called */
        mutex_unlock(&dev->io_mutex);
        retval = -ENODEV;
        goto error;
    }

    retval = usb_control_msg(dev->udev,
                             usb_sndctrlpipe(dev->udev, 0),
                             LAUNCHER_CTRL_REQUEST,
                             LAUNCHER_CTRL_REQUEST_TYPE,
                             LAUNCHER_CTRL_VALUE,
                             LAUNCHER_CTRL_INDEX,
                             buf,
                             writesize,
                             0); // TODO: Pick a better timeout value, this waits forever as of now.

    mutex_unlock(&dev->io_mutex);
    if (retval < 0)
    {
        dev_err(&dev->interface->dev,
                "%s - ERROR: Failed writing control URB. Error Code: %d\n",
                __func__, retval);
        goto error;
    }

    /*
     * release our reference to this urb, the USB core will eventually free
     * it entirely
     */
    usb_free_urb(urb);

    pr_alert("Finished a write operation!\n");

    return writesize;
error:
    if (urb)
    {
        usb_free_urb(urb);
    }
    up(&dev->limit_sem);

exit:
    kfree(buf);
    return retval;
}

static const struct file_operations miss_launch_fops = {
    .owner = THIS_MODULE,
    .read = miss_launch_read,
    .write = miss_launch_write,
    .open = miss_launch_open,
    .release = miss_launch_release,
    .flush = miss_launch_flush,
    .llseek = noop_llseek,
};

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver miss_launch_class = {
    .name = "miss_launch%d",
    .fops = &miss_launch_fops,
    .minor_base = USB_MISS_LAUNCH_MINOR_BASE,
};

static int miss_launch_probe(struct usb_interface *interface,
                             const struct usb_device_id *id)
{
    struct usb_miss_launch *dev;
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;
    int retval = -ENOMEM;

    /* allocate memory for our device state and initialize it */
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev)
    {
        dev_err(&interface->dev, "Out of memory\n");
        goto error;
    }
    kref_init(&dev->kref);
    sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
    mutex_init(&dev->io_mutex);
    spin_lock_init(&dev->err_lock);

    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

    // Get the interrupt in endpoint
    iface_desc = interface->cur_altsetting;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
    {
        endpoint = &iface_desc->endpoint[i].desc;

        // Check to see if the endpoint is of type "in interrupt"
        if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT))
        {
            dev->int_in_endpoint = endpoint;
        }
    }

    if (!dev->int_in_endpoint)
    {
        dev_err(&interface->dev, "Could not find interrupt in endpoint!");
        goto error;
    }

    /* save our data pointer in this interface device */
    usb_set_intfdata(interface, dev);

    /* we can register the device now, as it is ready */
    retval = usb_register_dev(interface, &miss_launch_class);
    if (retval)
    {
        /* something prevented us from registering this driver */
        dev_err(&interface->dev,
                "Not able to get a minor for this device.\n");
        usb_set_intfdata(interface, NULL);
        goto error;
    }

    /* let the user know what node this device is now attached to */
    dev_info(&interface->dev,
             "USB Missile Launcher device now attached to USBMissileLauncher-%d",
             interface->minor);
    return 0;

error:
    if (dev)
        /* this frees allocated memory */
        kref_put(&dev->kref, miss_launch_delete);
    return retval;
}

static void miss_launch_disconnect(struct usb_interface *interface)
{
    struct usb_miss_launch *dev;
    int minor = interface->minor;

    dev = usb_get_intfdata(interface);
    usb_set_intfdata(interface, NULL);

    /* give back our minor */
    usb_deregister_dev(interface, &miss_launch_class);

    /* prevent more I/O from starting */
    mutex_lock(&dev->io_mutex);
    dev->interface = NULL;
    mutex_unlock(&dev->io_mutex);

    /* decrement our usage count */
    kref_put(&dev->kref, miss_launch_delete);

    dev_info(&interface->dev, "USB Missile Launcher #%d now disconnected", minor);
}

static void miss_launch_draw_down(struct usb_miss_launch *dev)
{
    usb_kill_urb(dev->int_in_urb);
}

static int miss_launch_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct usb_miss_launch *dev = usb_get_intfdata(intf);

    if (!dev)
        return 0;
    miss_launch_draw_down(dev);
    return 0;
}

static int miss_launch_resume(struct usb_interface *intf)
{
    return 0;
}

static int miss_launch_pre_reset(struct usb_interface *intf)
{
    struct usb_miss_launch *dev = usb_get_intfdata(intf);

    mutex_lock(&dev->io_mutex);
    miss_launch_draw_down(dev);

    return 0;
}

static int miss_launch_post_reset(struct usb_interface *intf)
{
    struct usb_miss_launch *dev = usb_get_intfdata(intf);

    /* we are sure no URBs are active - no locking needed */
    dev->errors = -EPIPE;
    mutex_unlock(&dev->io_mutex);

    return 0;
}

static struct usb_driver miss_launch_driver = {
    .name = "missile_launcher",
    .probe = miss_launch_probe,
    .disconnect = miss_launch_disconnect,
    .suspend = miss_launch_suspend,
    .resume = miss_launch_resume,
    .pre_reset = miss_launch_pre_reset,
    .post_reset = miss_launch_post_reset,
    .id_table = miss_launch_table,
    .supports_autosuspend = 1,
};

module_usb_driver(miss_launch_driver);
