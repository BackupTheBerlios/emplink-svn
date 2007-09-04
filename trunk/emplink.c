/*
 * USB Serial Converter Generic functions
 *
 * Copyright (C) 1999 - 2002 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License version
 *	2 as published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/usb.h>
#include <asm/uaccess.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,17)
#include <linux/usb/serial.h>
#else
#include <../drivers/usb/serial/usb-serial.h>
#endif

static int debug;

static __u16 vendor  = 0x04b8;
static __u16 product = 0x0514;

int emplink_open(struct usb_serial_port *port, struct file* filp);
void emplink_close(struct usb_serial_port *port, struct file* filp);
static void emplink_cleanup (struct usb_serial_port *port);

static int emplink_probe(struct usb_serial *serial, const struct usb_device_id *id);
void usb_serial_emplink_shutdown (struct usb_serial *serial);
void usb_serial_emplink_write_bulk_callback (struct urb *urb, struct pt_regs *regs);

static struct usb_device_id emplink_device_ids[2]; /* Initially all zeroes. */

/* All of the device info needed for the Generic Serial Converter */
struct usb_serial_driver usb_serial_emplink_device = {
	.driver = {
		.owner =	THIS_MODULE,
		.name =		"emplink",
	},
	.id_table =		emplink_device_ids,
	.num_interrupt_in =	0,
	.num_bulk_in =		0,
	.num_bulk_out =		1,
	.num_ports =		1,
	.open	=		emplink_open,
	.close = 		emplink_close,
	.probe = 		emplink_probe,
};

/* we want to look at all devices, as the vendor/product id can change
 * depending on the command line argument */
static struct usb_device_id emplink_serial_ids[] = {
	{.driver_info = 42},
	{}
};

static int emplink_probe(struct usb_serial *serial,
			       const struct usb_device_id *id)
{
	const struct usb_device_id *id_pattern;
	struct usb_host_interface *iface_desc;
	
	id_pattern = usb_match_id(serial->interface, emplink_device_ids);
	if (id_pattern != NULL) {
	    
	    iface_desc = serial->interface->cur_altsetting;
	    dbg("Probing on interface %i", iface_desc->desc.bInterfaceNumber);
	    if (iface_desc->desc.bInterfaceNumber != 2) {
		dbg("Invalid interface");
		return -ENODEV;
	    }
	    dbg("EMPLINK converter found");
	    return 0;
	}
	return -ENODEV;
}

static struct usb_driver emplink_driver = {
	.name =		"usbserial_emplink",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	emplink_serial_ids,
	.no_dynamic_id = 	1,
};


int usb_serial_emplink_register (void)
{
	int retval = 0;

	emplink_device_ids[1].idVendor=0;
	emplink_device_ids[1].idProduct=0;
	
	dbg("%s", __FUNCTION__);
	emplink_device_ids[0].idVendor = vendor;
	emplink_device_ids[0].idProduct = product;
	emplink_device_ids[0].match_flags = USB_DEVICE_ID_MATCH_VENDOR | USB_DEVICE_ID_MATCH_PRODUCT;

	/* register our generic driver with ourselves */
	retval = usb_serial_register (&usb_serial_emplink_device);
	if (retval)
		goto exit;
	retval = usb_register(&emplink_driver);
	if (retval) {
		dbg("usb_register failed()");
		usb_serial_deregister(&usb_serial_emplink_device);
	}
exit:
	return retval;
}

void usb_serial_emplink_deregister (void)
{
	/* remove our driver */
	dbg("%s", __FUNCTION__);
	usb_deregister(&emplink_driver);
	usb_serial_deregister (&usb_serial_emplink_device);
}

struct timer_list timer;
struct work_struct queue;
DECLARE_WAIT_QUEUE_HEAD (emplink_wait);

void queue_routine(void *l_port)
{
    int ret = 0;
    struct usb_serial_port *port = (struct usb_serial_port*)l_port;
    struct usb_device *dev;
    short length;
    char buf[100];
    int i;
    
    
    if (port) {
        dev = port->serial->dev;
    } else {
	dbg("%s: NULL parameter passed",__FUNCTION__);
	return;
    }
    
    if (dev) {
        dbg("Sending control URB");
        ret = usb_control_msg(dev,
		    usb_rcvctrlpipe(dev,0),
		    0x01,
		    (USB_TYPE_VENDOR | USB_DIR_IN),
		    0x00,0x00,&length,sizeof(length),HZ/20);
        dbg("Device returned %i bytes",ret);
        if (ret == 2); {
	    dbg("%s: returned value = 0x%i",__FUNCTION__,length);
	    if (length > 0) {
		memset(buf,0,sizeof(buf));
	        ret = usb_control_msg(dev,
			    usb_rcvctrlpipe(dev,0),
	    		    0x02,
			    (USB_TYPE_VENDOR | USB_DIR_IN),
			    0x00,0x00,buf,sizeof(buf)-1,HZ/20);
		if (ret > 0) {
		    dbg("Device returned %s", buf);
		    tty_buffer_request_room(port->tty,ret);
		    for (i=0; i < ret; i++) {
    			tty_insert_flip_char(port->tty,buf[i],TTY_NORMAL);
		    }
		    tty_flip_buffer_push(port->tty);
		}
	    }
	}
    } else {
	dbg("Passed invalid *dev");
    }
    wake_up_interruptible(&emplink_wait);
}

void emplink_timer(unsigned long nil)
{
    schedule_work(&queue);
    mod_timer(&timer,jiffies+HZ/10);
}


int emplink_open (struct usb_serial_port *port, struct file *filp)
{
//	struct usb_serial *serial = port->serial;
//	int result = 0;

	dbg("%s - port %d", __FUNCTION__, port->number);

	/* force low_latency on so that our tty_push actually forces the data through, 
	   otherwise it is scheduled, and with high data rates (like with OHCI) data
	   can get lost. */
	if (port->tty)
		port->tty->low_latency = 1;
	
	// Insert some characters in the port

	INIT_WORK(&queue,queue_routine,port);
	
	init_timer(&timer);
	timer.function=emplink_timer;
	timer.data = 0;
	timer.expires = jiffies;
	
	add_timer(&timer);
	return 0;
}

void emplink_close(struct usb_serial_port *port, struct file* filp)
{
     dbg("%s",__FUNCTION__);
    del_timer_sync(&timer);
    cancel_delayed_work(&queue);
    schedule_work(&queue);
    interruptible_sleep_on(&emplink_wait);
    emplink_cleanup(port);
    
}

static void emplink_cleanup (struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;

	dbg("%s - port %d", __FUNCTION__, port->number);

	if (serial->dev) {
		/* shutdown any bulk reads that might be going on */
		if (serial->num_bulk_out)
			usb_kill_urb(port->write_urb);
	}
}

void usb_serial_emplink_close (struct usb_serial_port *port, struct file * filp)
{
	dbg("%s - port %d", __FUNCTION__, port->number);
	emplink_cleanup (port);
}

module_init(usb_serial_emplink_register);
module_exit(usb_serial_emplink_deregister);

MODULE_LICENSE("GPL");

module_param(debug,bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debbuging enabled");

