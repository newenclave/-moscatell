#include <iostream>

#include <string.h>
#include <unistd.h>
#include <net/if.h>

#if defined(__linux__)

#include <linux/if_tun.h>
//#include <linux/ipv6.h>
#include <linux/ip.h>

#elif defined( __FreeBSD__) || defined( __OpenBSD__) ||  defined(__APPLE__)

#include <net/if_tun.h>
#include <netinet/ip.h>

#elif defined(_WIN32)

#elif
//#error
#endif

#include <sys/ioctl.h>
#include <fcntl.h>
#include <netinet/in.h>


#include "tuntap.h"

#include "boost/asio/ip/address_v4.hpp"
#include "boost/asio/ip/address_v6.hpp"

namespace msctl { namespace common {

    namespace ba = boost::asio;

#ifndef _WIN32

    struct fd_keeper {
        int fd_ = -1;

        fd_keeper( )
        { }

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

        int release( )
        {
            int tmp = fd_;
            fd_ = -1;
            return tmp;
        }
    };

    int set_v4_param( const char *devname, unsigned long code,
                      std::uint32_t param )
    {
        struct ifreq ifr;
        struct sockaddr_in addr;
        fd_keeper s;

        memset( &ifr, 0, sizeof(ifr) );
        memset( &addr, 0, sizeof(addr) );
        strncpy(ifr.ifr_name, devname, IFNAMSIZ);

        addr.sin_family = AF_INET;
        s.fd_ = socket(addr.sin_family, SOCK_DGRAM, 0);

        if( s.fd_ < 0 ) {
            return -1;
        }

        addr.sin_addr.s_addr = param;
        ifr.ifr_addr = *reinterpret_cast<sockaddr *>(&addr);

        if (ioctl(s, code, (caddr_t) &ifr) < 0 ) {
            return -1;
        }
        return 0;
    }

    int setip_v4_mask( const char *devname, const char *ip_addr )
    {
        boost::system::error_code ec;
        auto ip = ba::ip::address_v4::from_string( ip_addr, ec );
        if( ec ) {
            errno = ec.value( );
            return -1;
        }
        return set_v4_param( devname, SIOCSIFNETMASK, htonl(ip.to_ulong( )) );
    }

    int setip_v4_mask( const char *devname, std::uint32_t mask )
    {
        return set_v4_param( devname, SIOCSIFNETMASK, mask );
    }

    int setip_v4_addr( const char *devname, const char *ip_addr )
    {
        boost::system::error_code ec;
        auto ip = ba::ip::address_v4::from_string( ip_addr, ec );
        if( ec ) {
            errno = ec.value( );
            return -1;
        }
        return set_v4_param( devname, SIOCSIFADDR, htonl(ip.to_ulong( )) );
    }

    int set_dev_up( const char *devname )
    {
        struct ifreq ifr;
        struct sockaddr_in addr;
        fd_keeper s;

        memset( &ifr, 0, sizeof(ifr) );
        memset( &addr, 0, sizeof(addr) );
        strncpy(ifr.ifr_name, devname, IFNAMSIZ);

        addr.sin_family = AF_INET;
        s.fd_ = socket(addr.sin_family, SOCK_DGRAM, 0);

        if( s.fd_ < 0 ) {
            return -1;
        }

        ifr.ifr_flags = IFF_UP | IFF_RUNNING;

        if (ioctl(s, SIOCSIFFLAGS, &ifr) < 0 ) {
            return -1;
        }
        return 0;
    }

    /// for UP
    /// ioctl(sockfd, SIOCGIFFLAGS, &ifr);
    /// ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
    /// ioctl(sockfd, SIOCSIFFLAGS, &ifr);

    int del_persistent( const char *dev, int flags )
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

        ioctl( fd, TUNSETPERSIST, 0 );

        return fd;
    }

    int opentuntap( const char *dev, int flags, bool persis )
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

        flags = fcntl( fd, F_GETFL, 0 );
        if( flags < 0 ) {
            close( fd );
            return -1;
        }

        if( fcntl( fd, F_SETFL, flags | O_NONBLOCK ) < 0) {
            close( fd );
            return -1;
        }

        if( persis ) {
            ioctl( fd, TUNSETPERSIST, 1 );
        }

        return fd;
    }

    int del_tun( const std::string &name )
    {
        return del_persistent( name.c_str( ), IFF_TUN | IFF_NO_PI );
    }

    int open_tun(const std::string &name , bool persist )
    {
        return opentuntap( name.c_str( ), IFF_TUN | IFF_NO_PI, persist );
    }

    int del_tap( const std::string &name )
    {
        return del_persistent( name.c_str( ), IFF_TAP | IFF_NO_PI );
    }

    int open_tap(const std::string &name, bool persist)
    {
        return opentuntap( name.c_str( ), IFF_TAP | IFF_NO_PI, persist );
    }

    int device_up( const std::string &name )
    {
        return set_dev_up( name.c_str( ) );
    }

    int set_dev_ip4( const std::string &name, const std::string &ip )
    {
        return setip_v4_addr( name.c_str( ), ip.c_str( ) );
    }

    int set_dev_ip4_mask( const std::string &name, const std::string &mask )
    {
        return setip_v4_mask( name.c_str( ), mask.c_str( ) );
    }

    int set_dev_ip4_mask( const std::string &name, std::uint32_t mask )
    {
        auto fix_mask = mask == 32 ? 0xFFFFFFFF : ( 1 << mask ) - 1;
        return setip_v4_mask( name.c_str( ), fix_mask );
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

    addres_mask_v6 get_v6( const std::string & /*dev*/ )
    {
        return addres_mask_v6( );

//        struct ifreq ifr;
//        struct in6_ifreq ifr6;

//        memset( &ifr, 0, sizeof(ifr) );
//        memset( &ifr6, 0, sizeof(ifr6) );

//        strncpy( ifr.ifr_name, dev.c_str( ), IFNAMSIZ );

//        fd_keeper s(socket(AF_INET, SOCK_DGRAM, 0));

//        if( s < 0 )  {
//            return addres_mask_v6( );
//        }

//        if( ioctl( s, SIOGIFINDEX, static_cast<void *>(&ifr) ) < 0 ) {
//            //std::perror( "v6 SIOGIFINDEX" );
//            return addres_mask_v6( );
//        }
//        std::cout << "det dev ID: " << dev << " "
//                  << ifr.ifr_ifindex << "\n";

//        ifr6.ifr6_ifindex = ifr.ifr_ifindex;
//        ifr6.ifr6_prefixlen = 64;

//        if( ioctl( s, SIOCGIFADDR, static_cast<void *>(&ifr6) ) < 0 ) {
//            //std::perror( "v6 SIOCGIFADDR" );
//            return addres_mask_v6( );
//        }

//        sockaddr_in6 * sa6 = reinterpret_cast<sockaddr_in6 *>(&ifr6.ifr6_addr);
//        boost::asio::ip::address_v6::bytes_type bytes;
//        std::copy( &sa6->sin6_addr.s6_addr[0],
//                   &sa6->sin6_addr.s6_addr[bytes.max_size( )],
//                   bytes.begin( ) );
//        auto addr = boost::asio::ip::address_v6(bytes);

//        if( ioctl( s, SIOCGIFNETMASK, static_cast<void *>(&ifr6) ) < 0 ) {
//            //std::perror( "v6 SIOCGIFNETMASK" );
//            return addres_mask_v6( );
//        }

//        sa6 = reinterpret_cast<sockaddr_in6 *>(&ifr6.ifr6_addr);
//        std::copy( &sa6->sin6_addr.s6_addr[0],
//                   &sa6->sin6_addr.s6_addr[bytes.max_size( )],
//                   bytes.begin( ) );
//        auto mask = boost::asio::ip::address_v6(bytes);

//        return std::make_pair( addr, mask );
    }

    iface_address_pair get_iface_address( const std::string &dev )
    {
        return std::make_pair(get_v4(dev), get_v6(dev));
    }

    addres_mask_v4 get_iface_ipv4( const std::string &dev )
    {
        return get_v4( dev );
    }

    src_dest_v4 extract_ip_v4( const char *data, size_t len )
    {
        auto hdr = reinterpret_cast<const iphdr *>(data);

        if( len < sizeof(*hdr) ) {
            return src_dest_v4( );
        }

        if( hdr->version != 4 ) {
            return src_dest_v4( );
        }

        return std::make_pair( hdr->saddr, hdr->daddr );
    }

#else
    addres_mask_v4 get_iface_ipv4( const std::string &dev )
    {
        return addres_mask_v4( );
    }
#endif

}}
