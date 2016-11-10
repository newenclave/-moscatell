
#ifndef SUBSYS_listener_H
#define SUBSYS_listener_H

#include "application.h"
#include "vtrc-common/vtrc-signal-declaration.h"

#include "common/parameter.h"
#include "lowlevel-protocol-server.h"

namespace msctl { namespace agent {

    class listener: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        using listener_param_sptr = utilities::parameter_sptr;
        using listener_param_map  = std::map<std::string, listener_param_sptr>;

        struct server_create_info {
            std::string                     point;
            std::string                     device;
            utilities::address_v4_poll      addr_poll;
            std::uint32_t                   max_queue = 10;
            bool                            tcp_nowait = false;
            lowlevel::server_proto_option   ll_opts; /// options for lowlevel
            listener_param_map              params;
        };

        struct register_info {
            std::string ip;
            std::string mask;
            std::string my_ip;
        };

        VTRC_DECLARE_SIGNAL( on_new_connection,
                             void ( vtrc::common::connection_iface *,
                                    const server_create_info & ) );

        VTRC_DECLARE_SIGNAL( on_reg_connection,
                             void ( vtrc::common::connection_iface *,
                                    const listener::server_create_info &,
                                    const register_info & ) );

        VTRC_DECLARE_SIGNAL( on_stop_connection,
                             void ( vtrc::common::connection_iface *,
                                    const listener::server_create_info & ) );


    public:

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

    
