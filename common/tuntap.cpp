
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "tuntap.h"

namespace msctl { namespace common {

#ifndef _WIN32
    int opentuntap( const char *dev, int flags )
    {
        const char *clonedev = "/dev/net/tun";

        struct ifreq ifr;
        int fd = -1;

        if( (fd = open(clonedev , O_RDWR)) < 0 ) {
            return fd;
        }

        memset(&ifr, 0, sizeof(ifr));

        ifr.ifr_flags = flags;

        if (*dev) {
            strncpy(ifr.ifr_name, dev, IFNAMSIZ);
        }

        if( ioctl( fd, TUNSETIFF, static_cast<void *>(&ifr) ) < 0 ) {
            close( fd );
            return -1;
        }

        return fd;
    }

    int open_tun( const std::string &name )
    {
        return opentuntap( name.c_str( ), IFF_TUN | IFF_NO_PI );
    }

    int open_tap( const std::string &name )
    {
        return opentuntap( name.c_str( ), IFF_TAP | IFF_NO_PI );
    }
#else

#endif

}}
