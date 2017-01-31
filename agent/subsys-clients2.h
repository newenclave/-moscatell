
#ifndef SUBSYS_clients2_H
#define SUBSYS_clients2_H

#include "application.h"

namespace msctl { namespace agent {

    class clients2: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        struct calls {
            virtual void register_ok( ) = 0;
            virtual void push( ) = 0;
            virtual void ping( ) = 0;
        };

        clients2( application *app );
        static std::shared_ptr<clients2> create( application *app );
        static const char *name( ) 
        {
            return "clients2";
        }

    private:

        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_clients2_H

    
