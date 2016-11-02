#include "../tuntap.h"

#if defined( __FreeBSD__) || defined( __OpenBSD__) ||  defined(__APPLE__)

#define UNIX_CODE

#include <netinet/in.h>
#include <net/if_tun.h>
#include <netinet/ip.h>

#define TUNTAP_DEVICE_NAME "/dev/tun"
#define IFF_TUN     0x0001
#define IFF_TAP     0x0002
#define IFF_NO_PI   0x1000

#endif