
#ifndef SUBSYS_clients_H
#define SUBSYS_clients_H

#include "application.h"
#include "vtrc-common/vtrc-signal-declaration.h"
#include "vtrc-client/vtrc-client-base.h"

#include "common/parameter.h"

namespace msctl { namespace agent {

    class clients: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        using client_param_sptr = utilities::parameter_sptr;
        using client_param_map  = std::map<std::string, client_param_sptr>;

        struct client_create_info {
            std::string         point;
            std::string         device;
            bool                tcp_nowait;
            std::string         id;
            client_param_map    params;
        };

        struct register_info {
            std::string ip;
            std::string mask;
            std::string server_ip;
        };

        VTRC_DECLARE_SIGNAL( on_client_ready,
                             void( vtrc::client::base_sptr,
                                   const client_create_info & ) );

        VTRC_DECLARE_SIGNAL( on_client_register,
                             void( vtrc::client::base_sptr,
                                   const client_create_info &,
                                   const register_info & ) );

        VTRC_DECLARE_SIGNAL( on_client_disconnect,
                             void( vtrc::client::base_sptr,
                                   const client_create_info &) );

    public:

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

    
