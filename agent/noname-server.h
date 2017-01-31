#ifndef MSCTL_NONAME_SERVER_H
#define MSCTL_NONAME_SERVER_H

#include <memory>
#include <cstdint>

#include "srpc/common/observers/define.h"
#include "srpc/common/observers/simple.h"

#include "srpc/server/acceptor/interface.h"
#include "srpc/common/transport/interface.h"
#include "srpc/common/transport/types.h"

#include "application.h"
#include "subsys-listener2.h"

#include "noname-common.h"

namespace msctl { namespace agent { namespace noname {

namespace server {

    class interface {
    public:

        using error_code        = srpc::common::transport::error_code;
        using acceptor_type     = srpc::server::acceptor::interface;
        using transport_type    = srpc::common::transport::interface;
        using accept_call       = std::function<void ( transport_type *,
                                                       const std::string&,
                                                       std::uint16_t ) >;

        using accept_error      = std::function<void ( const error_code &) >;
        using close_call        = std::function<void ( ) >;

        virtual ~interface( ) { }
        virtual acceptor_type *acceptor(  ) = 0;
        virtual void start(  ) = 0;
        virtual void stop(  ) = 0;

        void assignt_accept_call( accept_call call )
        {
            accept_call_ = std::move(call);
        }

        void assignt_error_call( accept_error call )
        {
            error_call_ = std::move(call);
        }

        void assignt_close_call( close_call call )
        {
            close_call_ = std::move(call);
        }

    protected:
        accept_call  accept_call_;
        accept_error error_call_;
        close_call   close_call_;
    };

    using server_sptr = std::shared_ptr<interface>;
    using server_wptr = std::weak_ptr<interface>;

    namespace tcp {
        server_sptr create( application *app,
                            std::string addr, std::uint16_t port );
    }

    namespace udp {
        server_sptr create( application *app,
                            std::string addr, std::uint16_t port );
    }

}

}}}

#endif // NONAMESERVER_H
