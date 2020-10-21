#!/bin/sh
#Andrea Di Iorio
#check ddriver_test output for correctly received&written messages
#pipe stdout of test to this script, will be duplicated on stderr
#core lines will be piped to check if a sent message on a device correspond to a received one on another

check_matching_msgs_py=$'from sys import stdin;
l=stdin.readlines()\nfor x in range(0,len(l),2):\n
 if l[x]==l[x+1]:print(x,x+1,"OK")\n else:print(l[x],"!=",l[x+1])'

tee /dev/stderr | grep -e "<<" -e ">>" | sort -n | python3 -c "$check_matching_msgs_py"
