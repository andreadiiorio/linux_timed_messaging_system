/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 */
#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
//#include <linux/pid.h>		/* For pid types */
//#include <linux/tty.h>		/* For the tty declarations */
//#include <linux/version.h>		/* For LINUX_VERSION_CODE */
#include <linux/signal_types.h>
#include <linux/syscalls.h>

#include "../include/timed_messaging_sys.h"
#include "../include/core.h"
#include "../include/utils.h"	

///Mod params
unsigned int Major;
unsigned long max_message_size, max_storage_size;
module_param(Major,uint,0444);	//supported max msg size
module_param(max_message_size,ulong,0660);	//supported max msg size
module_param(max_storage_size,ulong,0660);	//supported max cumulative msg size
/// flex /sys export	//TODO MOVABLE=
static struct kobject* kobj_mod_params;
SYSVAR_GET(max_message_size)
SYSVAR_PUT(max_message_size)

SYSVAR_GET(max_storage_size)
SYSVAR_PUT(max_storage_size)
//
static struct kobj_attribute max_msg_size_attr		= __ATTR(max_message_size,
      0660,SYSVAR_GET_NAME(max_message_size),SYSVAR_PUT_NAME(max_message_size));
static struct kobj_attribute max_storage_size_attr	= __ATTR(max_storage_size,
	  0660,SYSVAR_GET_NAME(max_storage_size),SYSVAR_PUT_NAME(max_storage_size));


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
	int error =		sysfs_create_file(kobj_mod_params, &max_msg_size_attr.attr);	
	error +=		sysfs_create_file(kobj_mod_params, &max_storage_size_attr.attr);
    if (error){
			printk(KERN_ERR "%s: failed to create the kobjs\n",MODNAME);
			return error;
	}
	///register the device driver
	//create and register a cdev with dyn alloc of major and NUM_MINOR minors
	Major = __register_chrdev(0,0,NUM_MINOR, DEVICE_NAME, &fops);	
	if (Major < 0) {
	  printk(KERN_INFO "%s: registering cdev failed\n",DEVICE_NAME);
	  return Major; 
	}
	printk(KERN_INFO "%s: registered cdev: Major=%d, numMinors=%d\n",
		DEVICE_NAME,Major,NUM_MINOR);
	//init internal structures
	error = init_ddriver_state();
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
