
#ifndef SUBSYS_tuntap_H
#define SUBSYS_tuntap_H

#include "application.h"
#include "common/tuntap.h"

namespace msctl { namespace agent {

    class tuntap: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        tuntap( application *app );
        static std::shared_ptr<tuntap> create( application *app );

        void open_tun( const std::string &name );

    private:

        static const char* name( )
        {
            return "tuntap";
        }
        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_tuntap_H

    
