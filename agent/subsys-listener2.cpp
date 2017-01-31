
#include "subsys-listener2.h"

#include "noname-server.h"

#include "common/tuntap.h"
#include "common/utilities.h"
#include "common/net-ifaces.h"

#include "protocol/tuntap.pb.h"

#define LOG(lev) log_(lev, "listener2") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    struct listener2::impl {

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        bool add_server( const listener2::server_create_info &inf, bool start )
        {

        }

        void start_all( )
        {

        }

        application  *app_;
        listener2    *parent_;
        logger_impl  &log_;
    };

    listener2::listener2( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }

    void listener2::init( )
    { }

    void listener2::start( )
    { 
        impl_->start_all( );
        impl_->LOGINF << "Started.";
    }

    void listener2::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<listener2> listener2::create( application *app )
    {
        return std::make_shared<listener2>( app );
    }

    bool listener2::add_server( const server_create_info &inf, bool start )
    {
        return impl_->add_server( inf, start );
    }

}}

