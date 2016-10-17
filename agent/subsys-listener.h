
#ifndef SUBSYS_listener_H
#define SUBSYS_listener_H

#include "application.h"

namespace msctl { namespace agent {

    class listener: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        listener( application *app );
        static std::shared_ptr<listener> create( application *app );

    private:

        std::string name( ) const override
        {
            return "listener";
        }
        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_listener_H

    
