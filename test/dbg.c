#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <limits.h>
#include "../include/mod_ioctl.h" //module ioctl cmds MACROs
#define DEVF_PATH "/home/snake/linux_timed_messaging_system/timed_msg_sys_1"
//#define TEST_ALL_IOCTL
int main(){
		int ret=0,fp;
		if((fp=open(DEVF_PATH,O_RDWR))<0)	{perror("open");ret=fp;goto end;}

#ifdef TEST_ALL_IOCTL
		unsigned long timeout=96;
		if((ret=ioctl(fp,SET_SEND_TIMEOUT,timeout))<0)	perror("ioctl SND"); 
		if((ret=ioctl(fp,SET_RECV_TIMEOUT,timeout))<0)	perror("ioctl RCV"); 
		if((ret=ioctl(fp,REVOKE_DELAYED_MESSAGES))<0)	perror("ioctl RVK"); 
		if((ret=ioctl(fp,LIMIT_FLUSH_SESS_TOGGLE))<0)	perror("ioctl LIM"); 
		if((ret=ioctl(fp,FLUSH))<0)						perror("ioctl FLS"); 	
#endif
		//print all messages from all dev files (if compiled with DEBUG )
		if((ret=ioctl(fp,96))<0)			perror("ioctl"); 
		//del all messages
		if((ret=ioctl(fp,DEL_STORED_MESSAGES,1))<0) perror("ioctl");
		end:
		if(!ret){//no error untl here
			if((ret=system("dmesg"))==-1) perror("system");
		}
		return ret;
}
