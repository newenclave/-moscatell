#include <mutex>

#include "subsys-listener.h"

#include "vtrc-server/vtrc-listener.h"
#include "vtrc-server/vtrc-listener-tcp.h"
#include "vtrc-server/vtrc-listener-local.h"

#include "vtrc-system.h"

#include "protocol/tuntap.pb.h"

#include "vtrc-common/vtrc-closure-holder.h"
#include "vtrc-common/vtrc-rpc-service-wrapper.h"
#include "vtrc-common/vtrc-stub-wrapper.h"
#include "vtrc-common/vtrc-mutex-typedefs.h"
#include "vtrc-common/vtrc-delayed-call.h"

#include "vtrc-server/vtrc-channels.h"

#include "common/tuntap.h"
#include "common/utilities.h"

#define LOG(lev) log_(lev, "listener")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent {

namespace {

    namespace vcomm = vtrc::common;
    namespace vserv = vtrc::server;
    namespace ba    = boost::asio;
    namespace bs    = boost::system;
    namespace gpb   = google::protobuf;

    using delayed_call = vcomm::delayed_call;
    using utilities::decorators::quote;

    logger_impl *gs_logger = nullptr;

    struct listener_info {
        vserv::listener_sptr point;
        std::string          device;
        utilities::address_v4_poll addr_poll;

        listener_info( ) = default;

        listener_info( vserv::listener_sptr p,
                       const listener::server_create_info &inf )
            :point(p)
            ,device(inf.device)
            ,addr_poll(inf.addr_poll)
        { }

        listener_info &operator = ( const listener_info &other )
        {
            point  = other.point;
            device = other.device;
            return *this;
        }
    };

    using listeners_map  = std::map<std::string, listener_info>;
    using server_stub    = rpc::tuntap::client_instance_Stub;
    using server_wrapper = vcomm::stub_wrapper<server_stub,
                                               vcomm::rpc_channel>;
    using server_wrapper_sptr = std::shared_ptr<server_wrapper>;


    /// works with routes
    /// one device -> many clients
    class server_transport: public common::tuntap_transport {

        std::queue<std::uint32_t>    free_ip_;
        utilities::address_v4_poll   poll_;

        struct client_info {
            vcomm::connection_iface *connection = nullptr;
            std::uint32_t            address;
            server_wrapper_sptr      client;
        };

        using client_info_sptr = std::shared_ptr<client_info>;
        std::map<std::uintptr_t, client_info_sptr> clients_;
        std::map<std::uint32_t,  client_info_sptr> routes_;

    public:

        using route_map = std::map<ba::ip::address, server_wrapper>;
        using parent_type = common::tuntap_transport;
        using empty_callback = std::function<void ()>;

        using route_table = std::map<std::uint32_t, server_wrapper_sptr>;

        server_transport( ba::io_service &ios,
                          const utilities::address_v4_poll &poll )
            :parent_type(ios, 2048, parent_type::OPT_DISPATCH_READ)
            ,poll_(poll)
        {
//            //// TODO fix!
//            in_addr addr;
//            inet_aton( "192.168.0.0", &addr );
//            in_addr mask;
//            inet_aton( "255.255.255.0", &mask );
//            poll_ = utilities::address_v4_poll( ntohl(addr.s_addr),
//                                                ntohl(mask.s_addr) );
        }

        void on_read( const char *data, size_t length ) override
        {
            rpc::tuntap::push_req req;
            req.set_value( data, length );
#ifdef _WIN32
#else
            auto srcdst = common::extract_ip_v4( data, length );
            if( srcdst.second ) {
                auto f = routes_.find( srcdst.second );
                if( f != routes_.end( ) ) {
                    f->second->client
                     ->call_request( &server_stub::push, &req );
                }
            }
#endif

        }        

        using shared_type = std::shared_ptr<server_transport>;

        void add_client_impl( vcomm::connection_iface_wptr clnt,
                              gpb::RpcController*                controller,
                              const ::msctl::rpc::tuntap::register_req* req,
                              ::msctl::rpc::tuntap::register_res*       res,
                              gpb::Closure *done, empty_callback cb )
        {
            using vserv::channels::unicast::create_event_channel;
            vcomm::closure_holder done_holder( done );

            auto clntptr = clnt.lock( );
            if( !clntptr ) {
                return;
            }

            std::uint32_t next_addr = 0;
            std::uint32_t next_mask = htonl(poll_.mask( ));

            if( !free_ip_.empty( ) ) {
                next_addr = free_ip_.front( );
                free_ip_.pop( );
            } else {
                next_addr = poll_.next( );
            }

            if( next_addr == 0 ) {
                controller->SetFailed( "Server is full." );
                return;
            }

            auto next_client_info = std::make_shared<client_info>( );
            next_client_info->connection  = clntptr.get( );
            next_client_info->address     = next_addr;

            auto chan = std::make_shared<server_wrapper>
                                (create_event_channel(clntptr), true );
            chan->channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT );

            auto weak_this = weak_type(shared_from_this( ));
            auto c_ptr = clntptr.get( );

            auto error_cb = [this, weak_this, c_ptr]( const char *mess ) {
                auto this_lock = weak_this.lock( );
                if( this_lock ) {
                    logger_impl &log_(*gs_logger);
                    LOGERR << "Channel error for client " << std::hex
                           << "0x" << c_ptr << " "
                           << quote(mess)
                           << ". Removing...";
                    this->del_client_impl( c_ptr );
                }
            };

            chan->channel( )->set_channel_error_callback( error_cb );

            next_client_info->client = chan;

            routes_[htonl(next_addr)]
                    = clients_[reinterpret_cast<std::uintptr_t>(c_ptr)]
                    = next_client_info;

            auto str_addr = ba::ip::address_v4( next_addr );
            auto str_mask = ba::ip::address_v4( htonl(next_mask) );
            logger_impl &log_(*gs_logger);

            LOGINF << "Set client address: " << str_addr.to_string( )
                   << " with mask " << str_mask.to_string( );

            res->mutable_iface_addr( )->set_v4_address( htonl(next_addr) );
            res->mutable_iface_addr( )->set_v4_mask( next_mask );
            cb( );
        }

        void add_client( vcomm::connection_iface *clnt,
                         gpb::RpcController*                controller,
                         const ::msctl::rpc::tuntap::register_req* req,
                         ::msctl::rpc::tuntap::register_res*       res,
                         gpb::Closure *done, empty_callback cb )
        {
            auto wclnt = clnt->weak_from_this( );
            dispatch( [this, wclnt, controller, req, res, done, cb]( ) {
                add_client_impl( wclnt, controller, req, res, done, cb );
            } );
        }

        void del_client_impl( vcomm::connection_iface *clnt )
        {
            auto id = reinterpret_cast<std::uintptr_t>(clnt);
            auto c = clients_.find( id );
            if( c != clients_.end( ) ) {
                routes_.erase(htonl(c->second->address));
                if( c->second->address == poll_.current( ) ) {
                    poll_.drop( );
                } else {
                    free_ip_.push( c->second->address );
                }
                clients_.erase( c );
            }
        }

        void del_client( vcomm::connection_iface *clnt  )
        {
            dispatch( [this, clnt ]( ) {
                del_client_impl( clnt );
            } );
        }

        static
        shared_type create( application *app,
                            const listener::server_create_info &inf )
        {
            using std::make_shared;
            auto dev  = common::open_tun( inf.device );
            auto inst = make_shared<server_transport>
                                ( app->get_io_service( ), inf.addr_poll );
            inst->get_stream( ).assign( dev.release( ));
            return inst;
        }

    };

    struct device_info {
        std::string                   name;
        server_transport::shared_type transport;
    };

    using device_info_sptr = std::shared_ptr<device_info>;

    using remote_map = std::map<std::uintptr_t, device_info_sptr>;
    using device_map = std::map<std::string,    device_info_sptr>;
    namespace unichannels = vserv::channels::unicast;

    class svc_impl: public rpc::tuntap::server_instance {

        using push_call = std::function<
                            void ( gpb::RpcController*,
                                   const ::msctl::rpc::tuntap::push_req*,
                                   ::msctl::rpc::tuntap::push_res*,
                                   ::google::protobuf::Closure* done)>;

        application                  *app_;
        vcomm::connection_iface      *client_;
        device_info_sptr              device_;
        delayed_call                  keep_alive_;
        std::uint64_t                 ticks_;
        server_wrapper                swrap_;
        push_call                     pusher_;

    public:

        using parent_type = rpc::tuntap::server_instance;

        svc_impl( application *app,
                  vcomm::connection_iface_wptr client,
                  device_info_sptr device )
            :app_(app)
            ,client_(client.lock( ).get( ))
            ,device_(device)
            ,keep_alive_(app->get_rpc_service( ))
            ,ticks_(application::tick_count( ))
            ,swrap_(unichannels::create_event_channel(client.lock( )), true)
        {
            namespace ph = std::placeholders;
            pusher_ = std::bind( &svc_impl::push_disconnect, this,
                                 ph::_1, ph::_2, ph::_3, ph::_4 );
            start_timer( 30 );
        }
        void start_timer( std::uint32_t sec )
        {
            auto wclient = client_->weak_from_this( );
            auto handler = [this, wclient]( const bs::error_code &err ) {
                this->keep_alive( err, wclient );
            };

            keep_alive_.call_from_now( handler, delayed_call::seconds( sec ) );
        }

        void keep_alive( const boost::system::error_code &err,
                         vcomm::connection_iface_wptr clnt )
        {
            auto &log_(*gs_logger);
            if( !err ) {
                auto lck = clnt.lock( );
                if( lck ) {
                    auto current = application::tick_count( );
                    if( (current - ticks_) >= 60000000 ) { /// 1 minute
                        LOGWRN << "Keep alive timer...Client disconnected.";
                        lck->close( );
                    } else {
                        //LOGDBG << "Keep alive timer...Pinging client...";
                        swrap_.call( &server_stub::ping );
                        start_timer( 30 );
                    }
                }
            }
        }

        ~svc_impl( )
        { }

        static std::string name( )
        {
            return parent_type::descriptor( )->full_name( );
        }

        void register_me( ::google::protobuf::RpcController* controller,
                          const ::msctl::rpc::tuntap::register_req* request,
                          ::msctl::rpc::tuntap::register_res* response,
                          ::google::protobuf::Closure* done ) override
        {
            auto cb = [this]( ){
                namespace ph = std::placeholders;
                pusher_ = std::bind( &svc_impl::push_default, this,
                                     ph::_1, ph::_2, ph::_3, ph::_4 );
            };
            device_->transport->add_client( client_, controller,
                                            request, response, done, cb );
        }

        void push_disconnect( ::google::protobuf::RpcController*,
                           const ::msctl::rpc::tuntap::push_req*,
                           ::msctl::rpc::tuntap::push_res*,
                           ::google::protobuf::Closure* done )
        {
            vcomm::closure_holder done_holder( done );
            client_->close( ); // hasta luego, violador!
        }

        void push_default( ::google::protobuf::RpcController*   /*controller*/,
                           const ::msctl::rpc::tuntap::push_req* request,
                           ::msctl::rpc::tuntap::push_res*      /*response*/,
                           ::google::protobuf::Closure* done )
        {
            vcomm::closure_holder done_holder( done );

            device_->transport
                   ->write_post_notify( request->value( ),
                     [ ](const boost::system::error_code &err)
                     {
                         if( err ) {
                             //std::cout << "" << err.message( ) << "\n";
                         }
                     } );
        }

        void push( ::google::protobuf::RpcController*   controller,
                   const ::msctl::rpc::tuntap::push_req* request,
                   ::msctl::rpc::tuntap::push_res*      response,
                   ::google::protobuf::Closure* done) override
        {
            pusher_(controller, request, response, done);
        }

        void ping( ::google::protobuf::RpcController* /*controller*/,
                   const ::msctl::rpc::empty* /*request*/,
                   ::msctl::rpc::empty* /*response*/,
                   ::google::protobuf::Closure* done) override
        {
            vcomm::closure_holder done_holder( done );
            ticks_ = application::tick_count( );
        }
    };

    application::service_wrapper_sptr create_service(
                                  application *app,
                                  vcomm::connection_iface_wptr cl,
                                  device_info_sptr dev )
    {
        auto inst = std::make_shared<svc_impl>( app, cl, dev );
        return app->wrap_service( cl, inst );
    }

}
    struct listener::impl {

        application     *app_;
        listener        *parent_;
        logger_impl     &log_;

        listeners_map    points_;
        std::mutex       points_lock_;

        remote_map          remote_;
        device_map          devices_;
        vtrc::shared_mutex  remote_lock_;

        impl( logger_impl &log )
            :log_(log)
        {
            gs_logger = &log_;
        }

        void add_server_point( vcomm::connection_iface *c,
                               const listener::server_create_info &dev )
        {
            auto id = reinterpret_cast<std::uintptr_t>(c);
            vtrc::upgradable_lock lck(remote_lock_);
            auto f  =  devices_.find( dev.device );
            device_info_sptr info;
            if( f != devices_.end( ) ) {
                /// ok there is one device

                info = f->second;
                vtrc::upgrade_to_unique ulck(lck);
                remote_[id] = info;

            } else {
                /// ok there is no device

                info = std::make_shared<device_info>( );
                info->name      = dev.device;
                info->transport = server_transport::create( app_, dev );
                if( info->transport ) {

                    info->transport->start_read( );

                    vtrc::upgrade_to_unique ulck(lck);
                    devices_[dev.device] = remote_[id] = info;

                } else {
                    LOGERR << "Failed to open device: " << errno;
                }
            }
        }

        void del_server_point( vcomm::connection_iface *c )
        {
            auto id = reinterpret_cast<std::uintptr_t>(c);
            vtrc::upgradable_lock lck(remote_lock_);
            auto f = remote_.find( id);
            if( f != remote_.end( ) ) {
                f->second->transport->del_client( c ); /// remove from transport
                vtrc::upgrade_to_unique ulck(lck);
                remote_.erase( f );                    /// remove from endpoints
            }
        }

        void on_start( const listener::server_create_info &p )
        {
            LOGINF << "Point " << quote(p.point) << " was started.";
                      ;
        }

        void on_stop( const listener::server_create_info &p )
        {
            LOGINF << "Point " << quote(p.point) << " was stopped.";
                      ;
        }

        void on_accept_failed( const VTRC_SYSTEM::error_code &err,
                               const listener::server_create_info &inf )
        {
            LOGERR << "Accept failed on listener: " << quote(inf.point)
                   << "' assigned to device " << quote(inf.device) << "; "
                   << "Error: " << err.value( )
                   << " (" << err.message( ) << ")"
                      ;
        }

        void on_new_connection( vcomm::connection_iface *c,
                                const listener::server_create_info &dev )
        {
            LOGINF << "Add connection: " << quote(c->name( ))
                   << " for device " << quote(dev.device);
            add_server_point( c, dev );
            parent_->get_on_new_connection( )( c, dev.device );
        }

        void on_stop_connection( vcomm::connection_iface *c,
                                 const listener::server_create_info & /*dev*/ )
        {
            LOGINF << "Remove connection: " << quote(c->name( ));
            del_server_point( c );
            parent_->get_on_stop_connection( )( c );
        }

        bool add( const listener::server_create_info &serv_info, bool s )
        {
            using namespace vserv::listeners;

            auto &point(serv_info.point);
            auto &dev(serv_info.device);

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

                LOGINF << "Adding "      << quote(point)
                       << " for device " << quote(dev);

                res->on_start_connect(
                    [this, serv_info](  ) {
                        this->on_start( serv_info );
                    } );

                res->on_stop_connect (
                    [this, serv_info](  ) {
                        this->on_stop( serv_info );
                    } );

                res->on_accept_failed_connect(
                    [this, serv_info]( const VTRC_SYSTEM::error_code &err ) {
                        this->on_accept_failed( err, serv_info );
                    } );

                res->on_new_connection_connect(
                    [this, serv_info]( vcomm::connection_iface *c ) {
                        this->on_new_connection( c, serv_info );
                    } );

                res->on_stop_connection_connect(
                    [this, serv_info]( vcomm::connection_iface *c ) {
                        this->on_stop_connection( c, serv_info );
                    } );

                if( s ) {
                    res->start( );
                }

                std::lock_guard<std::mutex> lck(points_lock_);
                points_[point] = listener_info( res, serv_info );
                return true;
            }

            return false;
        }

        void start_all( )
        {
            std::lock_guard<std::mutex> lck(points_lock_);
            for( auto &p: points_ ) {
                LOGINF << "Starting " << quote(p.first) << "...";
                p.second.point->start( );
                LOGINF << "Ok.";
            }
        }

        void reg_creator( )
        {
            using res_type = application::service_wrapper_sptr;
            auto creator = [this]( application *app,
                                   vcomm::connection_iface_wptr cl ) -> res_type
            {
                auto id = reinterpret_cast<std::uintptr_t>(cl.lock( ).get( ));

                vtrc::shared_lock lck(remote_lock_);
                auto f  =  remote_.find( id );
                return f == remote_.end( )
                        ? res_type( )
                        : create_service( app, cl, f->second );
            };

            app_->register_service_factory( svc_impl::name( ), creator );
        }

        void unreg_creator( )
        {
            app_->unregister_service_factory( svc_impl::name( ) );
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
        impl_->start_all( );
        impl_->reg_creator( );
        impl_->LOGINF << "Started.";
    }

    void listener::stop( )
    {
        impl_->unreg_creator( );
        impl_->LOGINF << "Stopped.";
    }

    std::shared_ptr<listener> listener::create( application *app )
    {
        return std::make_shared<listener>( app );
    }

    bool listener::add_server( const server_create_info &inf, bool start )
    {
        return impl_->add( inf, start );
    }

}}

