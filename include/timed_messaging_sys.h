/*
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
