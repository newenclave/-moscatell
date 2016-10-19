
#include "subsys-tuntap.h"
#include "subsys-clients.h"
#include "subsys-listener.h"

#define LOG(lev) log_(lev, "tuntap") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

    namespace vcomm = vtrc::common;
    namespace vclnt = vtrc::client;
    namespace vserv = vtrc::client;

    struct tuntap::impl {
        application *app_;
        tuntap      *parent_;
        logger_impl &log_;
        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void add_client_point( vclnt::base_sptr c, const std::string &dev )
        {

        }

        void del_client_point( vclnt::base_sptr c )
        {

        }

        void add_server_point( vcomm::connection_iface *c,
                               const std::string &dev )
        {

        }

        void del_server_point( vcomm::connection_iface *c )
        {

        }

        void init( )
        {
            auto &lst(app_->subsys<listener>( ) );
            auto &cln(app_->subsys<clients>( ) );

            lst.on_new_connection_connect(
                [this]( vcomm::connection_iface *c, const std::string &dev ) {
                    this->add_server_point( c, dev );
                } );

            lst.on_stop_connection_connect(
                [this]( vcomm::connection_iface *c ) {
                    this->del_server_point( c );
                } );

            cln.on_client_ready_connect(
                [this]( vclnt::base_sptr c, const std::string &dev ) {
                    this->add_client_point( c, dev );
                } );

            cln.on_client_disconnect_connect(
                [this]( vclnt::base_sptr c ) {
                    this->del_client_point( c );
                } );
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
