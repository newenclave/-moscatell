#include "../tuntap.h"

#if defined(__linux__)

#include <linux/if_tun.h>
//#include <linux/ipv6.h>
#include <linux/ip.h>

#define TUNTAP_DEVICE_NAME "/dev/net/tun"


#endif