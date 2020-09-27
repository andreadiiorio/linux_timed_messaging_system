/*
 *TIMED MESSAGING SYSTEM
 *Andrea Di Iorio	277550
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

#include "include/utils.h"	//TODO CHECK
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrea Di Iorio");
#define DEVICE_NAME	"TIMED_MESSAGING_SYS"
#define NUM_MINOR	10	//max concurrent instances supported TODO
#define AUDIT		if(1)

static int 	_open(struct inode *, struct file *);
static int 	_release(struct inode *, struct file *);
static ssize_t	_write(struct file *, const char *, size_t, loff_t *);
static ssize_t	_read (struct file *, char __user *, size_t, loff_t *);
static long	_unlocked_ioctl (struct file *, unsigned int, unsigned long);
static long	_compat_ioctl (struct file *, unsigned int, unsigned long);
static int 	_flush (struct file *, fl_owner_t id);


static int Major;


static int _open(struct inode *inode, struct file *file) {
	//TODO
}
static int _release(struct inode *inode, struct file *file) {
	//TODO
}
static ssize_t _write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	//TODO
}






































































































static struct file_operations fops = {
	.owner =		THIS_MODULE,
	.open =			_open,
	.release =		_release
	.write =		_write,
	.read =			_read,
	._unlocked_ioctl=	_unlocked_ioctl,
	._compat_ioctl=		_compat_ioctl,
	.flush=			_flush,
};//struct define at https://elixir.bootlin.com/linux/latest/source/include/linux/fs.h#L1837


int init_module(void) {
	///register the device driver
	
	Major = __register_chrdev(0,0,NUM_MINOR, DEVICE_NAME, fops);	//create and register a cdev with dyn alloc of major and NUM_MINOR minors
	if (Major < 0) {
	  printk("%s: registering cdev failed\n",DEVICE_NAME);
	  return Major;
	}

	printk(KERN_INFO "%s: registered cdev: Major=%d, numMinors=%d\n",DEVICE_NAME,Major,NUM_MINOR);
	return 0;
}

void cleanup_module(void)
{
	__unregister_chrdev(0,0,NUM_MINOR, DEVICE_NAME);
	printk(KERN_INFO "%s: device unregistered, it was assigned major number %d\n",DEVICE_NAME,Major);
}
