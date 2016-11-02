#ifndef TUNTAP_H
#define TUNTAP_H

#include <string>
#include "async-transport-point.hpp"
#include "boost/asio/ip/address_v4.hpp"
#include "boost/asio/ip/address_v6.hpp"

#ifndef _WIN32
#include "boost/asio/posix/stream_descriptor.hpp"
#else
#include "boost/asio/windows/stream_handle.hpp"
#endif

namespace msctl { namespace common {

#ifndef _WIN32

    using stream_type = boost::asio::posix::stream_descriptor;
    using tuntap_transport = async_transport::point_iface<stream_type>;
    using native_handle = stream_type::native_handle_type;
    static const native_handle TUN_HANDLE_INVALID_VALUE = -1;

#else

    using stream_type = boost::asio::windows::stream_handle;
    using tuntap_transport = async_transport::point_iface<stream_type>;
    using native_handle = stream_type::native_handle_type;
    static const native_handle TUN_HANDLE_INVALID_VALUE = INVALID_HANDLE_VALUE;

#endif

    using addres_mask_v4 = std::pair<boost::asio::ip::address_v4,
    boost::asio::ip::address_v4>;
    using addres_mask_v6 = std::pair<boost::asio::ip::address_v6,
    boost::asio::ip::address_v6>;

    using src_dest_v4 = std::pair<std::uint32_t, std::uint32_t>;

    src_dest_v4 extract_ip_v4( const char *data, size_t len );
    int extract_family( const char *data, size_t len );


    struct device_info {
        native_handle handle = TUN_HANDLE_INVALID_VALUE;
        std::string	  name;
    };

    device_info open_tun( const std::string &hint_name );
    void clone_handle( native_handle hdl );
    int del_tun( const std::string &name );
    void setup_device( native_handle device,
                       const std::string &ip,
                       const std::string &otherip,
                       const std::string &mask );

}}

#endif // TUNTAP_H
