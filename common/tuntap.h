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

    void close_handle( native_handle hdl );

    class device_info {
        native_handle handle_ = TUN_HANDLE_INVALID_VALUE;
        std::string   name_;

        void close( )
        {
            if( handle_ != TUN_HANDLE_INVALID_VALUE ) {
                close_handle( handle_ );
            }
        }

    public:

        device_info( device_info &&other )
        {
            swap(other);
        }

        device_info &operator = ( device_info &&other )
        {
            swap(other);
            return *this;
        }

        device_info( ) = default;

        device_info( const device_info&  ) = delete;
        device_info& operator = ( const device_info&  ) = delete;

        ~device_info( )
        {
            close( );
        }

        void assign( native_handle handle )
        {
            close( );
            handle_ = handle;
        }

        void assign_name( const std::string &name )
        {
            name_ = name;
        }

        const std::string &name( )
        {
            return name_;
        }

        native_handle get( ) const
        {
            return handle_;
        }

        native_handle release( )
        {
            auto tmp = handle_;
            handle_ = TUN_HANDLE_INVALID_VALUE;
            return tmp;
        }

        void swap( device_info &other )
        {
            std::swap(handle_, other.handle_);
            name_.swap( other.name_ );
        }
    };

    int device_up( const std::string &name );

    device_info open_tun( const std::string &hint_name );
    int del_tun( const std::string &name );
    void setup_device( native_handle device,
                       const std::string &name,
                       const std::string &ip,
                       const std::string &otherip,
                       const std::string &mask );

}}

#endif // TUNTAP_H
