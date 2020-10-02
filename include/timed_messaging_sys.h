/*
 *  TIMED MESSAGING SYSTEM
 *  Andrea Di Iorio	277550
 */
///Configuration
#define NUM_MINOR	10	//max concurrent instances supported TODO
#define AUDIT		if(1)

//ioctl cmd
#define SET_SEND_TIMEOUT	0
#define SET_RECV_TIMEOUT	1
#define REVOKE_DELAYED_MESSAGES	2
