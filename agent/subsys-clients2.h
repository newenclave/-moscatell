
#ifndef SUBSYS_clients2_H
#define SUBSYS_clients2_H

#include "application.h"
#include "common/create-params.h"

namespace msctl { namespace agent {

    class clients2: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        struct client_create_info {
            std::string               name;
            std::string               point;
            std::string               device;
            std::string               id;
            common::create_parameters common;
            bool                      udp = false;
        };

        struct register_info {
            std::string ip;
            std::string mask;
            std::string server_ip;
        };

        clients2( application *app );
        static std::shared_ptr<clients2> create( application *app );
        static const char *name( ) 
        {
            return "clients2";
        }

        bool add_client( const client_create_info &inf, bool start );

    private:

        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_clients2_H

    
