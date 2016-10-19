#ifndef TUNTAP_H
#define TUNTAP_H

#include <string>
#include "async-transport-point.hpp"
#include "boost/asio/ip/address_v4.hpp"
#include "boost/asio/ip/address_v6.hpp"

#ifndef _WIN32
#include "boost/asio/posix/stream_descriptor.hpp"
#else
#error "Windows is not yet supported"
#endif


namespace msctl { namespace common {

    using addres_mask_v4 = std::pair<boost::asio::ip::address_v4,
                                     boost::asio::ip::address_v4>;
    using addres_mask_v6 = std::pair<boost::asio::ip::address_v6,
                                     boost::asio::ip::address_v6>;

    using iface_address_pair = std::pair<addres_mask_v4, addres_mask_v6>;

    iface_address_pair get_iface_address( const std::string &dev );

#ifndef _WIN32

    using posix_stream = boost::asio::posix::stream_descriptor;
    using tuntap_transport = async_transport::point_iface<posix_stream>;

    int open_tun( const std::string &name );
    int open_tap( const std::string &name );



#else

#endif
}}

#endif // TUNTAP_H
