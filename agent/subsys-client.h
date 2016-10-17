#ifndef SUBSYS_CLIENT_H
#define SUBSYS_CLIENT_H

#include "application.h"

namespace msctl { namespace agent {

    class client: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        client( application *app );

    private:

        std::string name( ) const override
        {
            return "client";
        }
        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif // SUBSYS_CLIENT_H
