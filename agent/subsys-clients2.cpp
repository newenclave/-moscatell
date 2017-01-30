
#include "subsys-clients2.h"


#define LOG(lev) log_(lev, "clients2") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    struct clients2::impl {
        application *app_;
        clients2      *parent_;
        logger_impl &log_; 
        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }
    };

    clients2::clients2( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }

    void clients2::init( )
    { }

    void clients2::start( )
    { 
        impl_->LOGINF << "Started.";
    }

    void clients2::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<clients2> clients2::create( application *app )
    {
        return std::make_shared<clients2>( app );
    }
}}

		