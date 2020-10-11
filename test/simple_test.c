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

#define DRNG_DEVFILE "/dev/urandom"
#define max(x, y) (((x) > (y)) ? (x) : (y))

unsigned int MAX_MSG_LEN=1024,MIN_MSG_LEN=128;

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
int main(int argc,char** argv){
	unsigned int i,msgSize,written;
	int devFp,urndFp,wr,rd;
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
	char* msg=malloc(MAX_MSG_LEN); //tmp msg
	if(!msg){
			fprintf(stderr,"tmp msg alloc failed\n");
			ret=EXIT_FAILURE;
			goto end;
	}
	////gen and write rnd messages
	unsigned long rndData;
	for(i=0;i<msgN;++i){
		//get rnd msg len
		if(_read_wrap(urndFp,(char*) &msgSize,sizeof(msgSize))<0){
			fprintf(stderr,"rnd read for msg size at msg num:%d fail\n",i);
			ret=EXIT_FAILURE;
			goto end;
		}	
		msgSize=MIN_MSG_LEN + (msgSize % (MAX_MSG_LEN - MIN_MSG_LEN ));
		written=0;
		///get rnd longs and concat them in 1! string to write
		while(written + 1 < msgSize){
			//get rnd msg data
			if(_read_wrap(urndFp,(char*) &rndData,sizeof(rndData))<0){
				fprintf(stderr,"rnd read for msg data at msg num:%d fail\n",i);
				ret=EXIT_FAILURE;
				goto end;
			}	
			wr=snprintf(msg+written,msgSize-written,"%lu",rndData);
			if(wr< 0){
					fprintf(stderr,"snprintf failed with %d \n",written);
					ret=wr;
					goto end;
			}
			if( (unsigned int) wr >= msgSize - written){
					//printf("msg num:%d truncated\n",i);
					wr = msgSize - written;
			}
			//if(written < msgSize - written ) printf("residual non used for rnd msg: %d, msgSize=%d\n",msgSize-written,msgSize);
			written+=wr;
		}
		printf("writing %s long %d\n\n",msg,msgSize);
		if((wr=write(devFp,msg,msgSize))<0){
			fprintf(stderr,"wr on dev failed with %d",wr);
			ret=wr;
			goto end;
		}
	}
	//read messages from the dev
	for(i=0;i<msgN;++i){
		if((rd=read(devFp,msg,MAX_MSG_LEN))<0){
				fprintf(stderr,"read on the dev failed with %d\n",rd);
				ret=rd;
				goto end;
		}
		printf("Readed %d bytes \t %s\n",rd,msg);
	}
	end:
	if(msg)	free(msg);
	close(devFp);
	return ret;
}
