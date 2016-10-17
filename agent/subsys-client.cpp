#include "subsys-client.h"

namespace msctl { namespace agent {

    struct client::impl {
        application *app_;
        client      *parent_;
    };

    client::client( application *app )
        :impl_(new impl)
    {
        impl_->app_ = app;
        impl_->parent_ = this;
    }

    void client::init( )
    { }

    void client::start( )
    { }

    void client::stop( )
    { }

}}
