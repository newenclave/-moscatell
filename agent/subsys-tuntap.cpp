
#include "subsys-tuntap.h"


#define LOG(lev) log_(lev, "tuntap") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    struct tuntap::impl {
        application *app_;
        tuntap      *parent_;
        logger_impl &log_;
        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }
    };

    tuntap::tuntap( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }

    void tuntap::init( )
    { }

    void tuntap::start( )
    { 
        impl_->LOGINF << "Started.";
    }

    void tuntap::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<tuntap> tuntap::create( application *app )
    {
        return std::make_shared<tuntap>( app );
    }
}}
