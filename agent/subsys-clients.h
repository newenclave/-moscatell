
#ifndef SUBSYS_clients_H
#define SUBSYS_clients_H

#include "application.h"
#include "vtrc-common/vtrc-signal-declaration.h"
#include "vtrc-client/vtrc-client-base.h"

namespace msctl { namespace agent {

    class clients: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

        VTRC_DECLARE_SIGNAL( on_client_ready,
                             void( vtrc::client::base_sptr,
                                   const std::string &dev ) );

        VTRC_DECLARE_SIGNAL( on_client_disconnect,
                             void( vtrc::client::base_sptr ) );

    public:

        struct client_create_info {
            std::string point;
            std::string device;
            bool        tcp_nowait;
        };

        clients( application *app );
        static std::shared_ptr<clients> create( application *app );
        static const char *name( ) 
        {
            return "clients";
        }

        bool add_client( const client_create_info &inf, bool start );

    private:

        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_clients_H

    
