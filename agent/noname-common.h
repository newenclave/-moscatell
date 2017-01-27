#ifndef MSCTL_NONAME_COMMON_H
#define MSCTL_NONAME_COMMON_H

#include <memory>
#include <cstdint>
#include "srpc/common/transport/interface.h"
#include "protocol/tuntap.pb.h"

namespace msctl { namespace agent { namespace noname {

    using transport_type = srpc::common::transport::interface;
    using transport_sptr = std::shared_ptr<transport_type>;
    using lowlevel_type  = msctl::rpc::tuntap::tuntap_message;
    using lowlevel_sptr  = std::shared_ptr<lowlevel_type>;

    struct client_info: public std::enable_shared_from_this<client_info> {
        virtual ~client_info( ) { }
        virtual transport_sptr transport( ) = 0;
        virtual bool post_message( lowlevel_sptr )  = 0;
        virtual bool send_message( lowlevel_sptr )  = 0;
        virtual lowlevel_sptr create_message( ) = 0;
        virtual bool ready( ) = 0;
        virtual void close( ) = 0;

        std::weak_ptr<client_info>         weak_from_this( )
        {
            return std::weak_ptr<client_info>(shared_from_this( ));
        }

        std::weak_ptr<client_info const>   weak_from_this( ) const
        {
            return std::weak_ptr<client_info const>(shared_from_this( ));
        }

    };

    using client_sptr = std::shared_ptr<client_info>;
    using client_wptr = std::weak_ptr<client_info>;

}}}

#endif // NONAMECOMMON_H
