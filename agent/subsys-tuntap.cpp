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

    logger_impl *gs_logger = nullptr;

    using client_stub    = rpc::tuntap::server_instance_Stub;
    using client_wrapper = vcomm::stub_wrapper<client_stub,
                                               vcomm::rpc_channel>;

    using server_stub    = rpc::tuntap::client_instance_Stub;
    using server_wrapper = vcomm::stub_wrapper<server_stub,
                                               vcomm::rpc_channel>;
    using server_wrapper_sptr = std::shared_ptr<server_wrapper>;

    namespace {

        /// p-t-p
        /// one client -> one device
        class client_transport: public common::tuntap_transport {

            //vclnt::base *c_;
            client_wrapper client_;

        public:

            using parent_type = common::tuntap_transport;
            static const auto default_opt = parent_type::OPT_DISPATCH_READ;

            client_transport( vclnt::base *c )
                :parent_type(c->get_io_service( ), 2048, default_opt )
                ,client_(c->create_channel( ), true)
            {
                client_.channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT);
            }

            void on_read( const char *data, size_t length ) override
            {
                auto &log_ = *gs_logger;
                LOGINF << "Got data: " << length << " bytes";
                rpc::tuntap::push_req req;
                req.set_value( data, length );
                client_.call_request( &client_stub::push, &req );
            }

            using shared_type = std::shared_ptr<client_transport>;

            static
            shared_type create( const std::string &device, vclnt::base *c )
            {
                using std::make_shared;
                auto dev  = common::open_tun( device );
                if( dev < 0 ) {
                    return shared_type( );
                }
                auto inst = make_shared<client_transport>( c );
                inst->get_stream( ).assign( dev );
                return inst;
            }
        };

        /// works with routes
        /// one device -> many clients
        class server_transport: public common::tuntap_transport {

            std::map<std::uintptr_t, server_wrapper_sptr> points_;

        public:

            using route_map = std::map<ba::ip::address, server_wrapper>;
            using parent_type = common::tuntap_transport;

            server_transport( ba::io_service &ios )
                :parent_type(ios, 2048, parent_type::OPT_DISPATCH_READ)
            { }

            void on_read( const char *data, size_t length ) override
            {
                rpc::tuntap::push_req req;
                req.set_value( data, length );
                std::cout << "Read from device\n";
                for( auto &p: points_ ) {
                    std::cout << "send data to client channel\n";
                    try {
                        p.second->call_request( &server_stub::push, &req );
                    } catch( ... ) {
                        std::cout << "Error\n";
                    }
                }
            }

            using shared_type = std::shared_ptr<server_transport>;

            void add_client_impl( vcomm::connection_iface_wptr clnt )
            {
                using vserv::channels::unicast::create_event_channel;

                auto clntptr = clnt.lock( );
                if( !clntptr ) {
                    return;
                }

                auto id = reinterpret_cast<std::uintptr_t>( clntptr.get( ) );
                auto svc = std::make_shared<server_wrapper>
                                    (create_event_channel(clntptr), true );

                std::cout << "add client channel " << std::endl;
                auto res = points_[id] = svc;
                svc->channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT );

            }

            void add_client( vcomm::connection_iface *clnt )
            {
                auto wclnt = clnt->weak_from_this( );
                dispatch( [this, wclnt]( ) { add_client_impl( wclnt ); } );
            }

            void del_client_impl( vcomm::connection_iface *clnt )
            {
                auto id = reinterpret_cast<std::uintptr_t>( clnt );
                std::cout << "del client channel " << std::endl;
                points_.erase( id );
            }

            void del_client( vcomm::connection_iface *clnt )
            {
                auto wclnt = clnt->weak_from_this( );
                dispatch( [this, wclnt]( ) { add_client_impl( wclnt ); } );
            }


            static
            shared_type create( application *app, const std::string &device )
            {
                using std::make_shared;
                auto dev  = common::open_tun( device );
                if( dev < 0 ) {
                    return shared_type( );
                }
                auto inst = make_shared<server_transport>
                                                ( app->get_io_service( ) );
                inst->get_stream( ).assign( dev );
                return inst;
            }

        };

//////////////////////// SERVICES ////////////////////////

        class service_wrapper: public vcomm::rpc_service_wrapper {
        public:
            using service_sptr = vcomm::rpc_service_wrapper::service_sptr;
            service_wrapper( service_sptr svc )
                :vcomm::rpc_service_wrapper(svc)
            { }
        };

        using service_wrapper_sptr = std::shared_ptr<service_wrapper>;

        class svc_impl: public rpc::tuntap::server_instance {

            application                  *app_;
            vcomm::connection_iface_wptr  client_;

        public:

            using parent_type = rpc::tuntap::server_instance;

            svc_impl( application *app, vcomm::connection_iface_wptr client )
                :app_(app)
                ,client_(client)
            {
                auto &log_(app->log( ));
                LOGINF << "Create service for " << client.lock( )->name( );
            }

            static std::string name( )
            {
                return parent_type::descriptor( )->full_name( );
            }

            void route_add( ::google::protobuf::RpcController* controller,
                            const ::msctl::rpc::tuntap::route_add_req* request,
                            ::msctl::rpc::tuntap::route_add_res* response,
                            ::google::protobuf::Closure* done) override
            {
                vcomm::closure_holder done_holder( done );
                auto clnt = client_.lock( ); // always valid here
                auto dev = reinterpret_cast<server_transport *>(clnt->user_data( ));
                if( dev ) {
                    dev->add_client( clnt.get( ) );
                }
                //auto device = clnt->env( )
            }

            void push( ::google::protobuf::RpcController* controller,
                       const ::msctl::rpc::tuntap::push_req* request,
                       ::msctl::rpc::tuntap::push_res* response,
                       ::google::protobuf::Closure* done) override
            {
                static auto &log_ = *gs_logger;
                vcomm::closure_holder done_holder( done );
                LOGINF << "Server got data: "
                       << request->value( ).size( ) << " bytes; ";
                auto clnt = client_.lock( );
                auto dev = reinterpret_cast<server_transport *>(clnt->user_data( ));
                if( dev ) {
                    dev->write( request->value( ) );
                } else {
                    auto hex = utilities::bin2hex( request->value( ) );
                    LOGDBG << hex;
                }
            }

        };

        application::service_wrapper_sptr create_service(
                                      application *app,
                                      vcomm::connection_iface_wptr cl )
        {
            auto inst = std::make_shared<svc_impl>( app, cl );
            return app->wrap_service( cl, inst );
        }

        class cnt_impl: public rpc::tuntap::client_instance {
            client_transport::shared_type device_;
        public:
            cnt_impl( client_transport::shared_type device )
                :device_(device)
            {
                std::cout << "client Create service\n";
            }

            void push( ::google::protobuf::RpcController*   /*controller*/,
                       const ::msctl::rpc::tuntap::push_req* request,
                       ::msctl::rpc::tuntap::push_res*      /*response*/,
                       ::google::protobuf::Closure* done) override
            {
                std::cout << "Got from server " << request->value( ).size( )
                          << " bytes\n";
                vcomm::closure_holder done_holder( done );
                device_->write_post_notify( request->value( ),
                [ ](const boost::system::error_code &err)
                {
                    std::cout << err.message( ) << "\n";
                });
            }
            static service_wrapper_sptr create( client_transport::shared_type d)
            {
                auto svc = std::make_shared<cnt_impl>( d );
                return std::make_shared<service_wrapper>( svc );
            }
        };
////////////////////////////////////////////////////////////////////////

        using clients_set = std::set<vclnt::base_sptr>;
        using servers_map = std::map<std::string,
                                     server_transport::shared_type>;

        using router_map = std::map<std::uintptr_t,
                                    server_transport::shared_type>;

    }

    struct tuntap::impl {

        application  *app_;
        tuntap       *parent_;
        logger_impl  &log_;

        clients_set   clients_;
        std::mutex    clients_lock_;

        servers_map         servers_;
        router_map          router_;
        vtrc::shared_mutex  servers_lock_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        {
            gs_logger = &log_;
        }

        void add_client_point( vclnt::base_sptr c, const std::string &dev )
        {
            auto device = client_transport::create( dev, c.get( ) );

            if( device ) {

                c->assign_rpc_handler( cnt_impl::create( device ) );
                device->start_read( );

                std::lock_guard<std::mutex> lck(clients_lock_);
                clients_.insert( c );

                client_wrapper cl(c->create_channel( ), true);
                cl.channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT );
                rpc::tuntap::route_add_req req;
                cl.call_request( &client_stub::route_add, &req );

            } else {
                LOGERR << "Failed to open device: " << errno;
            }
        }

        void del_client_point( vclnt::base_sptr c )
        {
            std::lock_guard<std::mutex> lck(clients_lock_);
            clients_.erase( c );
        }

        void add_server_point( vcomm::connection_iface *c,
                               const std::string &dev )
        {
            vtrc::upgradable_lock lck(servers_lock_);
            auto f = servers_.find( dev );
            if( f != servers_.end( ) ) {
                c->set_user_data( f->second.get( ) );
                router_[reinterpret_cast<std::uintptr_t>(c)] = f->second;
                LOGINF << "Adding client for device '" << dev << "'";
            } else {
                LOGINF << "Crteate device '" << dev << "'";
                auto device = server_transport::create( app_, dev );
                if( device ) {
                    device->start_read( );
                    c->set_user_data( device.get( ) );
                    vtrc::upgrade_to_unique utu(lck);
                    servers_[dev] = device;
                    router_[reinterpret_cast<std::uintptr_t>(c)] = device;
                    LOGINF << "Adding client for device '" << dev << "'";
                } else {
                    LOGERR << "Failed to open device: " << errno;
                }
            }
        }

        void del_server_point( vcomm::connection_iface *c )
        {
            vtrc::shared_lock lck(servers_lock_);
            auto id = reinterpret_cast<std::uintptr_t>(c);
            auto f = router_.find( id);
            if( f != router_.end( ) ) {
                f->second->del_client( c );
            }
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

        void reg_creator( )
        {
            app_->register_service_factory( svc_impl::name( ), create_service );
        }

        void unreg_creator( )
        {
            app_->unregister_service_factory( svc_impl::name( ) );
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
        impl_->reg_creator( );
    }

    void tuntap::stop( )
    { 
        impl_->unreg_creator( );
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<tuntap> tuntap::create( application *app )
    {
        return std::make_shared<tuntap>( app );
    }
}}
