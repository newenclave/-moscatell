#include <mutex>

#include "subsys-listener.h"

#include "vtrc-server/vtrc-listener.h"
#include "vtrc-server/vtrc-listener-tcp.h"
#include "vtrc-server/vtrc-listener-local.h"

#include "vtrc-system.h"

#define LOG(lev) log_(lev, "listener")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent {

    namespace vcomm = vtrc::common;
    namespace vserv = vtrc::server;

    struct listener_info {
        vserv::listener_sptr point;
        std::string          device;

        listener_info( ) = default;

        listener_info( vserv::listener_sptr p, const std::string &d )
            :point(p)
            ,device(d)
        { }

        listener_info &operator = ( const listener_info &other )
        {
            point  = other.point;
            device = other.device;
            return *this;
        }
    };

    using listeners_map = std::map<std::string, listener_info>;

    struct listener::impl {

        application     *app_;
        listener        *parent_;
        logger_impl     &log_;

        listeners_map    points_;
        std::mutex       points_lock_;

        impl( logger_impl &log )
            :log_(log)
        { }

        void on_start( const std::string &p )
        {
            LOGINF << "Point '" << p << "' was started.";
                      ;
        }

        void on_stop( const std::string &p )
        {
            LOGINF << "Point '" << p << "' was stopped.";
                      ;
        }

        void on_accept_failed( const VTRC_SYSTEM::error_code &err,
                               const std::string &p, const std::string &d )
        {
            LOGERR << "Accept failed on listener: '" << p
                   << "' assigned to device '" << d << "'; "
                   << "Error: " << err.value( )
                   << " (" << err.message( ) << ")"
                      ;
        }

        void on_new_connection( vcomm::connection_iface *c,
                                const std::string &dev )
        {
            LOGINF << "Add connection: '" << c->name( ) << "'"
                   << " for device '" << dev << "'";
            parent_->get_on_new_connection( )( c, dev );
        }

        void on_stop_connection( vcomm::connection_iface *c,
                                 const std::string & /*dev*/ )
        {
            LOGINF << "Remove connection: '" << c->name( ) << "'";
            parent_->get_on_stop_connection( )( c );
        }

        bool add( const std::string &point, const std::string &dev )
        {
            using namespace vserv::listeners;

            auto inf = utilities::get_endpoint_info( point );
            vserv::listener_sptr res;

            if( inf.is_local( ) ) {
                res = local::create( *app_, inf.addpess );
            } else if( inf.is_ip( ) ) {
                res = tcp::create( *app_, inf.addpess, inf.service, true );
            } else {
                LOGERR << "Failed to add endpoint '"
                       << point << "'; Bad format";
            }

            if( res ) {

                LOGINF << "Adding '" << point << "' for device " << dev;

                res->on_start_connect(
                    [this, point](  ) {
                        this->on_start( point );
                    } );

                res->on_stop_connect (
                    [this, point](  ) {
                        this->on_stop( point );
                    } );

                res->on_accept_failed_connect(
                    [this, point, dev]( const VTRC_SYSTEM::error_code &err ) {
                        this->on_accept_failed( err, point, dev );
                    } );

                res->on_new_connection_connect(
                    [this, dev]( vcomm::connection_iface *c ) {
                        this->on_new_connection( c, dev );
                    } );

                res->on_stop_connection_connect(
                    [this, dev]( vcomm::connection_iface *c ) {
                        this->on_stop_connection( c, dev );
                    } );

                std::lock_guard<std::mutex> lck(points_lock_);
                points_[point] = listener_info( res, dev );
                return true;
            }

            return false;
        }

        void start_all( )
        {
            std::lock_guard<std::mutex> lck(points_lock_);
            for( auto &p: points_ ) {
                LOGINF << "Starting '" << p.first << "'...";
                p.second.point->start( );
                LOGINF << "Ok.";
            }
        }
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

    bool listener::add_server(const std::string &point, const std::string &dev)
    {
        return impl_->add( point, dev );
    }

    void listener::start_all( )
    {
        return impl_->start_all(  );
    }

}}

