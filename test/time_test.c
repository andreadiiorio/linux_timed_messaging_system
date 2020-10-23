/*
 *
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
 * 
 * 	 bind 2 threads to respectivelly write and read from one device file 
 * 	 using delays and checking time to exchange each message using barriers
 * 	 flexible time gathering with GETTIME(arg,out) macro: out=get_time_with(&arg)
 * 	 
 * 	 define GETTIMEOFDAY in compilation to expand implement GETTIME with gettimeofday 
 * 	 otherwise will be implemented with __rdtscp, scaling the tsc counter with ksym tsc_khz
 * 	 in that case, the reader and the writer will be bounded to specific different cores
 * 	 to avoid use different TSC HW during a time mesurement
 *
 */

//sched_setaffinity
#define _GNU_SOURCE
#include <sched.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <sys/ioctl.h>

#include "../include/mod_ioctl.h" //module ioctl cmds MACROs

#define DRNG_DEVFILE "/dev/urandom"
#define MOD_DEVFILE  "../timed_msg_sys_1"
//rnd str
#define RND_LONG_NUM			5	//num of 8bytes of rnd data readed
#define MSGSIZE 				96
#define XSTR(s) STR(s)
#define STR(s) #s
//each message is prefixed with a progressive number and a tab
#define HEADER_LEN				5	//progres. num prefix to each message len (just num)
#define HEADER_FRMT_STR			"%" XSTR(HEADER_LEN) "u" 

//timer either with gettimeofday or __rdtscp, with common calling function GETTIME
#define MICRO	1000000
#ifdef GETTIMEOFDAY 
	#include <sys/time.h>
	#define GETTIME(arg,out)	\
		do{ \
			gettimeofday(&arg, NULL);	\
			out=arg.tv_usec + arg.tv_sec * MICRO; \
		}while(0)
#else //TSC
	//cross platform rdtscp's header
	#ifdef _MSC_VER //MS compiler vers --> Win
		#include <intrin.h> /* for rdtscp and clflush */
		#pragma optimize("gt",on)
	#else //linux
		#include <x86intrin.h> /* for rdtscp and clflush */
	#endif
	
	#define GETTIME(arg,out)	out=__rdtscp(&arg)
	//scale tsc -> u_sec: tsc_khz/1000 (~tsc_Mhz)
	#ifndef TSC_KHZ //see Makefile
		#define TSC_KHZ		2592044 //gdb /dev/null /proc/kcore -ex 'x/uw 0x'$(grep '\<tsc_khz\>' /proc/kallsyms | cut -d' ' -f1) -batch 2>/dev/null | tail -n 1 | cut -f2
	#endif
	#define USEC_SCALE	(TSC_KHZ/1000)
#endif

#define handle_error(errCode, msg) do{ errno=errCode; perror(msg);} while(0);
#define TIMEOUT_MIN_MIL 50
#define TIMEOUT_MAX_MIL 100
#define ITERATIONS		5	//num of read-write cicles
#ifndef QUIET
	#define AUDIT 	if(1)
#else
	#define AUDIT 	if(0)
#endif

int urndFp;	//file descriptor to DRNG_DEVFILE O_RDONLY opened
pthread_barrier_t barrier;
unsigned long int timeout;

static int _read_wrap(int fd,char* dst,size_t count);
static void* thread_work(void* arg);
static int rndNumericStr(char* msg,unsigned prefix);

int main(int argc,char** argv){
		unsigned x;

		/*
		 * first thread will have the writer role (!0)
		 * also the cpuId to bind to if not defined GETTIMEOFDAY -> TSC
		 */
		long th_arg=5,ret=0; 
		char* ptr;
		//rnd devF open
		if((urndFp=open(DRNG_DEVFILE,O_RDONLY))<0){
				perror("open DRNG_DEVFILE");
				exit(EXIT_FAILURE);
		}
		////timeout given in argv
		if(argc == 2){
			timeout=strtoul(argv[1],&ptr,10);
			if (ptr==argv[1] || timeout == ULONG_MAX ){
				perror("strtol errd");
				exit(EXIT_FAILURE);
			}
			goto start;
		}
		//rnd timeout
		if(_read_wrap(urndFp,(char*)&timeout,sizeof(timeout))<0){
			fprintf(stderr,"rnd read timeout failed");
			exit(EXIT_FAILURE);
		}
		timeout=TIMEOUT_MIN_MIL+(timeout % (TIMEOUT_MAX_MIL - TIMEOUT_MIN_MIL));
		printf("selected timeout: %lu\n",timeout);
		start:
		if((ret=pthread_barrier_init(&barrier,NULL,2))){
			handle_error(ret,"pthread_barrier_init");
			exit(EXIT_FAILURE);
		}

		///start threads
		pthread_t threads[2];
		for (x=0; x<2; x++,th_arg=!th_arg){//first thread will have !0 arg
			if((ret=pthread_create(threads+x,NULL,thread_work,(void*)th_arg))<0){
				handle_error(ret,"pthread_create");
				break;
			}
		}
		
		///join threads
		long th_ret;
		for (x=0; x<2; x++){
			if((ret=pthread_join(threads[x],(void**) &th_ret)))
				handle_error(ret,"pthread_join");
			printf("thread[%d] returned %ld\n",x,th_ret);
		}
		if((ret=pthread_barrier_destroy(&barrier))){
			handle_error(ret,"pthread_barrier_init");
			exit(EXIT_FAILURE);
		}

		return ret;
}

/*
 * read or write from the device with the given timeout
 * the given arg, indicate if the thread will be the reader or the writer
 */
static void* thread_work(void* arg){
	long role=(long) arg,ret,wr,rd,noMsgCounter=0;
	int fd,len,open_flag;
	char msg[MSGSIZE];
	//timer stuff
	unsigned long timeStart,timeEnd;
	long double elapsed;
#ifdef GETTIMEOFDAY
	struct timeval _arg; 
#else //rtdsc
	//set sched affinity for this thread to given ID -> avoid to use different TSC HW
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(role,&set);
	unsigned int _arg; //will hold IA32_TSC_AUX used in __rdtscp
	
	if(pthread_setaffinity_np(pthread_self(),sizeof(set),&set) == -1){
		perror("sched_setaffinity");
		exit(EXIT_FAILURE);
	}
#endif
	//open mod devF
	open_flag=O_RDONLY;
	if(role)	open_flag=O_WRONLY;
	if((fd=open(MOD_DEVFILE,open_flag))<0){
			perror("open MOD_DEVFILE");
			return (void*) EXIT_FAILURE;
	}
	//set timeouts  
	if(role){
		if((ret=ioctl(fd,SET_SEND_TIMEOUT,timeout))<0){
			perror("ioctl SET_SEND_TIMEOUT");
			goto end;
		}
	}
	else{
		if((ret=ioctl(fd,SET_RECV_TIMEOUT,2*timeout))<0){
			perror("ioctl SET_RECV_TIMEOUT");
			goto end;
		}
	}
	//send & receive messages mesuring time
	for (unsigned x=0;x<ITERATIONS;++x){
		if(role){ //the writer prepare the next msg
			if((ret=rndNumericStr(msg,x))==-1) goto end;	
			len=ret;
		}
		//send - recv together
		ret=pthread_barrier_wait(&barrier);
		if( ret && ret != PTHREAD_BARRIER_SERIAL_THREAD ){
			handle_error(ret,"pthread_barrier_wait");
			goto end;
		}
		if(role){	//writer role
			GETTIME(_arg,timeStart);
			if( (wr=write(fd,msg,len))<0 ){
				fprintf(stderr,"WRITE TO FD %d FAILED\n",fd);
				perror("write");
				ret=wr;
				goto end;
			}
			GETTIME(_arg,timeEnd);
		}
		else{		//reader role
			GETTIME(_arg,timeStart);
			do{
				if( (rd=read(fd,msg,MSGSIZE))<0 ){
					AUDIT{
						fprintf(stderr,"READ FROM THE FD %d FAILED\n",fd);
						perror("read");
					}
					noMsgCounter++;
					//no msg may be ready later	
					if (errno != ENOMSG && errno != ETIME){
						perror("read");
						ret=rd;
						goto end;
					}
				}
			} while(rd <=0 ); //retry until a msg is received
			GETTIME(_arg,timeEnd);
		}
		elapsed=(timeEnd-timeStart)/(long double) MICRO;
#ifndef GETTIMEOFDAY
		elapsed/=USEC_SCALE; 
#endif
		fflush(stdout);
		printf("elapsed %lu unit=%Lf secs.%s",
			timeEnd-timeStart,elapsed,role?"Writer\n":"Reader\n");
		if(!role){
			AUDIT 	printf(">>msg\t%s\t%ldB\n",msg,rd); //readed message
			if (noMsgCounter) printf("missed read:%ld\n",noMsgCounter);
		}
		fflush(stdout);
		noMsgCounter=0;
	}
	//end together 
	ret=pthread_barrier_wait(&barrier);
	if( ret && ret != PTHREAD_BARRIER_SERIAL_THREAD ){
		handle_error(ret,"pthread_barrier_wait");
		goto end;
	}
	end:
	if((ret=ioctl(fd,LIMIT_FLUSH_SESS_TOGGLE))<0)	
		perror("ioctl LIMIT_FLUSH_SESS_TOGGLE");
	close(fd);
	return (void*) ret;
}
//write a random numeric str in msg (point to MSGSIZE B allocated ) with prefix num written
//return written bytes or -1 in error
static int rndNumericStr(char* msg,unsigned prefix){
	int written=0,wr,x;
	//get rnd msg data
	unsigned long rndData[RND_LONG_NUM]; //filled with random data
	if(_read_wrap(urndFp,(char*) &rndData,sizeof(rndData))<0){
		fprintf(stderr,"rnd read for msg data at msg :%u fail\n",prefix);
		return -1;
	}	
	
	//get rnd data 8bytes at times and convert to string
	for(x=0; x<RND_LONG_NUM && written+1 < MSGSIZE; ++x){
		wr=snprintf(msg + written, MSGSIZE - written,"%lu",rndData[x]);
		if(wr < 0){
				fprintf(stderr,"snprintf failed with %d \n",written);
				return -1;
		}
		if( wr > MSGSIZE - written -1 ){
				AUDIT printf("msg num:%u truncated\n",prefix);
				wr=MSGSIZE - written -1; //add the terminator in the normal case
		}
		written+=wr;
	}
	//add HEADER_LEN chars progressive header
	if(snprintf(msg,HEADER_LEN+1,HEADER_FRMT_STR,prefix) != HEADER_LEN ){
			fprintf(stderr,"msg prefix error\n");
			return -1;
	}
	msg[HEADER_LEN]='\t';
	AUDIT printf("<<msg\t%s\t%uB\n",msg,written+1);
	return ++written; //consider '\0' 
}
static int _read_wrap(int fd,char* dst,size_t count){
	ssize_t rd;
	size_t readed=0;
	while (readed < count){
		rd=read(fd,dst+readed,count-readed);
		if (rd<0){
			perror("read");
			return rd;
		}
		readed+=rd;
	}
	return 0;
}
