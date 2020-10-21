/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
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
