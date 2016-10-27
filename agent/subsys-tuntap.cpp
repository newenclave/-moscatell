#include <memory>

#include "subsys-tuntap.h"
#include "subsys-clients.h"
#include "subsys-listener.h"

#include "protocol/tuntap.pb.h"
#include "common/tuntap.h"

#include "vtrc-common/vtrc-closure-holder.h"
#include "vtrc-common/vtrc-rpc-service-wrapper.h"
#include "vtrc-common/vtrc-stub-wrapper.h"
#include "vtrc-common/vtrc-mutex-typedefs.h"
#include "vtrc-server/vtrc-channels.h"

#define LOG(lev) log_(lev, "tuntap") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent {

    namespace vcomm = vtrc::common;
    namespace vclnt = vtrc::client;
    namespace vserv = vtrc::server;
    namespace ba    = boost::asio;

    namespace {
        logger_impl *gs_logger = nullptr;

    }

    struct tuntap::impl {

        application  *app_;
        tuntap       *parent_;
        logger_impl  &log_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        {
            gs_logger = &log_;
        }


        void init( )
        {

        }

    };

    tuntap::tuntap( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }

    void tuntap::init( )
    {
        impl_->init( );
    }

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
