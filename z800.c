/**
 * eMagin Z800 HMD USB driver
 * 
 * The USB packet info came from the public release of the eMagin
 * EMA SDK v2.2.  Many thanks to eMagin for releasing that data.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 * 
 * Copyright (C) 2006 Adam King <va3pip@gmail.com>
 *
 * v0.6   (ak) initial version
 */

#include "z800.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/delay.h>

#define Z800_VENDOR_ID	0x1641
#define Z800_PRODUCT_ID	0x0120
#define X800_VENDOR_ID	0x1642
#define X800_PRODUCT_ID	0x0120

const u8 BYTEVERB_PEEK_EEPROM      = 0x81;
const u8 BYTEVERB_SET_POKE_ADDR    = 0x82;
const u8 BYTEVERB_POKE_EEPROM      = 0x83;
const u8 BYTEVERB_RESET            = 0x84;
const u8 BYTEVERB_SLEEP            = 0x85;
const u8 BYTEVERB_PEEK_OLED_0      = 0x86;
const u8 BYTEVERB_PEEK_OLED_1      = 0x87;
const u8 BYTEVERB_STEP_BRIGHTNESS  = 0x88;
const u8 BYTEVERB_QUERY_STATE      = 0x89;
const u8 BYTEVERB_WAKE             = 0x8A;
const u8 BYTEVERB_3DMODE           = 0x8B;
const u8 BYTEVERB_3DFLIP           = 0x8C;

static struct usb_device_id z800_table [] = {
	{ USB_DEVICE(Z800_VENDOR_ID, Z800_PRODUCT_ID) },
	{ USB_DEVICE(X800_VENDOR_ID, X800_PRODUCT_ID) },
	{ }
};
MODULE_DEVICE_TABLE(usb, z800_table);

static struct usb_driver z800_driver;

struct usb_z800 {
	struct usb_device *	udev;
	struct usb_interface *interface;
	struct urb *read_urb;
	struct urb *write_urb;
	int read_urb_running;
	int wait_on_write;
	u8 eeprom_byte;
	char *write_buffer;
	char *read_buffer;
};

static int getFirmware( struct usb_z800 *dev, char *buf );
static int z800_usb_write( struct usb_z800 *dev, u8 verb, u8 noun );
static int readBuffer( struct usb_z800 *dev );

static void z800_abort_transfers(struct usb_z800 *dev)
{
	info( "Aborting transfer..." );
	
	/*	if a write is currently occuring, wait
		till it's done
	*/
	while( dev->wait_on_write )
		msleep(10);
		
	if( dev->read_urb_running) {
		dev->read_urb_running = 0;
		usb_kill_urb(dev->read_urb);	
	}
}

/*
* 		File ops
*/
static int z800_open(struct inode *inode, struct file *file)
{
	struct usb_z800 *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;
	
	subminor = iminor(inode);

	interface = usb_find_interface(&z800_driver, subminor);
	if (!interface) {
		err ("Z800: %s - error, can't find device for minor %d",
			__FUNCTION__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	/* create a write and read urb */
	dev->write_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->write_urb) {
		retval = -ENOMEM;
		goto exit;
	}
	dev->read_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!dev->read_urb) {
		retval = -ENOMEM;
		goto exit;
	}

	dev->write_buffer = kmalloc( 64, GFP_KERNEL );
	dev->read_buffer = kmalloc( 64, GFP_KERNEL );
	dev->read_urb_running = 0;
	dev->wait_on_write = 0;
	dev->eeprom_byte = 0;

	/* save our object in the file's private structure */
	file->private_data = dev;

	readBuffer(dev);
	
	info("============ Z800 Opened =============");
exit:
	return retval;
}

static int z800_release(struct inode *inode, struct file *file)
{
	struct usb_z800 *dev;
    
	dev = (struct usb_z800 *)file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* shutdown any running urbs */
	z800_abort_transfers(dev);
	
	usb_free_urb(dev->write_urb);
	usb_free_urb(dev->read_urb);
	kfree(dev->write_buffer);
	kfree(dev->read_buffer);
	
	info("===========  Z800 Released  ===========");    
	return 0;
}

static int z800_ioctl(struct inode *inode, struct file *file, 
			unsigned int cmd, unsigned long arg)
{
	char buf[8];
	struct usb_z800 *dev;
	
	dev = (struct usb_z800 *)file->private_data;
	if( dev == NULL ) 
		return -ENODEV;
	
	memset(buf, 0, 8);
	switch(cmd) {
		case IOCTL_GET_FIRMWARE_VERSION:	
			info( "z800_ioctl: ---- doing firmware... ------" );
			if( getFirmware(dev, buf) != 0 )
				return -EFAULT;			
			if( copy_to_user((void __user *)arg, buf, 2) !=0 ) 
				return -EFAULT;
			break;
		
		case IOCTL_SLEEP:
			info( "z800_ioctl: ---- doing sleep... ------" );
			z800_usb_write( dev, BYTEVERB_SLEEP, 0 );
			break;

		case IOCTL_KEEPALIVE:
			info( "z800_ioctl: ---- doing wake... -------" );
			z800_usb_write( dev, BYTEVERB_WAKE, 0 );
			break;

		case IOCTL_SET_ENABLE_3D:
			info( "z800_ioctl: ---- doing enable3d[%x] ---", (arg?1:0));
			z800_usb_write( dev, BYTEVERB_3DMODE, (u8)(arg?1:0) );
			break;
	
		case IOCTL_CYCLEBRIGHTNESS:
			info( "z800_ioctl: ---- doing stepbright... -------" );
			z800_usb_write( dev, BYTEVERB_STEP_BRIGHTNESS, 0 );
			break;
	
		default:
			info( "z800: UNKNOWN ioctl" );
			return -ENOTTY;
	}
	
	return 0;
}

static void write_callback( struct urb *urb, struct pt_regs *regs )
{
	struct usb_z800 *dev = urb->context;

	/* info( "write_callback: status = %d\n", urb->status ); */
	if (urb->status)
		info( "write-callback ERROR status: %d %d", 
				urb->status, urb->actual_length );
	dev->wait_on_write = 0;
}

/* FIXME: we shouldn't constantly resubmit the in urb - but only
   when we're expecting an urb from the z800. 
*/  
static void read_callback( struct urb *urb, struct pt_regs *regs )
{
	int retval;
	struct usb_z800 *dev = urb->context;

	retval = 0;
	if( urb->status ) {
		if(	urb->status == -ENOENT ||
			urb->status == -ECONNRESET ||
			urb->status == -ESHUTDOWN ) {
				switch(urb->status)
				{
					case -ENOENT:
						info( "read_callback: HELP - ENOENT!" );
						return;
					case -ECONNRESET:
						info( "read_callback: HELP - ECONNRESET!" );
						return;
					case -ESHUTDOWN:
						info( "read_callback: HELP - ESHUTDOWN!" );
						return;
				}
				return;
		}
	}
	
	if( urb->actual_length > 0 ) {
/*		info( "read_callback:data %d [%x][%x][%x][%x][%x][%x]", 
			urb->actual_length, 
			(u8)dev->read_buffer[0],
			(u8)dev->read_buffer[1],
			(u8)dev->read_buffer[2],
			(u8)dev->read_buffer[3],
			(u8)dev->read_buffer[4],
			(u8)dev->read_buffer[5] 
			);
*/		/* we're only interested in BYTEVERB_PEEK_EEPROM */
		if( (u8)dev->read_buffer[2] == BYTEVERB_PEEK_EEPROM ) {
			dev->eeprom_byte = (u8)dev->read_buffer[4];	
		}
	}
	
	
	if(dev->read_urb_running) {
		retval = usb_submit_urb( dev->read_urb, GFP_ATOMIC);
		if( retval )
			info( "read_callback: failed to resubmit (%d)", retval );
	}
}

static int getFirmware( struct usb_z800 *dev, char *buf )
{
	int waitCount;
	memset( buf, 0, 8 );
		
	/* get the MSB of the firmware version	*/
	waitCount = 0;
	z800_usb_write( dev, BYTEVERB_PEEK_EEPROM, 0 );

	/* FIXME: should replace with a read wait_event */
	while( dev->eeprom_byte == 0 )
	{
		msleep(50);
		waitCount++;
		
		/* only wait for 1s */
		if( waitCount > 20 ) {
			info( "getFirmware: Unable to get MSB.  Aborting..." );
			return -1;
		}
	}
	buf[0] = dev->eeprom_byte;
	dev->eeprom_byte = 0;

	/* now the lsb */
	waitCount = 0;
	z800_usb_write( dev, BYTEVERB_PEEK_EEPROM, 1 );
	
	/* FIXME: should replace with a read wait_event */
	while( dev->eeprom_byte == 0 )
	{
		msleep(50);
		waitCount++;
		
		/* only wait for 1s */
		if( waitCount > 20 ) {
			info( "getFirmware: Unable to get LSB.  Aborting..." );
			return -1;
		}
	}
	buf[1] = dev->eeprom_byte;
	dev->eeprom_byte = 0;

	info( "getFirmware: [%x.%x]", (u8)buf[0], (u8)buf[1] );
	return 0;
}	

static int z800_usb_write( struct usb_z800 *dev, u8 verb, u8 noun )
{
	int retval = 0;

	/* 	if another write is already in progress, 
		wait until it's sent
		FIXME: should replace with a wait_event */
	while(dev->wait_on_write) msleep(1);
	
	dev->wait_on_write = 1;

	memset(dev->write_buffer, 0, 64);	
	dev->write_buffer[0] = verb;
	dev->write_buffer[1] = noun;
	
	/* info("writeBuffer: sending urb. verb = %x noun = %x", verb, noun); */ 
	usb_fill_int_urb( dev->write_urb, dev->udev, 
						usb_sndintpipe( dev->udev, 0x02),
						dev->write_buffer,
						35,	// why 35?? I dunno - it's what eMagin uses.
						(usb_complete_t)write_callback,
						dev, 8 );
						
	retval = usb_submit_urb( dev->write_urb, GFP_KERNEL );

	return retval;
}


static int readBuffer( struct usb_z800 *dev )
{
	int retval = 0;

	memset(dev->read_buffer, 0, 64);
	
	/*info("readBuffer: filling and submitting urb"); */
	usb_fill_int_urb( dev->read_urb, dev->udev, 
						usb_rcvintpipe( dev->udev, 0x81),
						dev->read_buffer,
						64,
						(usb_complete_t)read_callback,
						dev, 8 );
	
	retval = usb_submit_urb( dev->read_urb, GFP_KERNEL );
	dev->read_urb_running = 1;
			
	return retval;
}


static struct file_operations z800_fops = {
	.owner   = THIS_MODULE,
	.ioctl   = z800_ioctl,
	.open    = z800_open,
	.release = z800_release,
};

/*
 * 		USB Ops
 */
static struct usb_class_driver z800_class = {
	.name = "z800:%d",
	.fops = &z800_fops,
	.minor_base = 155,
};

static int z800_probe(struct usb_interface *interface, 
			const struct usb_device_id *id)
{
	int retval;
	struct usb_z800 *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		err("Out of memory");
		return -ENOMEM;
	}
	
    dev->udev = usb_get_dev(interface_to_usbdev(interface));
    dev->interface = interface;

	usb_set_intfdata(interface, dev);
	
	retval = usb_register_dev(interface, &z800_class);
	if (retval) {
		err("Not able to get a minor for this device.");
		usb_set_intfdata(interface, NULL);
		return retval;
    }
    
    info("Z800 now registered");
	
	return 0;
}

static void z800_disconnect(struct usb_interface *interface)
{
	struct usb_z800 *dev;
	int minor = interface->minor;

	/* prevent z800_open() from racing z800_disconnect() */
	/* FIXME: don't use the kernel lock */
	lock_kernel();

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &z800_class);

	dev->interface = NULL;
	
	/* if the device is not opened, then we clean up right now */
	/* FIXME: MEMORY LEAK if device is open when unplugged */
/*	if (!dev->open_count) {
		kfree(dev);
	}
*/	
	unlock_kernel();

	info("Z800 #%d now disconnected", minor);	
}

static struct usb_driver z800_driver = {
	.name = "z800",
	.id_table = z800_table,
	.probe = z800_probe,
	.disconnect = z800_disconnect,
};

static int __init z800_init(void)
{
	int retval = 0;

	printk( KERN_ALERT "Registering Z800 Device \n" );
	
	retval = usb_register(&z800_driver);
	if (retval)
		err("usb_register failed. Error number %d", retval);
	return retval;
}

static void __exit z800_exit(void)
{
	printk( KERN_ALERT "z800 exiting...\n" );
	usb_deregister(&z800_driver);
}

module_init(z800_init);
module_exit(z800_exit);


MODULE_LICENSE ("GPL");
MODULE_AUTHOR ("Adam King <va3pip@gmail.com>");
MODULE_VERSION ("1:0.8");
MODULE_DESCRIPTION("USB Z800 EMagin Driver");
