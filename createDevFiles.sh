#!/bin/bash
max_minor=$(cat /sys/module/timed_msg_sys/parameters/num_minors )
major=$(cat /sys/module/timed_msg_sys/parameters/Major )
dev_file_basename="timed_msg_sys_"	
if [ $DEV_FILE_BASENAME ];then	dev_file_basename=$DEV_FILE_BASENAME;fi
#create device files
rm -f $dev_file_basename*
for x in $(seq 0 $max_minor);do sudo mknod $dev_file_basename$x c $major $x;done
