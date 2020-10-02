/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 *
 *  Core of module -> implementation of core d.driver api
 *
 */

#include "../include/timed_messaging_sys.h"
#include "../include/core.h"

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
	int i,err=0;
	for (i=0;i<NUM_MINOR;i++){
		ddstate* dMinor=minors + i;
		//TODO not unmountable module if exist a session?
		session* sess=list_first_entry_or_null(&dMinor->sessions);
		if (sess) printk(KERN_ERR "%s: unmounting module with a session alive\n"
					MODNAME);

		//delete unreaded messages
		message* msg;
		list_for_each_entry(msg,&minor->avaible_messages,link){
			list_del(&msg->link);
			kfree(msg->data);
			kfree(msg);
		
		}
	}
	return err;
	
}
int _open(struct inode *inode, struct file *file) {
	ddstate* minor=minors + get_minor(file);
	session* sess;
	if (!(sess = kmalloc(sizeof(session),GFP_KERNEL)))	return -ENOMEM;

	//init session fields
	mutex_init(&sess->mtx);
	//non-predefined (events), bound workQueue, 
	//default=256 exe contexts per cpu assigniable to work items
	//	mem pressure -> >=1 exe context for sure
	unsigned int wq_flags=WQ_MEM_RECLAIM;
	#ifdef DELAYED_WRITER_HIGH_PRIO
	wq_flags |= WQ_HIGHPRI;
	#endif
	if(!(sess->workq_writers = alloc_workqueue(WRITERS_WORKQ,wq_flags,0))){
		kfree(sess);
		printk(KERN_ERR "%s: writers workqueue alloc error\n");
		return -ENOMEM;
	}
	sess->timeoutRd=sess->timeoutWr=0;	//dflt timeout
	INIT_LIST_HEAD(&sess->writers_delayed);
	INIT_LIST_HEAD(&sess->link);
	// double link session <-> ddstate (per minor)
	mutex_lock(&minor -> mtx);
	list_add_tail(&sess->link,&minor->sessions);
	mutex_unlock(&minor -> mtx);
	file -> private_data = (void*) sess;
	return 0;
}

int _release(struct inode *inode, struct file *file) {
	ddstate* minor=minors + get_minor(file);
	session* sess =(session*) file -> private_data;

	//wait deleyed write to complete
	flush_workqueue(&session -> workq_writers);
	destroy_workqueue(&session->workq_writers);
	//unlinks
	mutex_lock(&minor -> mtx);
	list_del(&sess->link);
	mutex_unlock(&minor -> mtx);
	
	kfree(sess);
	return 0;
}

int _is_dev_file_full(ddstate* minor,unsigned long len_toadd){
	if (minor->cumul_msg_size + len_toadd <= max_storage_size )	return 0;
	printk(KERN_ERR "%s: exceeded max_storage_size=%lu,discarding msg of %lu bytes",
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
 * 	-%ENOMEM
 * after msg preparation, evaluate if the curr op.mode require to defer the write
 * if so,enqueue the operation to the custom workqueue as a work item
 * otherwise,safelly enqueue the new message to the avaible ones, 
 * notifing blocked readers for the new msg [if any for it ]
 */
ssize_t _write(struct file *file, const char *buff, size_t len, loff_t *off) {
	session*	sess=file -> private_data;
	ddstate* 	minor=minors + get_minor(file);
	message* 	msg;
	char*		kbuff;
	delayed_write* pending_wr=NULL; //NULL to distinguish in err dealloc 
	int		ret;
	
	//get the user message
	if (!(msg = kmalloc(sizeof(msg),GFP_KERNEL))){
		printk(KERN_ERR "%s: msg alloc failed",MODNAME);
		return -ENOMEM;	
	}
	//allocate a tmp buff to copy the user message with contiguos mem if
	//possible, otherwise with non contiguos, using dflt numa node
	if (!(kbuff = kvmalloc_node(len,GFP_KERNEL,NUMA_NO_NODE))){
		printk(KERN_ERR "%s: kbuff alloc failed",MODNAME);
		ret = -ENOMEM;
		goto free_msg;
	}
	if (ret=copy_from_user(kbuff,buff,len)){
		printk(KERN_ERR "%s: copy_from_user left %d bytes",MODNAME,ret);
		ret = -ENOMEM;
		goto free_kbuff	;
	}
	//prepare the message
	msg->data=kbuff;
	msg->len=len;

	//Check if ddriver IO sess operative mode require to defer the write
	if (sess ->timeoutWr){	
		//deleay the write
		if (!(pending_wr = kmalloc(sizeof(delayed_write),GFP_KERNEL))){
			printk(KERN_ERR "%s: error alloc delayed_write");
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
		if(!mutex_trylock(sess->mtx)){
			AUDIT printk(KERN_INFO "%s: deferred write while flushing the device,
				 aborting",MODNAME);	
			goto free_pending_wr;
		}
		list_add_tail(&pending_wr->link,&sess->writers_delayed);
		mutex_unlock(sess->mtx);
#else						      //defered write delayed if concurr with flush 
		mutex_lock(sess->mtx);				
		list_add_tail(&pending_wr->link,&sess->writers_delayed);
		mutex_unlock(sess->mtx);
#endif
		//queue work item in the custom  WRITERS_WORKQ
		INIT_DELAYED_WORK(&pending_wr->delayed_work,_delayed_write);
		queue_delayed_work(sess->writers_delayed,pending_wr->delayed_work,
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

/*
 * @minor: ddstate relative to session opened to a specific devFile
 * @msg:   message to add
 * @event: event to propagate to blocked readers waiting [if any]
 * 		   if = %NULLEVENT, readers not notified
 * will be notified the topmost blocked reader, waiting for a msg
 * Return: len of added message or %-ENOSPC if the device is full
 */
static int _add_msg(ddstate* minor, message* msg,char event){
	INIT_LIST_HEAD(&msg->link);
	mutex_lock(minor->mtx);
	if ( _is_dev_file_full(msg->len) ){
		return -ENOSPC;	
		mutex_unlock(minor->mtx);
	}
	list_add_tail(msg,&minor->avaible_messages);
	minor->cumul_msg_size+=msg->len;
	if (event != NULLEVENT)		_notify_reader(minor,0,event);
	mutex_unlock(minor->mtx);
	return msg->len;
}
/*
 * @minor: ddstate relative to session opened to a specific devFile
 * @notify_all: if !=0 notify all interruptible threads on the  waitQueue
 * 				otherwise notify just topmost waiting thread
 * @event: either %MSGREADY %FLUSH, will 'or' with topmost delayed reader awake_cond
 * notify a pending reader,if exist, blocked for a msg with the given event
 * 
 * NOTE: ddstate (minor devF lock) should be taken
 */
static void _notify_reader(ddstate* minor,char notify_all,char event){
	delayed_read* defered_rd;
	if (notify_all){	//wake up all pending rd
		list_for_each_entry(defered_rd,&minor->readers_delayed,link)
			defered_rd->awake_cond |= event;
		wake_up_interruptible_all(&minor->waitq_readers);
		return;
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
void _delayed_write (struct work_struct *work){
	int ret;
	struct delayed_work *delayed_work;
	message* trgt_msg;
	delayed_write* pending_wr;
	//delayed_work struct in kernel 5.8.12 at: linux/workqueue.h + 116
	delayed_work = container_of(work_struct, struct delayed_work, work); 
	pending_wr= container_of(delayed_work, delayed_write, delayed_work);
	trgt_msg = pending_wr -> msg;
	ret = _add_msg(&minor,trgt_msg,MSGREADY);
	if (ret > 0)		return ret;	//successful write
	
	kfree(msg->data);
	kfree(msg);
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
 *	if requestest less bytes then readed message size, the remaining is discarded along with the message.
 *	-%ENOMEM
 *
 * check if there is an avaible message, if so dequeue it and copy to @buff
 * otherwise, if the session op.mode allow to wait, go to sleep for the setted time
 * waiting for a new msg.
 */
ssize_t _read(struct file *file, const char *buff, size_t len, loff_t *off) {
	session*	sess=file -> private_data;
	ddstate* 	minor=minors + get_minor(file);
	message* 	msg;
	unsigned long   max_wait_time;
	delayed_read*	pending_rd=NULL;
	int ret,left=0;
	mutex_lock(&minor->mtx);
	msg=list_first_entry_or_null(&minor->avaible_messages,message,link);
	if (msg)	goto msg_ready;

	mutex_unlock(&minor>-mtx);
	///NO MSG AVAIBLE -> check session op.mode allow to wait
	max_wait_time=sess->timeoutRd;
	AUDIT	printk(KERN_INFO "%s:NO avaible msg...
		     op.mode allow to wait %lu jiffies\n",MODNAME,max_wait_time);
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
		ret=wait_event_interruptible_timeout(&minor->waitq_readers,
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
			//if msg will be "stealed" from another session 
			//	-> no more time to wait
			max_wait_time=0;
			ret=-ETIME;
		}
		else { 						//event and not expired timer
			AUDIT printk(KERN_INFO "%s:awake_cond %xwith residuajiffies=%lu",
				MODNAME,pending_rd->awake_cond,ret);
			max_wait_time=ret;	
		}

		if (pending_rd->awake_cond & FLUSH){	//called _flush()
			AUDIT printk(KERN_INFO "%a: flush() called",MODNAME);
			return -ECANCELED;	//unlink - free done in flush()
		}
		//msg ready
		AUDIT printk(KERN_INFO "%s: DBG- MSGREADY=>%d==%d",
			MODNAME,pending_rd->awake_cond,MSGREADY);	//TODO DEBUG
		
		mutex_lock(&minor->mtx);
		if(msg=list_first_entry_or_null(&minor->avaible_messages,message,link)){
			//msg avaible => unlink-free pending_rd
			list_del(&pending_rd->link);
			kfree(pending_rd);
			goto msg_ready;
		}
		//avaible msg already taken from another sess -> back to sleep
		mutex_unlock(&minor>-mtx);
		AUDIT printk(KERN_INFO "%s: avaible msg already taken",MODNAME);
	}
	//if exited without a goto -> timeout in earlier iteration but no msg
	}
	unlink_pending_rd:
		mutex_lock(&minor->mtx);
		list_del(&pending_rd->link);
		mutex_unlock(&minor->mtx);
	free_pending_rd:
		kfree(pending_rd)
		return ret;

	msg_ready:
		list_del(&msg->link);		//unlink the msg
		mutex_unlock(&minor->mtx);
		if(len > msg->size)	len=msg->size
		elif(len < msg->size)	
			AUDIT printk(KERN_INFO "%s: discarding %lu bytes 
			    of the msg not requested ",MODNAME,msg->size - len);
		if(left=copy_to_user(msg->data,buff,len))
			AUDIT printk(KERN_INFO "%s: not copied %lu bytes 
						of the request",MODNAME,left);
		//TODO reinsert the msg with hope of another read fully sucesfull ?
		

		//free copied msg
		kfree(msg->data);
		kfree(msg);
		return len-left;
}

/*
 * @file: IOsession that called flush() 
 * @id:   unused
 * revoke pending messages and unblock waiting readers 
 */
int	_flush (struct file* file, fl_owner_t id){
	ddstate* minor=minors + get_minor(file);
	session* sess;
	unsigned int c=0;	//audit counter
	//revoke defered writes
	list_for_each_entry(sess,&minor->sessions,link){
		mutex_lock(&sess->mtx);	//serialize with other sessions's new deferred wr
		_cancel_pending_wr(sess);
		mutex_unlock(&sess->mtx);
		AUDIT c++;
	}
	AUDIT printk(KERN_INFO "%s: flush- deleyed messages revoked in %d IOsessions"
		,MODNAME,c);

	c=0;
	mutex_lock(minor->mtx);	//serialize with other session's defered rd
	//unblock waiting readers
	_notify_reader(minor,1,FLUSH);
	//unlink - free pending rd
	delayed_read* defered_rd;
	list_for_each_entry(defered_rd,&minor->readers_delayed,link){
		list_del(&defered_rd->link);
		kfree(defered_rd);
		AUDIT c++;
	}
	mutex_unlock(minor->mtx);
	AUDIT printk(KERN_INFO "%s:flush- unblocked %d readers",MODNAME,c);

	return 0;
}


/*
 * @file: IOSession on which ioctl has been called
 * @cmd:  ioctl cmd requested to modify operative mode of @file->operations
 * 	  either:%SET_SEND_TIMEOUT %SET_RECV_TIMEOUT %REVOKE_DELAYED_MESSAGES
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
		default:
			AUDIT printk(KERN_ERR "%s: _unlocked_ioctl ->
				 invalid cmd %d given\n",MODNAME,cmd);
			return -ENOTTY;
			break;
	}
	return 0;	
}

/*
 * try to cancell all defered work in the given @sess 
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
	AUDIT printk(KERN_INFO "%s: cancelled %d defered write, but impossible 
		to cancel %d because were already started",MODNAME,
			defered_ops-already_started_ops,already_started_ops);
}

