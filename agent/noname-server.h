#ifndef MSCTL_NONAME_SERVER_H
#define MSCTL_NONAME_SERVER_H

#include "srpc/common/config/memory.h"
#include "srpc/common/config/stdint.h"

#include "srpc/common/observers/define.h"
#include "srpc/common/observers/simple.h"

#include "srpc/server/acceptor/interface.h"
#include "srpc/common/transport/interface.h"
#include "srpc/common/transport/types.h"

namespace msctl { namespace agent { namespace noname {

    struct interface: public srpc::enable_shared_from_this<interface> {

    public:

        using shared_type    = srpc::shared_ptr<interface>;
        using error_code     = srpc::common::transport::error_code;
        using io_service     = srpc::common::transport::io_service;
        using transport_type = srpc::common::transport::interface;
        using acceptor_type  = srpc::server::acceptor::interface;

        SRPC_OBSERVER_DEFINE( on_accept, void (transport_type *,
                                               const std::string &,
                                               srpc::uint16_t) );
        SRPC_OBSERVER_DEFINE( on_accept_error, void (const error_code &) );
        SRPC_OBSERVER_DEFINE( on_close, void ( ) );

    public:
        virtual void start( ) = 0;
        virtual void stop(  ) = 0;
        virtual acceptor_type *acceptor( ) = 0;
    };

}}}

#endif // NONAMESERVER_H
