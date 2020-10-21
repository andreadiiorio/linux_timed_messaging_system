/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 */
#ifndef _CORE
#define _CORE

#include <linux/slab.h>	
#include <linux/sched.h>	
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/uaccess.h>	//needed by debian10 K4.19

///core struct
typedef struct list_head list_link;
//device driver state per minor num of device files avaible
typedef struct _session{
	///ioctl touched metadata 
	unsigned long timeoutRd,timeoutWr;
	char limit_flush;	   //if non-zero flush will be limited to calling IOsess

	struct mutex mtx;	   //serialize wr defering with cross session flush()
	//delayed WR
	struct workqueue_struct* workq_writers; //writers workQueue 
	list_link writers_delayed;

	list_link link;
} session;
typedef struct _ddstate{
	unsigned long cumul_msg_size; 	//cumulative size of stored msgs
	list_link sessions;				// || IO-session opened with current devFile
	list_link avaible_messages;		//fifo 2linkedlist queue
	struct mutex	mtx;
	//delayed RD
	list_link readers_delayed;
	wait_queue_head_t waitq_readers;//blocked readers waitqueue
} ddstate;


	
typedef struct _msg{
	char* data;
	unsigned long len;
	list_link link;
} message;

typedef struct _delayed_wr{
	ddstate* minor;		//devF identifing struct where post msg
	session* sess;
	message* msg;		//floating msg to store
	struct delayed_work delayed_work;
	list_link link;
} delayed_write;

#define NULLEVENT   0
#define MSGREADY	1<<0
#define FLUSHED		1<<1

typedef struct _delayed_rd{
	char awake_cond; //& with MSG_READY or FLUSH to check the actual awake cond
	session* sess;	 //iosess where called rd 
	list_link link;
} delayed_read;


///prototypes
//fops prototypes
int		init_ddriver_state(void);
void 	free_ddriver_state(void);
int 	_open(struct inode *, struct file *);
int 	_release(struct inode *, struct file *);
ssize_t	_write(struct file *, const char *, size_t, loff_t *);
ssize_t	_read (struct file *, char __user *, size_t, loff_t *);
long	_unlocked_ioctl (struct file *, unsigned int, unsigned long);
long	_compat_ioctl (struct file *, unsigned int, unsigned long);
int 	_flush (struct file *, fl_owner_t id);

///Support MACROS
//devFile numbers
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(filp)      MAJOR(filp->f_inode->i_rdev)
#define get_minor(filp)      MINOR(filp->f_inode->i_rdev)
#else
#define get_major(filp)      MAJOR(filp->f_dentry->d_inode->i_rdev)
#define get_minor(filp)      MINOR(filp->f_dentry->d_inode->i_rdev)
#endif
//milisec -> jiffies 
#define millis_2_jiffies(mill)  ( mill * HZ / 1000 )

///Configuration MACROS
#define WRITERS_WORKQ	"WRITERS_WORKQ"
#define AUDIT			if(1)
#define DEBUG			if(1)

//Features Modify
#define	TIMEOUT_DEF_MILLIS //if def -> timer expressed in millis in ioctl,otherwise in jiffies
//#define 	DELAYED_WRITER_HIGH_PRIO	//TODO TEST -> writer added to high prio work queue

/*
 * on shared IOsessions (open & fork) serialize critical sections (sess var RW)
 * and print a warning 
 */
#define SHRD_IOSESS_WARN_AND_SERIALIZE 

#define FLUSH_DEFER_WR_CONCURR_FAIL	//defered write fail if concurr with flush 
#endif
