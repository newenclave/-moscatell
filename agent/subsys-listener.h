
#ifndef SUBSYS_listener_H
#define SUBSYS_listener_H

#include "application.h"
#include "vtrc-common/vtrc-signal-declaration.h"

namespace msctl { namespace agent {

    class listener: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

        VTRC_DECLARE_SIGNAL( on_new_connection,
                             void ( vtrc::common::connection_iface *,
                                    const std::string & ) );

        VTRC_DECLARE_SIGNAL( on_stop_connection,
                             void ( vtrc::common::connection_iface * ));


    public:

        struct server_create_info {
            std::string                point;
            std::string                device;
            utilities::address_v4_poll addr_poll;
        };

        listener( application *app );
        static std::shared_ptr<listener> create( application *app );
        bool add_server( const server_create_info &inf, bool start );

    private:

        static const char* name( )
        {
            return "listener";
        }
        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_listener_H

    
