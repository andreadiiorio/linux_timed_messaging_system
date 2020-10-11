/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 *
 *  Core of module -> implementation of core d.driver api
 *
 */

#include "../include/core.h"
#include "../include/timed_messaging_sys.h"

#define DEBUG2	if(1)

//internal function prototypes
static int _add_msg(ddstate* minor, message* msg,char event);
static void _notify_reader(ddstate* minor,char notify_all,char event,session* sess);
static void _cancel_pending_wr(session* sess);
static void _delayed_write (struct work_struct *work);
ddstate minors[NUM_MINOR];
extern unsigned long max_message_size, max_storage_size;	//mod params
/*
 * initialize dev driver state for each supported minor
 * at module mounting
 */
int init_ddriver_state(void){
	int i,err=0;
	for (i=0;i<NUM_MINOR;i++){
		ddstate* dMinor=minors + i;
		mutex_init(&dMinor -> mtx);
		init_waitqueue_head(&dMinor -> waitq_readers);
		INIT_LIST_HEAD(&dMinor -> sessions);
		INIT_LIST_HEAD(&dMinor -> avaible_messages);
		INIT_LIST_HEAD(&dMinor -> readers_delayed);
	}
	return err;
}

/*
 * dealloc dev driver state for each supported minor
 * at module unmounting
 */
void free_ddriver_state(void){
	int i;
	session* sess;
	message* msg;
	for (i=0;i<NUM_MINOR;i++){
		ddstate* dMinor=minors + i;
		DEBUG printk(KERN_INFO "%s: free minor %d struct\n",MODNAME,i);
		//TODO not unmountable module if exist a session?
		sess=list_first_entry_or_null(&dMinor->sessions,session,link);
		if (sess) printk(KERN_ERR "%s: unmounting module with a session alive\n",
					MODNAME);

		//delete unreaded messages
		list_for_each_entry(msg,&dMinor->avaible_messages,link){
			DEBUG printk(KERN_INFO "%s: free msg: %s \n",MODNAME,msg->data);
			list_del(&msg->link);
			kfree(msg->data);
			kfree(msg);
		
		}
	}
}

int _open(struct inode *inode, struct file *file) {
	ddstate* minor=minors + get_minor(file);//AUTO CHECK EX. MINOR BY PREV REGISTRATION
	unsigned int wq_flags=WQ_MEM_RECLAIM;
	session* sess;
	//DEBUG printk(KERN_INFO "%s: open on devFile with minor %d\n",MODNAME,get_minor(file));
	if (!(sess = kzalloc(sizeof(session),GFP_KERNEL)))	{
		printk(KERN_ERR "%s: failed to alloc a new session",MODNAME);
		return -ENOMEM;
	} //sess zeroed -> dflt timers=0,limit_flush flag OFF

	//init session fields
	mutex_init(&sess->mtx);
	//non-predefined (events), bound workQueue, 
	//default=256 exe contexts per cpu assigniable to work items
	//	mem pressure -> >=1 exe context for sure
	#ifdef DELAYED_WRITER_HIGH_PRIO
	wq_flags |= WQ_HIGHPRI;
	#endif
	if(!(sess->workq_writers = alloc_workqueue(WRITERS_WORKQ,wq_flags,0))){
		kfree(sess);
		printk(KERN_ERR "%s: writers workqueue alloc error\n",MODNAME);
		return -ENOMEM;
	}
	sess->timeoutRd=sess->timeoutWr=0;	//dflt timeout
	INIT_LIST_HEAD(&sess->writers_delayed);
	INIT_LIST_HEAD(&sess->link);
	// link session <- ddstate 
	mutex_lock(&minor -> mtx);
	list_add_tail(&sess->link,&minor->sessions);
	mutex_unlock(&minor -> mtx);
	file -> private_data = (void*) sess;
	return 0;
}

int _release(struct inode *inode, struct file *file) {
	ddstate* minor=minors + get_minor(file);
	session* sess =(session*) file -> private_data;
	DEBUG printk(KERN_INFO "%s: release on devFile with minor %d\n",MODNAME,get_minor(file));

	//wait deleyed write to complete
	flush_workqueue(sess-> workq_writers);
	destroy_workqueue(sess->workq_writers);
	//unlinks
	mutex_lock(&minor -> mtx);
	list_del(&sess->link);
	mutex_unlock(&minor -> mtx);
	
	kfree(sess);
	return 0;
}

int _is_dev_file_full(ddstate* minor,unsigned long len_toadd){
	if (minor->cumul_msg_size + len_toadd <= max_storage_size )	return 0;
	printk(KERN_INFO "%s: exceeded max_storage_size=%lu,discarding msg of %lu bytes",
		MODNAME,max_storage_size,len_toadd);
	return -ENOSPC;
}

/*
 * @file:	calling IO sess
 * @buff:	msg contento to write
 * @len:	msg len
 * @off:	unsed
 *
 * Returns:
 * 	written size in byte, otherwise
 * 	%-ENOMEM	(some control struct alloc failed)
 * 	%-ENOSPC	(deviceFile full)
 * after msg preparation, evaluate if the curr op.mode require to defer the write
 * if so,enqueue the operation to the custom workqueue as a work item
 * otherwise,safelly enqueue the new message to the avaible ones, 
 * notifing blocked readers for the new msg
 */
ssize_t _write(struct file *file, const char *buff, size_t len, loff_t *off) {
	session*	sess=file -> private_data;
	ddstate* 	minor=minors + get_minor(file);
	message* 	msg;
	char*		kbuff;
	delayed_write* pending_wr=NULL; //NULL to distinguish in err dealloc 
	int		ret,err;
	
	DEBUG printk(KERN_INFO "%s: write on devFile with minor %d of %ld bytes\n",
		MODNAME,get_minor(file),len);
	/////get the user message
	
	if (!(msg = kmalloc(sizeof(msg),GFP_KERNEL))){
		printk(KERN_ERR "%s: msg alloc failed",MODNAME);
		return -ENOMEM;	
	}
	//allocate a tmp buff to copy the user message with contiguos mem if
	//possible, otherwise with non contiguos, using dflt numa node
	if (!(kbuff = kvmalloc(len,GFP_KERNEL))){
		printk(KERN_ERR "%s: kbuff alloc failed",MODNAME);
		ret = -ENOMEM;
		goto free_msg;
	}
	if ((ret=copy_from_user(kbuff,buff,len))){
		printk(KERN_ERR "%s: copy_from_user left %d bytes",MODNAME,ret);
		ret = -ENOMEM;
		goto free_kbuff	;
	}
	//prepare the message
	msg->data=kbuff;
	msg->len=len;

	//Check if ddriver IO sess operative mode require to defer the write
	if (sess->timeoutWr){
		DEBUG printk(KERN_INFO "%s: Deferring write of %d\n",
			MODNAME,get_minor(file));
		//deleay the write
		if (!(pending_wr = kmalloc(sizeof(delayed_write),GFP_KERNEL))){
			printk(KERN_ERR "%s: error alloc delayed_write",MODNAME);
			err=-ENOMEM;
			goto free_kbuff;
		}
		//init pending write and link with the others of the curr sess
		pending_wr -> minor = minor;
		pending_wr -> sess  = sess;
		pending_wr -> msg = msg;
		INIT_LIST_HEAD(&pending_wr->link);
		//serialize with flush
#ifdef FLUSH_DEFER_WR_CONCURR_FAIL	//defered write fail if concurr with flush 
		if(!mutex_trylock(&sess->mtx)){
			AUDIT printk(KERN_INFO "%s: deferred write while flushing the device,\
				 aborting",MODNAME);	
			goto free_pending_wr;
		}
		list_add_tail(&pending_wr->link,&sess->writers_delayed);
		mutex_unlock(&sess->mtx);
#else						      //defered write delayed if concurr with flush 
		mutex_lock(&sess->mtx);				
		list_add_tail(&pending_wr->link,&sess->writers_delayed);
		mutex_unlock(&sess->mtx);
#endif
		//queue work item in the custom  WRITERS_WORKQ
		INIT_DELAYED_WORK(&pending_wr->delayed_work,_delayed_write);
		queue_delayed_work(sess->workq_writers,&pending_wr->delayed_work,
						sess->timeoutWr); //after op.mode WR delay->queue WR work
		return 0;	    //deferred write -> no bytes actually written
	}
	//add the message to the devFile instance
	ret = _add_msg(minor,msg,MSGREADY);
	if (ret > 0)		return ret;	//successful write
	
	//error return
	free_pending_wr:
		if(pending_wr)	kfree(pending_wr); //not free if err but delayed the msg
	free_kbuff:		
		kfree(kbuff);
	free_msg:		
		kfree(msg);
	return ret;
	
			

}
static void _list_msgs(ddstate* minor){
	DEBUG2 {
		unsigned long m=(minor-minors)/(sizeof(minor));
		message* msg; unsigned i=0;
		list_for_each_entry(msg,&minor->avaible_messages,link)
			printk(KERN_INFO "%s: minor %lu -> msg %u: %s [%lu long]\n",MODNAME,m,i++,msg->data,msg->len);
	}
}
/*
 * @minor: ddstate relative to an opened session to a specific devFile
 * @msg:   message to add
 * @event: event to propagate to blocked readers waiting [if any]
 * 		   if = %NULLEVENT, readers not notified
 * will be notified the topmost blocked reader, waiting for a msg
 * Return: len of added message or %-ENOSPC if the device is full
 */
static int _add_msg(ddstate* minor, message* msg,char event){
	INIT_LIST_HEAD(&msg->link);
	mutex_lock(&minor->mtx);
	if ( _is_dev_file_full(minor,msg->len) ){
		mutex_unlock(&minor->mtx);
		return -ENOSPC;	
	}
	list_add_tail(&msg->link,&minor->avaible_messages);
	minor->cumul_msg_size+=msg->len;

	if (event != NULLEVENT)		_notify_reader(minor,0,event,NULL);
	DEBUG2	_list_msgs(minor);
	mutex_unlock(&minor->mtx);
	DEBUG printk(KERN_INFO "%s: minor %lu , written a new message %s of len %lu.\
		new cumulative size of devFile: %lu -> left %lu \n",MODNAME,
		((unsigned long) minor - (unsigned long)minors)/(sizeof(minor)),msg->data,msg->len,minor->cumul_msg_size,
		max_storage_size -minor->cumul_msg_size);
	return msg->len;
}
/*
 * @minor: ddstate relative to session opened to a specific devFile
 * @notify_all: if !=0 notify all interruptible threads on the  waitQueue
 * 	otherwise notify just topmost waiting thread
 * @event: either %MSGREADY %FLUSH, will 'or' with topmost delayed reader awake_cond
 * 	notify a pending reader,if exist, blocked for a msg with the given event
 * @sess: if not NULL, will be used to limit readers to awake to ones related to
 * 	session at @sess. only if notify_all !0 --for clean close--
 * NOTE: ddstate (minor devF lock) should be taken
 */
static void _notify_reader(ddstate* minor,char notify_all,char event,session* sess){
	delayed_read* defered_rd;
	if (notify_all){	//wake up all pending rd
		//notify just session's readers --for clean close--
		if(sess){	
			list_for_each_entry(defered_rd,&minor->readers_delayed,link){
				if (defered_rd->sess == sess)
					defered_rd->awake_cond |= event;
			wake_up_interruptible_all(&minor->waitq_readers);
			return;
		}
		//notify all devF waiting readers
		list_for_each_entry(defered_rd,&minor->readers_delayed,link)
			defered_rd->awake_cond |= event;
		wake_up_interruptible_all(&minor->waitq_readers);
		return;
		}
	}
	//wakeup just the topmost pending rd
	if(!(defered_rd = list_first_entry_or_null(&minor->readers_delayed,delayed_read,link))){
		AUDIT printk(KERN_INFO "%s: no readers waiting for msgs",MODNAME);
		return;
	}
	//TODO UNLINK DONE BY THE DEFERED READER;  list_del(&defered_rd->link);	//unlink the delayed reader to unlock
	defered_rd->awake_cond |= event;
	//wakeup 1 wake-one or wake-many thread, waiting for the event 
	wake_up_interruptible(&minor->waitq_readers); 
	return;
}

/*
 * posticipated write
 * @work:contained as: work_struct -> delayed_work -> delayed_write (this module)
 */
static void _delayed_write (struct work_struct *work){
	int ret;
	struct delayed_work *delayed_work;
	message* trgt_msg;
	delayed_write* pending_wr;
	//delayed_work struct in kernel 5.8.12 at: linux/workqueue.h + 116
	delayed_work = container_of(work, struct delayed_work, work); 
	pending_wr= container_of(delayed_work, delayed_write, delayed_work);
	trgt_msg = pending_wr -> msg;
	ret = _add_msg(pending_wr->minor,trgt_msg,MSGREADY);
	if (ret > 0)		return ;	//successful write
	//fail
	AUDIT printk(KERN_ERR "%s: delayed write failed with err:%d\n",MODNAME,ret);
	kfree(trgt_msg->data);
	kfree(trgt_msg);
	kfree(delayed_work);
	return;
}

/*
 * @file:	calling IO sess
 * @buff:	msg contento to write
 * @len:	msg len
 * @off:	unsed
 *
 * Returs:
 * 	the number of bytes readed, 
 *	if requestest less bytes then readed message size,
 *		the remaining is discarded along with the message.
 *	%-ENOMEM -> some control struct alloc failed
 *	%-ENOMSG -> no message avaible 
 *	%-ETIME ->  no message avaible after the max ammount of wait time
 *	%-ECANCELED -> flush called while waiting for a new message
 *
 * check if there is an avaible message, if so dequeue it and copy to @buff
 * otherwise, if the session op.mode allow to wait, go to sleep for the setted time
 * waiting for a new msg.
 */
ssize_t _read(struct file *file, char __user *buff, size_t len, loff_t *off) {
	session*	sess=file -> private_data;
	ddstate* 	minor=minors + get_minor(file);
	message* 	msg;
	unsigned long   max_wait_time;
	delayed_read*	pending_rd=NULL;
	int ret,left=0;
	DEBUG printk(KERN_INFO "%s: read on devFile with minor %d of %ld bytes \n",
		MODNAME,get_minor(file),len);
	mutex_lock(&minor->mtx);
	msg=list_first_entry_or_null(&minor->avaible_messages,message,link);
	if (msg)	goto msg_ready;
	
	mutex_unlock(&minor->mtx);
	///NO MSG AVAIBLE -> check session op.mode allow to wait
	max_wait_time=sess->timeoutRd;
	AUDIT	printk(KERN_INFO "%s: NO avaible msg...\
		 op.mode allow to wait up to %lu jiffies\n",MODNAME,max_wait_time);
	if (!max_wait_time)	return -ENOMSG; //no wait allowed -> return
	
	//init pending read op.
	if( !(pending_rd=kzalloc(sizeof(delayed_read),GFP_KERNEL))) {
		printk(KERN_ERR "%s: failed to alloc the pending read",MODNAME);
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&pending_rd->link);
	//enqueue the waiting rd op
	mutex_lock(&minor->mtx);
	list_add_tail(&pending_rd->link,&minor->readers_delayed);
	mutex_unlock(&minor->mtx);
	//until am I allowed to wait a new msg
	while(max_wait_time>0){
	//sleep until either: msgready,flush,signal,timeout	
	//TODO DISALLOW SIGNALS?
	ret=wait_event_interruptible_timeout(minor->waitq_readers,
			pending_rd->awake_cond,sess->timeoutRd);	
	if(ret==0){					//timeout expired not event
		AUDIT printk(KERN_INFO "%s:timeout waiting for msg",MODNAME);
		ret=-ETIME;
		goto unlink_pending_rd;
	}
	else if(ret==-ERESTARTSYS){	//signal
		AUDIT printk(KERN_INFO "%s:sig interruption",MODNAME);
		ret=-ENOMSG;
		goto unlink_pending_rd;	//TODO POSSIBILE UNLINK GIA FATTO?
	}
	else if(ret==1){			//event and expired
		AUDIT printk(KERN_INFO "%s:awake_cond after timeout",MODNAME);
		//if msg will be \"stealed\" from another session 
		//	-> no more time to wait
		max_wait_time=0;
		ret=-ETIME;
	}
	else { 						//event and timer not expired 
		AUDIT printk(KERN_INFO "%s:awake_cond %x with residuajiffies=%d",
			MODNAME,pending_rd->awake_cond,ret);
		max_wait_time=ret;	
	}
	
	if (pending_rd->awake_cond & FLUSH){	//called _flush()
		AUDIT printk(KERN_INFO "%s: flush() called",MODNAME);
		return -ECANCELED;	//unlink - free in flush()
	}
	//msg ready
	DEBUG printk(KERN_INFO "%s: MSGREADY, awake_cond=>%x=?=%x",
		MODNAME,pending_rd->awake_cond,MSGREADY);	//TODO DEBUG
	
	mutex_lock(&minor->mtx);
	if((msg=list_first_entry_or_null(&minor->avaible_messages,message,link))){
			//msg avaible => unlink-free pending_rd
			list_del(&pending_rd->link);
			kfree(pending_rd);
			goto msg_ready;
		}
		//avaible msg already taken from another sess -> back to sleep
		mutex_unlock(&minor->mtx);
		DEBUG printk(KERN_INFO "%s: avaible msg already taken",MODNAME);
	}
	//if exited without a goto -> timeout in earlier iteration but no msg
	
	unlink_pending_rd:
		mutex_lock(&minor->mtx);
		list_del(&pending_rd->link);
		mutex_unlock(&minor->mtx);
	free_pending_rd:
		kfree(pending_rd);
		return ret;

	msg_ready:
		DEBUG printk(KERN_INFO "%s: msg ready=%s len=%lu. Unlinking it\n",MODNAME,
			msg->data,msg->len);

		list_del(&msg->link);		//unlink the msg
		mutex_unlock(&minor->mtx);
		if(len > msg->len)	len=msg->len;
		else if (len < msg->len)	
			AUDIT printk(KERN_INFO "%s: not requested %lu bytes \
			    of the msg. Discarding them...",MODNAME,msg->len - len);

		if((left=copy_to_user(buff,msg->data,len)))
			AUDIT printk(KERN_INFO "%s: copy_to_user not copied %d bytes",
				MODNAME,left);
		//TODO reinsert the msg with hope of another read fully sucesfull ?
		

		//free copied msg
		kfree(msg->data);
		kfree(msg);
		return len-left;	//TODO UNCOPIABLE DISCARD ?
}

/*
 * @file: IOsession that called flush() 
 * @id:   unused
 * revoke pending messages and unblock waiting readers on the whole dev file
 * NOTE: if file's session has limit_flush set to non-zero, the flush will be
 * limitated to calling session's readers and writers delayed msgs --clean close--
 */
int	_flush (struct file* file, fl_owner_t id){

	ddstate* minor=minors + get_minor(file);
	session* sess_tmp;
	session* sess=file->private_data;
	delayed_read* defered_rd;
	unsigned int c=0;	//audit counter
	DEBUG printk(KERN_INFO "%s: flush on devFile with minor %d %s\n",
		MODNAME,get_minor(file),sess->limit_flush?"limited to this session":"");
	//revoke defered writes
	list_for_each_entry(sess_tmp,&minor->sessions,link){
		if (sess->limit_flush && sess_tmp != sess ) continue;//if set,limit flush
		mutex_lock(&sess_tmp->mtx);//serialize with other sessions's deferred wr
		_cancel_pending_wr(sess_tmp);
		mutex_unlock(&sess_tmp->mtx);
		AUDIT c++;
	}
	AUDIT printk(KERN_INFO "%s: flush- deleyed messages revoked in %d IOsessions"
		,MODNAME,c);

	AUDIT c=0;
	mutex_lock(&minor->mtx);	//serialize with other session's defered rd
	//unblock waiting readers
	sess_tmp=NULL;							//awake all devF waiting readers
	if(sess->limit_flush)	sess_tmp=sess;	//awake just session's readers
	_notify_reader(minor,1,FLUSH,sess_tmp);
	//unlink - free pending rd
	list_for_each_entry(defered_rd,&minor->readers_delayed,link){
		if (sess->limit_flush && defered_rd->sess!=sess ) continue;
		list_del(&defered_rd->link);
		kfree(defered_rd);
		AUDIT c++;
	}
	mutex_unlock(&minor->mtx);
	AUDIT if(c)	printk(KERN_INFO "%s: flush- unblocked %d readers",MODNAME,c);

	return 0;
}


/*
 * @file: IOSession on which ioctl has been called
 * @cmd:  ioctl cmd requested to modify operative mode of @file->operations
 * 	  either:%SET_SEND_TIMEOUT %SET_RECV_TIMEOUT %REVOKE_DELAYED_MESSAGES %LIMIT_FLUSH_SESS_TOGGLE
 * @arg:  RD-WR operations timeout for %SET_SEND_TIMEOUT %SET_RECV_TIMEOUT cmds in milliseconds
 *
 * Return: 0 on success, %-ENOTTY on invalid cmd given
 *
 * it's possible to change read max wait time and write defer time with
 * respectivelly %SET_SEND_TIMEOUT and %SET_RECV_TIMEOUT 
 * delayed messages can be revoked with %REVOKE_DELAYED_MESSAGES
 */
long _unlocked_ioctl(struct file* file, unsigned int cmd, unsigned long arg){
	session* sess = file -> private_data;
	unsigned long timeout;
	AUDIT printk(KERN_INFO "%s: ioctl[%u]:%lu on devFile with minor %d\n",
		MODNAME,cmd,arg,get_minor(file));
	if(cmd != REVOKE_DELAYED_MESSAGES){	//get timeout to set
#ifdef TIMEOUT_DEF_MILLIS
		timeout=millis_2_jiffies(arg);
#else		//timeout expressed in jiffies directly
		timeout=arg;
#endif
	}
	switch(cmd){
		case SET_SEND_TIMEOUT:
			sess->timeoutWr=timeout;
			break;
		case SET_RECV_TIMEOUT:
			sess->timeoutRd=timeout;
			break;
		case REVOKE_DELAYED_MESSAGES:
			_cancel_pending_wr(sess);
			break;
		case LIMIT_FLUSH_SESS_TOGGLE:
			sess->limit_flush=!sess->limit_flush;
			break;
		default:
			printk(KERN_ERR "%s: _unlocked_ioctl ->invalid cmd %d given\n",MODNAME,cmd);
			return -ENOTTY;
			break;
	}
	return 0;	
}

/*
 * try to cancel all defered work in the given @sess session
 * the operation not already started will be cancelled and related struct freed
 */
static void _cancel_pending_wr(session* sess){
	delayed_write* 	defered_wr;
	unsigned int	defered_ops=0,already_started_ops=0; //audit counters
	list_for_each_entry(defered_wr,&sess->writers_delayed,link){
		if(cancel_delayed_work(&defered_wr->delayed_work)){
			list_del(&defered_wr->link);
			kfree(defered_wr->msg->data);
			kfree(defered_wr->msg);
			kfree(defered_wr);
		}
		else	already_started_ops++;
		defered_ops++;
	}
	AUDIT{
		if (defered_ops - already_started_ops) printk(KERN_INFO "%s: cancelled %d\
				defered writes",MODNAME,defered_ops-already_started_ops);
		if(already_started_ops) printk(KERN_INFO "%s:Impossible to cancel %d \
			ones, because were already started",MODNAME,already_started_ops);
	}
}

