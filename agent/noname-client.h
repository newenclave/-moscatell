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

namespace msctl { namespace agent { namespace noname {

namespace client {

    struct interface: public srpc::enable_shared_from_this<interface> {
    public:
        using connector_type = srpc::client::connector::interface;

        SRPC_OBSERVER_DEFINE( on_ready,     void(bool) );
        SRPC_OBSERVER_DEFINE( on_connect,   void(bool) );
        SRPC_OBSERVER_DEFINE( on_dictonect, void(bool) );

    public:

        virtual ~interface( ) { }

        virtual void start( ) = 0;
        virtual void stop(  ) = 0;

        virtual connector_type *connector( ) = 0;

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
