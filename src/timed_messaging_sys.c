/*
 *  Copyright 2020 Andrea Di Iorio
 *	This file is part of linux_timed_messaging_system.

 *   linux_timed_messaging_system is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   linux_timed_messaging_system is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with linux_timed_messaging_system.  If not, see <https://www.gnu.org/licenses/>.
 */
#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
//#include <linux/pid.h>			/* For pid types */
//#include <linux/tty.h>			/* For the tty declarations */
//#include <linux/version.h>		/* For LINUX_VERSION_CODE */
#include <linux/signal_types.h>
#include <linux/syscalls.h>

#include "../include/timed_messaging_sys.h"
#include "../include/core.h"
#include "../include/utils.h"	

///Mod params
int Major;
unsigned long max_message_size, max_storage_size,num_minors;
module_param(Major,int,0444);				//supported max msg size
module_param(max_message_size,ulong,0660);	//supported max msg size
module_param(max_storage_size,ulong,0660);	//supported max cumulative msg size
module_param(num_minors,ulong,0444);	//supported minors num,just to authomatize char devF creation

static struct file_operations fops = {
	.owner =			THIS_MODULE,
	.open =				_open,
	.release =			_release,
	.write =			_write,
	.read =				_read,
	.unlocked_ioctl=	_unlocked_ioctl,
	.flush=				_flush,
};


int __init mod_init (void) {
	///exporting module parameters to /sys/kern
	//actually creating another kernel object
	int error;
	
	///register the device driver
	//create and register a cdev with dyn alloc of major and NUM_MINOR minors
	Major = __register_chrdev(0,0,NUM_MINOR, DEVICE_NAME, &fops);	
	if (Major < 0) {
	  printk(KERN_INFO "%s: registering cdev failed\n",DEVICE_NAME);
	  return Major; 
	}
	num_minors=NUM_MINOR;//just to authomatize char devF creation
	printk(KERN_INFO "%s: registered cdev: Major=%d, numMinors=%d\n",
		DEVICE_NAME,Major,NUM_MINOR);
	//init internal structures
	error = init_ddriver_state();
	max_message_size=1<<12;
	max_storage_size =1<<16;
    if (error)	printk("%s: failed allocate internal structs\n",MODNAME);
	return error;
}

void __exit mod_end(void)
{
	free_ddriver_state();
	__unregister_chrdev(0,0,NUM_MINOR, DEVICE_NAME);
	printk(KERN_INFO "%s: device with major number %d unregistered\n",MODNAME,Major);
}

module_init(mod_init);
module_exit(mod_end);
MODULE_LICENSE("GPL"); 
MODULE_AUTHOR("Andrea Di Iorio");
