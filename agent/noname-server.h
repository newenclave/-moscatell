#ifndef MSCTL_NONAME_SERVER_H
#define MSCTL_NONAME_SERVER_H

#include <memory>
#include <cstdint>

#include "srpc/common/observers/define.h"
#include "srpc/common/observers/simple.h"

#include "srpc/server/acceptor/interface.h"
#include "srpc/common/transport/interface.h"
#include "srpc/common/transport/types.h"

#include "noname-common.h"
#include "application.h"
#include "subsys-listener2.h"

namespace msctl { namespace agent { namespace noname {


    struct interface: public srpc::enable_shared_from_this<interface> {

    public:

        using shared_type    = srpc::shared_ptr<interface>;
        using error_code     = srpc::common::transport::error_code;
        using io_service     = srpc::common::transport::io_service;
        using acceptor_type  = srpc::server::acceptor::interface;
        using client_sptr    = std::shared_ptr<client_info>;

        SRPC_OBSERVER_DEFINE( on_accept, void (client_sptr,
                                               const std::string &,
                                               std::uint16_t) );

        SRPC_OBSERVER_DEFINE( on_client_close, void (client_sptr) );
        SRPC_OBSERVER_DEFINE( on_accept_error, void (const error_code &) );
        SRPC_OBSERVER_DEFINE( on_close, void ( ) );

    public:
        virtual ~interface( ) { }

        virtual void start( ) = 0;
        virtual void stop(  ) = 0;
        virtual acceptor_type *acceptor( ) = 0;
    };

    using server_ptr  = interface *;
    using server_sptr = std::shared_ptr<interface>;
    using server_wptr = std::weak_ptr<interface>;

    namespace tcp {
        server_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port );
    }

    namespace udp {
        server_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port );
    }

}}}

#endif // NONAMESERVER_H
