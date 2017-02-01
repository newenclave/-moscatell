#ifndef MSCTL_NONAME_CLIENT_H
#define MSCTL_NONAME_CLIENT_H

#include <memory>
#include <cstdint>

#include "srpc/common/observers/define.h"
#include "srpc/common/observers/simple.h"

#include "srpc/client/connector/interface.h"
#include "srpc/common/transport/interface.h"
#include "srpc/common/transport/types.h"

#include "application.h"
#include "subsys-listener2.h"

#include "noname-common.h"

namespace msctl { namespace agent { namespace noname {

namespace client {

    using transport_type = srpc::common::transport::interface;

    struct interface: public srpc::enable_shared_from_this<interface> {

    public:

        using connect_call    = std::function<void(transport_type *)>;
        using error_call      = std::function<void(const error_code &)>;
        using disconnect_call = std::function<void( )>;

        using connector_type = srpc::client::connector::interface;

    public:

        virtual ~interface( ) { }

        virtual void start( ) = 0;
        virtual void stop(  ) = 0;

        virtual connector_type *connector( ) = 0;

        void assign_on_connect( connect_call call )
        {
            on_connect_ = std::move( call );
        }

        void assign_on_disconnect( disconnect_call call )
        {
            on_disconnect_ = std::move( call );
        }

        void assign_on_error( error_call call )
        {
            on_error_ = std::move( call );
        }

    protected:

        connect_call    on_connect_;
        disconnect_call on_disconnect_;
        error_call      on_error_;

    };

    using client_sptr = std::shared_ptr<interface>;
    using client_wptr = std::weak_ptr<interface>;

    namespace tcp {
        client_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port );
    }

    namespace ucp {
        client_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port );
    }
}

}}}
#endif // NONAMECLIENT_H
