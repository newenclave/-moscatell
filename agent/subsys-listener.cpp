
#include "subsys-listener.h"


#define LOG(lev) log_(lev, "listener")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    struct listener::impl {
        application *app_;
        listener    *parent_;
        logger_impl &log_;
        impl( logger_impl &log )
            :log_(log)
        { }
    };

    listener::listener( application *app )
        :impl_(new impl(app->log( )))
    {
        impl_->app_ = app;
        impl_->parent_ = this;
    }

    void listener::init( )
    { }

    void listener::start( )
    {
        impl_->LOGINF << "Started.";
    }

    void listener::stop( )
    {
        impl_->LOGINF << "Stopped.";
    }

    std::shared_ptr<listener> listener::create( application *app )
    {
        return std::make_shared<listener>( app );
    }

}}

