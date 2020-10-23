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

#ifndef _mod_ioctl
#define _mod_ioctl

//module ioctl cmds
#define REVOKE_DELAYED_MESSAGES		1
#define SET_SEND_TIMEOUT			1<<2
#define SET_RECV_TIMEOUT			1<<3
                                       
#define FLUSH						1<<5//invoke flush
//toogle flush effect limiting on the c6lling sess (dflt limit=off)
#define LIMIT_FLUSH_SESS_TOGGLE		1<<7
#define DEL_STORED_MESSAGES			1<<8
#endif
