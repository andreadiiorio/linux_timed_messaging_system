#!/bin/bash
dev_file_basename="timed_msg_sys_"	max_minor=10	major=243
if [ $DEV_FILE_BASENAME ];then dev_file_basename=$DEV_FILE_BASENAME;fi
if [ $MAX_MINOR ];then max_minor=$MAX_MINOR;fi
if [ $MAJOR ];then major=$MAJOR; fi
for x in $(seq 0 $max_minor);do sudo mknod $dev_file_basename$x c $major $x;done
