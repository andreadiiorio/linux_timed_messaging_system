/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 */

#ifndef _timed_msg_sys
#define _timed_msg_sys

#include "mod_ioctl.h"
/// /sys export - helper function declare macros
// var GET - PUT function definition macros
#define SYSVAR_GET_NAME(var)	sys_get_##var
#define SYSVAR_PUT_NAME(var)	sys_put_##var
#define SYSVAR_GET(var)		\
	static ssize_t SYSVAR_GET_NAME(var)(struct kobject *kobj, struct kobj_attribute *attr, char *buf){\
        	return sprintf(buf, "%lu\n", var);}

#define SYSVAR_PUT(var)		\
	static ssize_t SYSVAR_PUT_NAME(var)(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count){\
        sscanf(buf, "%lu", &var);\
        return count;}

///Configuration
#define NUM_MINOR	10	//max concurrent instances supported TODO

#define MODNAME "TIMED_MESSAGING_SYS"
#define DEVICE_NAME	"TIMED_MESSAGING_SYS"


#endif
