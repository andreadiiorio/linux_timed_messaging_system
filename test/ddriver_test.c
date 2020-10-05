/*
 * Andrea Di Iorio
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <limits.h>


const char* const usage = "usage <devFileBasePath,minorsNum,threadsPerMinorNum,[MAX_MSG_LEN]>";
unsigned long MAX_MSG_LEN=4096;	//max bytes per message

//config
#define MAX_MINOR_PER_THREAD	3		//max minor globally associated to a thread
#define MIN_MSG_LEN				128 	//min bytes per message
#define MAX_SUFFIX_LEN			5		//max chars per device file suffix
#define MAX_PATH_LEN			256
#define ALL_MSGS_NUM			50		//all messages to exchange between threads
#define MAX_MSG_PER_ITERATION	6		//upper bound of num of msg to read/write 

#define DRNG_DEVFILE			"/dev/urandom"
//support macro
#define DEBUG	if(1)
#define AUDIT	if(1)
//print standard error message 
#define handle_error(errCode, msg) fprintf(stderr,"%s %s",msg,strerror(-errCode))
typedef struct _msg {
		char* data;
		unsigned int len;
} message;
typedef struct _thread_args {
		char** paths;//NULL terminated list of paths to devFile with the given minor,
		//not more then MAX_MINOR_PER_THREAD
		
		//exchanged msgs by the thread
		unsigned int msgN;    //num msgs to (write) read (onto)from the dev
		message** toWriteMsgs;//msgs that the thread has to write to the devices
		message* readedMsgs;  //msgs readed from the thread from the devices

		pthread_t thread;	
}	thread_arg;

//random integers formatted as varlen strings \0 terminated contiguos
//will be splitted between threads as rdonly msgs to exchange
message* msgs;

int urndFp;	//file pointer to DRNG_DEVFILE
//helper prototypes
static void _print_double_pointer_list(char** dblPtr);
static void _print_msgs(message*,unsigned int);
static void _print_msgs_link(message**,unsigned int);
static int _read_wrap(int fd,char* dst,size_t count);
static void _free_double_pointer_list(char** dblPtr);
static void _getDevFilePath(char* dest,char* prefix,size_t prefixLen,long suffix);
static int _initMessages(message**,unsigned int,unsigned int,unsigned int);
static void _freeMsgs(message* ms,unsigned int num);
void* thread_work(void* arg);
int _msg_rd(int fp,char* dst,unsigned int num);
int _msg_wr(int fp,char* src,unsigned int num);

/*
 *	open associated device files and read-write msgs cyclically on each of them
 *	at each iteration will be selected a random op (0 = WR, !0 = RD) and rnd num
 *	of msgs to RW||WR, subtracted from the num of msgs to exchange by the thread
 *	return (casted) EXIT_SUCCESS on success, on err EXIT_FAILURE
 */
void* thread_work(void* arg){
	thread_arg* t_arg= (thread_arg*) arg;
	int ret=EXIT_SUCCESS,fp,wr,rd,x,i=0;
	int minorsFp[MAX_MINOR_PER_THREAD];
	char** p=t_arg->paths;
	
	///open dev files in minorsFp
	while(*p){
		if( (fp=open(*p,O_RDWR) <0) ){
				perror("open");
				ret=EXIT_FAILURE;
				goto end;
		}
		minorsFp[i++]=fp;

		p++; //next path entry
	}
	unsigned int numMinors=i-1;
	
	unsigned int old,toWriteN=t_arg->msgN,toReadN=t_arg->msgN;
	unsigned int* msgBudget;	//point to toWriteN or toReadN
	///cyclic read  thread's assigned msgs from the devFiles vars
	message* msgDst=t_arg->readedMsgs;
	///cyclic write thread's assigned msgs to       devFiles vars
	i=0;
	message** msgSrc=t_arg->toWriteMsgs;
	char* tmp;
	//rnd vars
	char rndOP,rndMsgN;
	//char op=-1; //if > 0 hold the fixed op todo (the other has ended the budget)
	while( toWriteN || toReadN ){
		//rndOP
		if(_read_wrap(urndFp,&rndOP,sizeof(rndOP))<0){
			fprintf(stderr,"rnd read for thread's OP failed");
			ret=EXIT_FAILURE;
			goto end;
		}
		rndOP=rndOP % 2;	// select RD - WR
		//rndMsgN
		if(_read_wrap(urndFp,&rndMsgN,sizeof(rndMsgN))<0){
			fprintf(stderr,"rnd read for thread's num msgs to exchange failed");
			ret=EXIT_FAILURE;
			goto end;
		}
		rndMsgN= 1+(rndMsgN % (MAX_MSG_PER_ITERATION) );
		//decrease msgs budget
		msgBudget=&toWriteN;
		if (rndOP)	msgBudget=&toReadN;
		if (*msgBudget<rndMsgN){
			rndMsgN=*msgBudget;
			*msgBudget=0;
		} 
		else	*msgBudget-=rndMsgN;

		//exchange rnd num of msg
		for (x=0,i=0; x<rndMsgN; i=( (++x)%numMinors)){
			fp=minorsFp[i];
			if(rndOP){		//selected READ
				if(!(msgDst->data=malloc(MAX_MSG_LEN))){
						fprintf(stderr,"msg to read data malloc failed\n");
						ret=EXIT_FAILURE;
						goto end;
				}
				fflush(0);printf("read to %d\n",fp);fflush(0);continue;
				if( (rd=read(fp,msgDst->data,MAX_MSG_LEN))<0){
					fprintf(stderr,"READ TO THE DEV FILE %d FAILED\n",fp);
					handle_error(-rd,"read");
					//TODO FATAL FILTER
					ret=rd;
					goto end;
				}
				old=msgDst->len;
				msgDst->len=rd;
				//realloc message with the ammount of readed data from the devFile
				if ((tmp=realloc(msgDst->data,msgDst->len))) msgDst=(message*)tmp;
				else  fprintf(stderr,"realloc of msg from %d -> %d fail",old,rd);
				msgDst++;
			}
			else{			//selected WRITE
				fflush(0);printf("write to %d\n",fp);fflush(0);continue;
				if( (wr=write(fp,(*msgSrc)->data,(*msgSrc)->len))<0 ){
					fprintf(stderr,"MSG WRITE FAILED ON DEVF FP %d \n",fp);
					handle_error(-wr,"write");
					//TODO FATAL FILTER
					ret=wr;
					goto end;
				}
				msgSrc++;
			}
		}

	}

	end:
		return (void*) ret;
}

int main(int argc,char** argv,char** env){
	unsigned int i,x;
	printf("%lu \n",sizeof(char*));
	//char** c=env;	while(*c)	{printf("%s\n",*c);c++;}	printf("\n\n");
	int ret=EXIT_SUCCESS;
	if(argc<4){
			fprintf(stderr,"%s",usage);
			exit(EXIT_FAILURE);
	}
	char* devFileBasePath=argv[1];
	size_t devFileBasePathLen=strnlen(devFileBasePath,MAX_PATH_LEN);
	//parse decimal strings
	char* ptr;
	unsigned long minorsNum=strtoul(argv[2],&ptr,10);
	if (ptr==argv[2] || minorsNum== LONG_MIN || minorsNum == LONG_MAX){
			perror("strtol errd");
			exit(EXIT_FAILURE);
	}
	unsigned long threadsPerMinorNum=strtoul(argv[3],&ptr,10);
	if (ptr==argv[2] || threadsPerMinorNum == LONG_MIN || threadsPerMinorNum == LONG_MAX){
			perror("strtol may errd");
			exit(EXIT_FAILURE);
	}
	if(argc==5){	//overwrite MAX_MSG_LEN
		MAX_MSG_LEN=strtoul(argv[3],&ptr,10);
		if (ptr==argv[3] || MAX_MSG_LEN== LONG_MIN || MAX_MSG_LEN== LONG_MAX){
				perror("strtol may errd");
				exit(EXIT_FAILURE);
		}
	}

	unsigned long numThreads=minorsNum * threadsPerMinorNum;
	unsigned long perThreadMessages=ALL_MSGS_NUM / numThreads;
	unsigned long residualMessages =ALL_MSGS_NUM % numThreads; //allocated to last thread
	unsigned long msgN;

	thread_arg* args=calloc(numThreads,sizeof(*args)); //per thread args
	if (!args){
			fprintf(stderr,"threads args malloc failed\n");
			exit(EXIT_FAILURE);
	}
	thread_arg* t_arg; //point to current thread metadata to initiate
	
	if((urndFp=open(DRNG_DEVFILE,O_RDONLY))<0){
			perror("open");
			exit(EXIT_FAILURE);
	}

	////init messages data with random numeric strings taken from DRNG_DEVFILE
	if(_initMessages(&msgs,ALL_MSGS_NUM,MIN_MSG_LEN,MAX_MSG_LEN)==-1)
			exit(EXIT_FAILURE);
	message* msg;
	DEBUG for(x=0,msg=msgs;x<ALL_MSGS_NUM;msg=msgs+(++x)) 
			printf("message[%d]: %s\n",x,msg->data);

	////init and start threads
	unsigned int threadMinors,minor=0,msgCounter=0;
	for( i=0,t_arg=args; i<numThreads; t_arg=args + (++i) ){
		////link thread to rnd devFiles plus a progressive one
		//get a rnd num of devFile associated to the current thread in init
		if(_read_wrap(urndFp,(char*) &threadMinors,sizeof(threadMinors))<0){
				fprintf(stderr,"rnd minor per thread %d failed\n",i);
				ret=EXIT_FAILURE;
				goto t_argsFree;
		}
		threadMinors = 1+(threadMinors % (MAX_MINOR_PER_THREAD  ));//at least 1
		AUDIT printf("threadMinors: %d for thread %d -> arg at %px \n",threadMinors,i,&t_arg);
		//init dev file paths 
		if(!(t_arg->paths=malloc( (threadMinors+1)*sizeof(*t_arg->paths) ))){
			fprintf(stderr,"malloc thread %d 's paths failed\n",i);
			ret=EXIT_FAILURE;
			goto t_argsFree;
		}
		t_arg->paths[0]=t_arg->paths[threadMinors]=NULL; //pre terminate list
		for (unsigned int z=0;z<threadMinors;z++){
				char* minorPath;
				if(!(minorPath=malloc(MAX_PATH_LEN))){
						fprintf(stderr,"malloc path failed\n");
						ret=EXIT_FAILURE;
						goto t_argsFree;
				}
				_getDevFilePath(minorPath,devFileBasePath,devFileBasePathLen,minor++);
				t_arg->paths[z]=minorPath;	//link
				printf("thread %d associated to dev %s\n",i,minorPath);
		}
		
		msgN=perThreadMessages;
		if (i == numThreads -1)	msgN+=residualMessages;
		////messages to read
		if(!(t_arg->readedMsgs=calloc(msgN,sizeof(*t_arg->readedMsgs)))){
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
		}
		
		////start thread
		if((ret=pthread_create(&t_arg->thread,NULL,thread_work,t_arg))<0){
			handle_error(ret,"pthread_create");
			goto t_argsFree;	
		}
	}
	
	long th_ret;
	t_argsFree:
		for( i=0,t_arg=args; i<numThreads; t_arg=args + (++i) ){
			///join thread
			if((ret=pthread_join(t_arg->thread,(void**) &th_ret)))
				handle_error(ret,"pthread_join");
			printf("thread [%d] returned %ld\n",i,th_ret);

			AUDIT{
				printf("arg of thread %d\n",i);
				printf("paths\n");
				_print_double_pointer_list(t_arg->paths);
				printf("toWriteMsgs\n");
				_print_msgs_link(t_arg->toWriteMsgs,t_arg->msgN);
				printf("readedMsgs [%d] \n",t_arg->msgN);
				_print_msgs(t_arg->readedMsgs,t_arg->msgN);
			}
			_free_double_pointer_list(t_arg->paths);
			//free thread's written messages
			if(t_arg->toWriteMsgs)	free(t_arg->toWriteMsgs);
			//free thread's readed messages
			if(t_arg->readedMsgs){
				for(x=0,msg=t_arg->readedMsgs;x<t_arg->msgN;msg=t_arg->readedMsgs+(++x)){
					if(msg->data)	free(msg->data);	
				}
				free(t_arg->readedMsgs);
			}
		}
		_freeMsgs(msgs,ALL_MSGS_NUM);
		free(args);
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
 * alloc in @dst @msgNum msgs contiguos each \0 terminated
 * each with size (excluded terminator) in range  (msgMinSize,msgMaxSize), 
 * each msg is a [truncated] numeric string generated from /dev/urandom
 * return -1 on error
 */
static int _initMessages(message** dst,
	unsigned int msgNum, unsigned int msgMinSize, unsigned int msgMaxSize){
	
	unsigned int msgSize,i;
	long rndData;	//will be iterativelly filled with rnd bytes from urandom
	int written;
	
	if(!(*dst=malloc(msgNum*sizeof(**dst)))){
		fprintf(stderr,"msgs data malloc failed\n");
		return -1;
	}
	message* msg;
	for (i=0,msg=*dst; i<msgNum; msg=*dst+(++i)){
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
		written=snprintf(msg->data,msgSize,"%20ld",rndData);
		if(written < 0){
				fprintf(stderr,"snprintf failed with %d \n",written);
				goto err;
		}
		if( ++written > msgSize){ //include the excluded \0 in snprintf return
				fprintf(stderr,"msg num:%d truncated\n",i);
				written=msgSize;
		}
		if(written<msgSize) memset(msg->data+written,0,msgSize-written); //pad the non written
		msg->len=written;
	}
	return 0;

	err:
		free(*dst);
		//free allocated messages
		for(int msgIdx=i-1; msgIdx>=0; msgIdx--) free((*dst+msgIdx)->data);
		return -1;
}

static void _freeMsgs(message* ms,unsigned int num){
	message* m=ms;
	for(int x=0;x<num;++x,++m){
		free(m->data);	
	}
	free(ms);
}

static void _free_double_pointer_list(char** dblPtr){
	//free a double pointer list NULL terminated
	//list expected to be init with NULL dblPtr or *dblPtr=NULL
	char* entry=NULL;
	if(!dblPtr)		return; //non init double pointer
	unsigned int x=0;
	entry=dblPtr[x];
	while(entry){
		free(entry);
		entry=dblPtr[++x];
	}
	free(dblPtr);
}

static void _print_msgs(message* msg,unsigned int num){
	for(int x=0;x<num;msg+=(++x)){
		if(!msg->data){
				printf("empty\n");
				break;
		}
		printf("%s\n",msg->data);
	}
}
static void _print_msgs_link(message** msgBase,unsigned int num){
	message*msg;
	for(int x=0;x<num;x++){
		msg=msgBase[x];
		if(!(msg->data)){
				printf("empty\n");
				break;
		}
		printf("%s\n",msg->data);
	}
}
static void _print_double_pointer_list(char** dblPtr){
	//free a double pointer list NULL terminated
	//list expected to be init with NULL dblPtr or *dblPtr=NULL
	char* entry=NULL;
	if(!dblPtr)	{printf("empty\n");return;}	
	unsigned int x=0;
	entry=dblPtr[x];
	while(entry){
		printf("%s ",entry);
		entry=dblPtr[++x];
	}
	printf("\n");
}

static int _read_wrap(int fd,char* dst,size_t count){
	ssize_t readed=0,rd=0;
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
