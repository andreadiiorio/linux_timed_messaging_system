/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 */

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


//config
#define MAX_MINOR_NUM			33 //max supported minors
#define MIN_MSG_LEN				33 //min bytes per message
#define MAX_SUFFIX_LEN			5	//max chars per device file suffix
#define MAX_PATH_LEN			256
#define MAX_MSG_PER_ITERATION	6	//upper bound of num of msg to read/write 
#define RND_LONG_NUM			5	//num of 8bytes of rnd data readed
#define DRNG_DEVFILE			"/dev/urandom"
//each message is prefixed with a progressive number and a tab
#define HEADER_LEN				5	//progres. num prefix to each message len (just num)
#define HEADER_FRMT_STR			"%" XSTR(HEADER_LEN) "u" 
//support macro
#define AUDIT	if(1)
#define DEBUG	if(1)
#define DEBUG2	if(1)
#define XSTR(s) STR(s)
#define STR(s) #s
#define handle_error(errCode, msg) do{ errno=errCode; perror(msg);} while(0);

#define TIMEOUT_MIN_MIL 7
#define TIMEOUT_MAX_MIL 69
#define MIN_MSG_BEFORE	20		//how many msg send before do REVOKE_DELAYED_MESSAGES

typedef struct _msg {
		char* data;
		unsigned int len;
} message;
typedef struct _thread_args {
		//exchanged msgs by the thread
		unsigned int msgN;    //num msgs to (write) read (onto)from the dev
		message** toWriteMsgs;//msgs that the thread has to write to the devices
		message* readedMsgs;  //msgs readed from the thread from the devices
		//control vars
		char	 flags;		  //inidicate if and what the thread will do an ioctl
		pthread_t thread;	
}	thread_arg;


message* msgs;//random integers formatted as varlen strings \0 terminated 
thread_arg* args; //per thread args, just for DEBUG print th offsets
unsigned long MAX_MSG_LEN=4096;	//max bytes per message
int urndFp;	//file pointer to DRNG_DEVFILE O_RDONLY opened
unsigned long minorsNum;	//selected num of dev files
unsigned long ALL_MSGS_NUM=20;	//all messages to exchange between threads
char** minorPaths;			//dev file path

//helper prototypes
static void _print_msgs(message*,unsigned int);
static int _read_wrap(int fd,char* dst,size_t count);
static void _getDevFilePath(char* dest,char* prefix,size_t prefixLen,long suffix);
static int _initMessages(message**,unsigned int,unsigned int,unsigned int);
static void _freeMsgs(message* ms,unsigned int num);
static void* thread_work(void* arg);
int _msg_rd(int fp,char* dst,unsigned int num);
int _msg_wr(int fp,char* src,unsigned int num);

/*
 *	open associated device files and read-write msgs cyclically on each of them
 *	at each iteration will be selected a random op (0 = WR, !0 = RD) and rnd num
 *	of msgs to RW||WR, subtracted from the num of msgs to exchange by the thread
 *	return (casted) EXIT_SUCCESS on success, on err EXIT_FAILURE
 */
static void* thread_work(void* arg){
	DEBUG sleep(1);
	thread_arg* t_arg= (thread_arg*) arg;
	unsigned long tid=t_arg-args;
	long ret=EXIT_SUCCESS;
	int fp=0,wr,rd,x;
	int minorsFp[MAX_MINOR_NUM];
	memset(minorsFp,0,sizeof(minorsFp));
	char* tmp;
	char enable_revoke=1;
	unsigned char rndOP,rndMsgN;
	unsigned int old,m=0,i;
	///open dev files in minorsFp
	for(i=0;i<minorsNum;++i){
		if( (fp=open(minorPaths[i],O_RDWR)) <0 ){
				perror("open");
				ret=EXIT_FAILURE;
				goto end;
		}
		//DEBUG{fflush(stdout);printf("t_arg[%p]\topened %s -> fd=%d\n",arg,minorPaths[i],fp);fflush(stdout);}
		minorsFp[i]=fp;
	}
	unsigned int writteN=0, toWriteN=t_arg->msgN, toReadN=t_arg->msgN;
	unsigned int* msgBudget;	//point to either toWriteN or toReadN
	message* msgDst=t_arg->readedMsgs;	//hold message to read dst buf
	message** msgSrc=t_arg->toWriteMsgs;//hold message to write ref
	
	////ioctl timeout (used if thread has RD-WR flags set )
	unsigned long timeout;
	if(_read_wrap(urndFp,(char*)&timeout,sizeof(timeout))<0){
		fprintf(stderr,"rnd read for thread's timeout failed");
		ret=EXIT_FAILURE;
		goto end;
	}
	timeout= TIMEOUT_MIN_MIL + (timeout % (TIMEOUT_MAX_MIL - TIMEOUT_MIN_MIL));
	//set ioctl supported timeout operative mode for the last IO session openend
	if(t_arg->flags & SET_RECV_TIMEOUT ){
		if((ret=ioctl(fp,SET_RECV_TIMEOUT,timeout))<0){
			fprintf(stderr,"[%lu] ioctl(%d) SET_RECV_TIMEOUT returned :%ld\n",tid,fp,ret);
			perror("ioctl SET_RECV_TIMEOUT");
			goto end;
		}
		DEBUG printf("[%lu] ioctl SET_RECV_TIMEOUT returned %ld\n",tid,ret);
	}
	if(t_arg->flags & SET_SEND_TIMEOUT){
		if((ret=ioctl(fp,SET_SEND_TIMEOUT,timeout))<0){
			fprintf(stderr,"[%lu] ioctl(%d) SET_SEND_TIMEOUT returned :%ld\n",tid,fp,ret);
			perror("ioctl SET_SEND_TIMEOUT");
			goto end;
		}
		DEBUG printf("[%lu] ioctl SET_SEND_TIMEOUT returned %ld\n",tid,ret);
	}

	//write and read assigned num of mesgs
	while( toWriteN || toReadN ){
		//rndOP
		if(_read_wrap(urndFp,(char*) &rndOP,sizeof(rndOP))<0){
			fprintf(stderr,"rnd read for thread's OP failed");
			ret=EXIT_FAILURE;
			goto end;
		}
		rndOP=rndOP % 2;	// select RD (1) - WR(0)
		//rndMsgN
		if(_read_wrap(urndFp,(char*)&rndMsgN,sizeof(rndMsgN))<0){
			fprintf(stderr,"rnd read for thread's num msgs to exchange failed");
			ret=EXIT_FAILURE;
			goto end;
		}
		rndMsgN= 1+(rndMsgN % (MAX_MSG_PER_ITERATION));

		//decrease msgs num target budget
		msgBudget=&toWriteN;
		if (rndOP)	msgBudget=&toReadN;
		if (*msgBudget<rndMsgN){ //normalize num msg to exchange with trgt budget
			rndMsgN=*msgBudget;
			*msgBudget=0;
		} 
		else	*msgBudget-=rndMsgN;

		//exchange rnd num of msg
		for (x=0; x<rndMsgN; ++x){
			m=(m+1)%minorsNum;
			fp=minorsFp[m];
			if(rndOP){		//READ
				if(!(msgDst->data=malloc(MAX_MSG_LEN))){
						fprintf(stderr,"msg to read data malloc failed\n");
						ret=EXIT_FAILURE;
						goto end;
				}
				msgDst->data[0]='\0';	//avoid valgrind warn 
				if( (rd=read(fp,msgDst->data,MAX_MSG_LEN))<0 ){
					fprintf(stderr,"[%lu] READ FROM THE FD %d  FAILED\n",tid,fp);
					perror("read");
					if (errno == ENOMSG || errno == ETIME){//msg may be ready later
						*msgBudget+=1; 
						AUDIT{
							fflush(stdout);
							printf("[%lu] msg unavaible right now on %d, retries %u\n"
								,tid,fp,*msgBudget);
							fflush(stdout);
							}
						if( sched_yield()<0 )	perror("sched_yield");
						continue;
					}
					ret=errno;
					goto end;
				}
				AUDIT old=msgDst->len;
				msgDst->len=rd;
				//realloc message to the readed bytes
				if(rd>0&&(tmp=realloc(msgDst->data,msgDst->len))) msgDst->data=tmp;
				else if(rd && !tmp){
					AUDIT fprintf(stderr,"[%lu] realloc msg err %d->%dB\n",tid,old,rd);
				}

				DEBUG2{
					fflush(stdout);
					printf("[%lu] rd: %.5s.. %dB on fd %d\n",tid,msgDst->data,rd,fp);
					fflush(stdout);
				}
				msgDst++;
			}
			else{			//WRITE
				if( (wr=write(fp,(*msgSrc)->data,(*msgSrc)->len))<0 ){
					fprintf(stderr,"[%p] WRITE TO FD %d FAILED[%d] \n",arg,fp,errno);
					perror("write");
					//TODO FATAL FILTER
					ret=errno;
					goto end;
				}
				DEBUG2{
					fflush(stdout);
					printf("[%lu] wr: %.5s.. %dB on fd:%d\n",tid,(*msgSrc)->data,wr,fp);
					fflush(stdout);
				}
				msgSrc++;
				writteN++;
			}
			//evaluate to call REVOKE_SENT if thread has the rel. flag
			if( enable_revoke && t_arg -> flags & REVOKE_DELAYED_MESSAGES 
					&& writteN > MIN_MSG_BEFORE ){

				if((ret=ioctl(fp,REVOKE_DELAYED_MESSAGES))<0){
					fprintf(stderr,"[%p] ioctl REVK returned :%ldld\n",arg,ret);
					perror("ioctl REVOKE_DELAYED_MESSAGES");
					goto end;
				}
				DEBUG printf("[%p] ioctl REVK returned %ld\n",arg,ret);
				enable_revoke=0; //disable next invocation
			}
		}
	}
	

	end:
	////close opened devfiles limitating implicit flush to the current session
	for (i=0,fp=minorsFp[0]; i<minorsNum && 0; fp=minorsFp[++i]){ //TODO DISABLE CLOSE
		if(!(fp))	break;	//err in open occurred
		if((ret=ioctl(fp,LIMIT_FLUSH_SESS_TOGGLE))<0){
			perror("ioctl");
			break;
		}
		if( close(fp) < 0){
			perror("close");
			ret=EXIT_FAILURE;
			break;
		}
		DEBUG printf("[%p] closed fp %d\n",arg,minorsFp[i]);
	}
	AUDIT sleep(2); //reduce dbg prints overlapped with join prints
	return (void*) ret;
}

const char* const usage = "usage <devFileBasePath,minorsNum,threadsPerMinorNum,[ALL_MSGS_NUM],[MAX_MSG_LEN]>";
int main(int argc,char** argv){
	unsigned int i,x;
	printf("%lu \n",sizeof(char*));
	//char** c=env;	while(*c)	{printf("%s\n",*c);c++;}	printf("\n\n");
	int ret=EXIT_SUCCESS;
	if(argc<4){
			fprintf(stderr,"%s",usage);
			exit(EXIT_FAILURE);
	}
	char* devFileBasePath=argv[1];	//actual paths obtained adding progressive num
	size_t devFileBasePathLen=strnlen(devFileBasePath,MAX_PATH_LEN);
	//parse decimal strings
	char* ptr;
	minorsNum=strtoul(argv[2],&ptr,10);
	if (ptr==argv[2] || minorsNum== LONG_MIN || minorsNum == LONG_MAX){
			perror("strtol errd");
			exit(EXIT_FAILURE);
	}
	if(minorsNum > MAX_MINOR_NUM){
			fprintf(stderr,"too minors\n");
			exit(EXIT_FAILURE);
	}
	unsigned long threadsPerMinorNum=strtoul(argv[3],&ptr,10);
	if (ptr==argv[3] || threadsPerMinorNum == LONG_MIN || threadsPerMinorNum == LONG_MAX){
			perror("strtol may errd");
			exit(EXIT_FAILURE);
	}
	if(argc==5){	//overwrite ALL_MSGS_NUM
		ALL_MSGS_NUM=strtoul(argv[4],&ptr,10);
		if (ptr==argv[3] || MAX_MSG_LEN== LONG_MIN || MAX_MSG_LEN== LONG_MAX){
				perror("strtol may errd");
				exit(EXIT_FAILURE);
		}
	}
	if(argc==6){	//overwrite MAX_MSG_LEN
		MAX_MSG_LEN=strtoul(argv[5],&ptr,10);
		if (ptr==argv[5] || MAX_MSG_LEN== LONG_MIN || MAX_MSG_LEN== LONG_MAX){
				perror("strtol may errd");
				exit(EXIT_FAILURE);
		}
	}

	unsigned long numThreads=minorsNum * threadsPerMinorNum;
	unsigned long perThreadMessages=ALL_MSGS_NUM / numThreads;
	unsigned long residualMessages =ALL_MSGS_NUM % numThreads; //allocated to last thread
	unsigned long msgN;

	args=calloc(numThreads,sizeof(*args)); //per thread args
	if (!args){
			fprintf(stderr,"threads args malloc failed\n");
			exit(EXIT_FAILURE);
	}
	thread_arg* t_arg; //point to current thread metadata to initiate
	
	if((urndFp=open(DRNG_DEVFILE,O_RDONLY))<0){
			perror("open");
			ret=EXIT_FAILURE;
			goto end;
	}

	////init messages data with random numeric strings taken from DRNG_DEVFILE
	if(_initMessages(&msgs,ALL_MSGS_NUM,MIN_MSG_LEN,MAX_MSG_LEN)==-1)
			exit(EXIT_FAILURE);
	message* msg;
	printf("initiating threads\n");fflush(stdout);

	////init dev files paths
	if (!(minorPaths=calloc(minorsNum,sizeof(*minorPaths)))){
		fprintf(stderr,"minorPaths malloc err\n");
		goto end;
	}
	for(i=0;i<minorsNum;++i){
		if(!(minorPaths[i]=malloc(MAX_PATH_LEN))){
			fprintf(stderr,"minor %u malloc failed\n",i);
			goto end;
		}
		_getDevFilePath(*(minorPaths+i),devFileBasePath,devFileBasePathLen,i);
		printf("dev file path:%s\n",*(minorPaths + i));
	}

	////init and start threads
	unsigned int msgCounter=0;
	for( i=0,t_arg=args; i<numThreads; t_arg=args + (++i) ){
		
		msgN=perThreadMessages;
		if (i == numThreads -1)	msgN+=residualMessages;
		////messages to read
		if(!(t_arg->readedMsgs=calloc(msgN,sizeof(*(t_arg->readedMsgs))))){
			fprintf(stderr,"malloc thread %d 's readedMsgs failed\n",i);
			ret=EXIT_FAILURE;
			goto t_argsFree;
		}
		t_arg->msgN=msgN;
		////messages to write 
		if(!(t_arg->toWriteMsgs=malloc( (msgN) * sizeof(*(t_arg->toWriteMsgs)) ))){
			fprintf(stderr,"malloc thread %d 's toWriteMsgs failed\n",i);
			ret=EXIT_FAILURE;
			goto t_argsFree;
		}
		for(unsigned int z=0; z<msgN; ++z,++msgCounter){
			t_arg->toWriteMsgs[z]=msgs+msgCounter;
			//DEBUG printf("thread %d <- msg: %s\n",i,(msgs+msgCounter)->data);
		}
		
		//TODO special thread flags to call ioctl cmds (pre zeroed)
		if(i == 5)		t_arg->flags |= SET_RECV_TIMEOUT;
		if(i == 6)		t_arg->flags |= SET_SEND_TIMEOUT;
		if(i == 7)		t_arg->flags |= REVOKE_DELAYED_MESSAGES;

		//start thread
		if((ret=pthread_create(&t_arg->thread,NULL,thread_work,t_arg))<0){
			handle_error(ret,"pthread_create");
			goto t_argsFree;	
		}
	}

	////join threads and deallocations
	long th_ret;
	t_argsFree:
		for( i=0,t_arg=args; i<numThreads; t_arg=args + (++i) ){
			///join thread if started
			if(t_arg->thread){
				if((ret=pthread_join(t_arg->thread,(void**) &th_ret)))
					handle_error(ret,"pthread_join");
				printf("thread[%d] returned %ld\n",i,th_ret);

				printf("thread[%d] readed %d messages\n",i,t_arg->msgN);
				_print_msgs(t_arg->readedMsgs,t_arg->msgN);
			}

			//free thread's readed messages
			if(!t_arg->readedMsgs)	continue; //err before msg to get calloc
			for(x=0,msg=t_arg->readedMsgs;x<t_arg->msgN;msg=t_arg->readedMsgs+(++x)){
				if(msg->data)	free(msg->data);	
			}
			free(t_arg->readedMsgs);
			//free thread's msgs to write pointers
			if(t_arg->toWriteMsgs)	free(t_arg->toWriteMsgs);
		}
		end:
		free(args);
		if(urndFp)	close(urndFp);
		_freeMsgs(msgs,ALL_MSGS_NUM);
		for(i=0;i<minorsNum;++i){
			if(!minorPaths[i])	break;
			free(minorPaths[i]);
		}
		free(minorPaths);

		exit(ret);
}


static void _getDevFilePath(char* dest,char* prefix,size_t prefixLen,long suffix){
	*dest='\0';			//prepare destination to be filled
	//gen a string with the given prefix concatted with the giben suffix in dest
	char suffixStr[MAX_SUFFIX_LEN]={'\0'};
	int suffixLen=snprintf(suffixStr,MAX_SUFFIX_LEN,"%ld",suffix)+1;	//get path suffix

	strncat(dest,prefix,prefixLen);	//get the common path prefix
	strncat(dest,suffixStr,suffixLen);	//full path builded

}

/*
 * alloc in @dst @msgNum msgs each with size (with terminator) in range  (msgMinSize,msgMaxSize), 
 * each msg is a [truncated] numeric string generated from /dev/urandom
 * return -1 on error
 */
static int _initMessages(message** dst,
	unsigned int msgNum, unsigned int msgMinSize, unsigned int msgMaxSize){
	
	unsigned int msgSize,x,i,written=0;
	unsigned long rndData[RND_LONG_NUM]; //filled with random data
	int wr;
	
	if(!(*dst=calloc(msgNum,sizeof(**dst)))){
		fprintf(stderr,"msgs data malloc failed\n");
		return -1;
	}
	message* msg;
	for (i=0,msg=*dst; i<msgNum; msg++,i++,written=0){
		//get rnd msg data
		if(_read_wrap(urndFp,(char*) &rndData,sizeof(rndData))<0){
			fprintf(stderr,"rnd read for msg data at msg num:%d fail\n",i);
			goto err;
		}	
		//get rnd msg len
		if(_read_wrap(urndFp,(char*) &msgSize,sizeof(msgSize))<0){
			fprintf(stderr,"rnd read for msg size at msg num:%d fail\n",i);
			goto err;
		}	
		msgSize=msgMinSize + (msgSize % (msgMaxSize - msgMinSize ));
		//alloc written bytes for the msg data
		if(!(msg->data = malloc(msgSize) )){
				fprintf(stderr,"msg data errd at %d\n",i);
				goto err;
		}
		//get rnd data 8bytes at times and convert to string
		for(x=0; x<RND_LONG_NUM && written+1 < msgSize; ++x){
			wr=snprintf(msg->data + written, msgSize - written,"%lu",rndData[x]);
			if(wr < 0){
					fprintf(stderr,"snprintf failed with %d \n",written);
					goto err;
			}
			if( (unsigned int ) wr > msgSize - written ){
					fprintf(stderr,"msg num:%d truncated\n",i);
					wr=msgSize - written;
			}
			written+=wr;
		}
		msg->len= ++written; //consider '\0' 
		//add HEADER_LEN chars progressive header
		if(snprintf(msg->data,HEADER_LEN+1,HEADER_FRMT_STR,i) != HEADER_LEN ){
				fprintf(stderr,"msg prefix error\n");
				goto err;
		}
		msg->data[HEADER_LEN]='\t';
		DEBUG printf("<<msg\t%s\t%uB\n",msg->data,msg->len);
	}
	return 0;

	err:
		//free allocated messages
		for(int msgIdx=i; msgIdx>=0; msgIdx--){
			if((*dst+msgIdx)->data)	free((*dst+msgIdx)->data);
		}
		free(*dst);
		return -1;
}

static void _freeMsgs(message* ms,unsigned int num){
	message* m=ms;
	for(unsigned int x=0;x<num;++x,++m){
		free(m->data);	
	}
	free(ms);
}


static void _print_msgs(message* msg,unsigned int num){
	message* m;
	for(unsigned int x=0; x<num; ++x){
		m=msg+x;
		if(!(m->data)){
				printf("empty\n");
				//break;
				continue;
		}
		if( printf(">>msg\t%s\t%uB\n",m->data,m->len) < MIN_MSG_LEN )
				fprintf(stderr,"msg:%u-short msg\n",x);	//TODO DEBUG
	}
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
