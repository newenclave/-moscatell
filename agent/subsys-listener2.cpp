
#include "subsys-listener2.h"

#include "noname-server.h"

#include "common/tuntap.h"
#include "common/utilities.h"
#include "common/net-ifaces.h"

#define LOG(lev) log_(lev, "listener2") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

namespace {

    template<typename T>
    std::uintptr_t uint_cast( const T *val )
    {
        return reinterpret_cast<std::uintptr_t>(val);
    }

    struct server_device;

    struct calls_impl: public listener2::calls {

        using register_info = noname::client_info::register_info;

        calls_impl( noname::client_sptr client )
            :client_(client)
        { }

        ~calls_impl( )
        {
            //std::cout << "calls_impl dtor" << std::endl;
        }

        void register_me( register_info &reg );
        void register_ok( register_info & );
        void push( const char *data, size_t len );
        void ping( );

        noname::client_sptr                         client_;
        std::shared_ptr<common::tuntap_transport>   my_dev_;
    };

    using client_info = std::shared_ptr<calls_impl>;

    /// works with routes
    /// one device -> many clients
    struct server_device: public common::tuntap_transport {

        using parent_type = common::tuntap_transport;
        using index_type = std::uintptr_t;
        using client_map = std::map<index_type, client_info>;

        server_device( application *app, size_t readlen )
            :common::tuntap_transport(app->get_io_service( ), readlen,
                                      parent_type::OPT_DISPATCH_READ)
        { }

        void add_client( std::uintptr_t id, client_info c )
        {
            auto call = [this, id, c]( )
            {
                std::cout << "add client" << uint_cast(c.get( )) << std::endl;
                clients_[id] = c;
            };
            get_dispatcher( ).post( call );
        }

        void del_client( std::uintptr_t id )
        {
            auto call = [this, id]( )
            {
                auto f = clients_.find( id );
                if( f != clients_.end( ) ) {
                    f->second->client_.reset( );
                    f->second->my_dev_.reset( );
                    clients_.erase( f );
                }
            };
            get_dispatcher( ).post( call );
        }

        void on_read( char *data, size_t length )
        {

        }

        client_map          clients_;
        noname::server::server_sptr server_;
    };


//////////////////// calls_impl
    void calls_impl::register_me( register_info &reg )
    {

    }

    void calls_impl::register_ok( register_info & )
    { }

    void calls_impl::push( const char *data, size_t len )
    {
        my_dev_->write( data, len );
    }

    void calls_impl::ping( )
    {

    }
    //////////////////// calls_impl

    using server_device_sptr = std::shared_ptr<server_device>;
}

    struct listener2::impl {

        using device_map = std::map<std::string, server_device_sptr>;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void on_new_client( server_device *s, noname::client_sptr c,
                            const std::string &addr, std::uint16_t port )
        {
            auto cl = std::make_shared<calls_impl>( c );
            cl->my_dev_ = s->shared_from_this( );
            c->set_calls( cl );
            s->add_client( uint_cast(c.get( )), cl );
            c->start( );

            LOGINF << "Client " << addr << ":" << port << " connected";
        }

        void on_del_client( server_device *s, noname::client_sptr c )
        {
            s->del_client( uint_cast(c.get( ) ) );
            LOGINF << "Client " << uint_cast(c.get( ))
                   << " disconnected";
        }

        void on_close_server( server_device *s )
        {

        }

        bool add_server( const listener2::server_create_info &inf, bool start )
        {
            auto ep = utilities::get_endpoint_info( inf.point );

            server_device_sptr  device =
                    std::make_shared<server_device>( app_, 2048 );

            noname::server::server_sptr res;

            if( ep.is_ip( ) ) {

                namespace nudp = noname::server::udp;
                namespace ntcp = noname::server::tcp;

                res = inf.udp ? nudp::create( app_, ep.addpess, ep.service )
                              : ntcp::create( app_, ep.addpess, ep.service );

                {
                    std::lock_guard<std::mutex> l(devices_lock_);
                    auto f = devices_.find( inf.device );
                    if( f == devices_.end( ) ) {
                        devices_[inf.device] = device;
                        device->server_ = res;
                    } else {
                        res.reset( );
                        LOGERR << "Server for '"
                               << inf.device << "' is already defined; '"
                               << f->first << "'";
                        return false;
                    }

                }

                auto dev = device.get( );
                if( res ) {
                    res->subscribe_on_accept(
                        [this, dev]( noname::client_sptr c,
                                     const std::string &addr,
                                     std::uint16_t port )
                        {
                            on_new_client( dev, c, addr, port );
                        } );

                    res->subscribe_on_client_close(
                        [this, dev]( noname::client_sptr c )
                        {
                            on_del_client( dev, c );
                        } );

                    res->subscribe_on_close(
                        [this, dev]( )
                        {
                            on_close_server( dev );
                        } );
                    if( start ) {
                        res->start( );
                        device->start_read( );
                    }
                }
            } else {
                LOGERR << "Failed to add endpoint '"
                       << inf.point << "'; Bad format";
                return false;
            }
            return true;

        }

        void start_all( )
        {
            std::lock_guard<std::mutex> l(devices_lock_);
            for( auto &d: devices_ ) {
                d.second->server_->start( );
                d.second->start_read( );
            }
        }

        application  *app_;
        listener2    *parent_;
        logger_impl  &log_;

        device_map    devices_;
        std::mutex    devices_lock_;
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

