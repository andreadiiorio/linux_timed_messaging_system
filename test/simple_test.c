/*
 * Andrea Di Iorio - 277550
 * write a series of messages, then read them from the same device file
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

#define DRNG_DEVFILE "/dev/urandom"
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define RND_LONG_NUM			5	//num of 8bytes of rnd data re
#define XSTR(s) STR(s)
#define STR(s) #s
#define HEADER_LEN				5	//progres. num prefix to each message len (just num)
#define HEADER_FRMT_STR			"%" XSTR(HEADER_LEN) "u" 
#define DEBUG	if(1)
unsigned int MAX_MSG_LEN=1024,MIN_MSG_LEN=128;
int urndFp;
typedef struct _msg {
		char* data;
		unsigned int len;
} message;

static int _read_wrap(int fd,char* dst,size_t count);
static int _initMessages(message**,unsigned int,unsigned int,unsigned int);
int main(int argc,char** argv){
	unsigned int i;
	int devFp,wr,rd;
	printf("%lu \n",sizeof(char*));
	int ret=EXIT_SUCCESS;
	if(argc<3){
			fprintf(stderr,"usage <targetDevFile,numMsgToWR&RD>\n");
			exit(EXIT_FAILURE);
	}
	char* devFilePath=argv[1];
	if((devFp=open(devFilePath,O_RDWR))<0){
			perror("open");
			exit(EXIT_FAILURE);
	}
	//parse decimal strings
	char* ptr;
	unsigned long msgN=strtoul(argv[2],&ptr,10);
	if (ptr==argv[2] || msgN == LONG_MIN || msgN == LONG_MAX){
			perror("strtol errd");
			exit(EXIT_FAILURE);
	}
	if((urndFp=open(DRNG_DEVFILE,O_RDONLY))<0){
			perror("open");
			exit(EXIT_FAILURE);
	}
	////gen rnd messages
	message *msgs=NULL,*msg;
	char* msg_data=NULL;
	if(!(msg_data=calloc(1,MAX_MSG_LEN))){
		fprintf(stderr,"msg_data alloc failed\n");
		ret=EXIT_FAILURE;
		goto end;
	}
	if((ret=_initMessages(&msgs,msgN,MIN_MSG_LEN,MAX_MSG_LEN)))	return EXIT_FAILURE;
	////write messages to the dev
	for(i=0,msg=msgs; i<msgN; msg=msgs+(++i)){
		if((wr=write(devFp,msg->data,msg->len))<0){
				fprintf(stderr,"write on the dev failed with %d\n",wr);
				perror("write");
				ret=wr;
				goto end;
		}
		printf("%u: written: %.5s, %u bytes\n",i,msg->data,wr);
	}
	////read messages from the dev
	for(i=0;i<msgN;++i){
		if((rd=read(devFp,msg_data,MAX_MSG_LEN))<0){
				fprintf(stderr,"read on the dev failed with %d\n",rd);
				perror("read");
				ret=rd;
				goto end;
		}
		printf("%u:  Readed %s ,%d bytes \n",i,msg_data,rd);
		memset(msg_data,0,MAX_MSG_LEN);
	}
	end:
	//free aux msgs vars
	for(i=0,msg=msgs; i<msgN; msg=msgs+(++i))	free(msg->data);
	free(msgs);
	if(msg_data)	free(msg_data);

	close(devFp);
	return ret;
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
		DEBUG printf("rnd msg %d: %s\n",i,msg->data);
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

static int _read_wrap(int fd,char* dst,size_t count){
	size_t readed=0;
	ssize_t rd=0;
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
