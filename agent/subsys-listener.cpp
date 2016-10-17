
#include "subsys-listener.h"

namespace msctl { namespace agent {

    struct listener::impl {
        application *app_;
        listener    *parent_;
    };

    listener::listener( application *app )
        :impl_(new impl)
    {
        impl_->app_ = app;
        impl_->parent_ = this;
    }

    void listener::init( )
    { }

    void listener::start( )
    { }

    void listener::stop( )
    { }

}}

		
