#include <iostream>

#include <string.h>
#include <unistd.h>
#include <net/if.h>

#include <linux/if_tun.h>

#include <sys/ioctl.h>
#include <fcntl.h>
#include <netinet/in.h>

#include <linux/ipv6.h>
#include <linux/ip.h>

#include "tuntap.h"

#include "boost/asio/ip/address_v4.hpp"
#include "boost/asio/ip/address_v6.hpp"

namespace msctl { namespace common {

    namespace ba = boost::asio;

#ifndef _WIN32

    struct fd_keeper {
        int fd_ = -1;

        explicit fd_keeper( int fd )
            :fd_(fd)
        { }

        ~fd_keeper( )
        {
            if( fd_ != -1 ) {
                close( fd_ );
            }
        }
        operator int ( )
        {
            return fd_;
        }
    };

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

        auto add = get_iface_address( dev );
//        std::cout << "v4: " << add.first.first.to_string( )
//                  << " v6: " << add.second.first.to_string( )
//                  << "\n";

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

    addres_mask_v4 get_v4( const std::string &dev )
    {
        struct ifreq ifr;
        memset( &ifr, 0, sizeof(ifr) );

        strncpy( ifr.ifr_name, dev.c_str( ), IFNAMSIZ );

        fd_keeper s(socket(AF_INET, SOCK_DGRAM, 0));

        if( s < 0 )  {
            return addres_mask_v4( );
        }

        if( ioctl( s, SIOCGIFADDR, static_cast<void *>(&ifr) ) < 0 ) {
            return addres_mask_v4( );
        }
        sockaddr_in * sa = reinterpret_cast<sockaddr_in *>(&ifr.ifr_addr);
        auto addr = ba::ip::address_v4(ntohl(sa->sin_addr.s_addr));

        if( ioctl( s, SIOCGIFNETMASK, static_cast<void *>(&ifr) ) < 0 ) {
            return addres_mask_v4( );
        }
        sa = reinterpret_cast<sockaddr_in *>(&ifr.ifr_netmask);
        auto mask = ba::ip::address_v4(ntohl(sa->sin_addr.s_addr));

        return std::make_pair( addr, mask );
    }

    addres_mask_v6 get_v6( const std::string &dev )
    {
        struct ifreq ifr;
        struct in6_ifreq ifr6;

        memset( &ifr, 0, sizeof(ifr) );
        memset( &ifr6, 0, sizeof(ifr6) );

        strncpy( ifr.ifr_name, dev.c_str( ), IFNAMSIZ );

        fd_keeper s(socket(AF_INET, SOCK_DGRAM, 0));

        if( s < 0 )  {
            return addres_mask_v6( );
        }

        if( ioctl( s, SIOGIFINDEX, static_cast<void *>(&ifr) ) < 0 ) {
            //std::perror( "v6 SIOGIFINDEX" );
            return addres_mask_v6( );
        }
        std::cout << "det dev ID: " << dev << " "
                  << ifr.ifr_ifindex << "\n";

        ifr6.ifr6_ifindex = ifr.ifr_ifindex;
        ifr6.ifr6_prefixlen = 64;

        if( ioctl( s, SIOCGIFADDR, static_cast<void *>(&ifr6) ) < 0 ) {
            //std::perror( "v6 SIOCGIFADDR" );
            return addres_mask_v6( );
        }

        sockaddr_in6 * sa6 = reinterpret_cast<sockaddr_in6 *>(&ifr6.ifr6_addr);
        boost::asio::ip::address_v6::bytes_type bytes;
        std::copy( &sa6->sin6_addr.s6_addr[0],
                   &sa6->sin6_addr.s6_addr[bytes.max_size( )],
                   bytes.begin( ) );
        auto addr = boost::asio::ip::address_v6(bytes);

        if( ioctl( s, SIOCGIFNETMASK, static_cast<void *>(&ifr6) ) < 0 ) {
            //std::perror( "v6 SIOCGIFNETMASK" );
            return addres_mask_v6( );
        }

        sa6 = reinterpret_cast<sockaddr_in6 *>(&ifr6.ifr6_addr);
        std::copy( &sa6->sin6_addr.s6_addr[0],
                   &sa6->sin6_addr.s6_addr[bytes.max_size( )],
                   bytes.begin( ) );
        auto mask = boost::asio::ip::address_v6(bytes);

        return std::make_pair( addr, mask );
    }

    iface_address_pair get_iface_address( const std::string &dev )
    {
        return std::make_pair(get_v4(dev), get_v6(dev));
    }

#else

#endif

}}
