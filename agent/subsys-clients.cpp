#include <mutex>
#include <system_error>

#include "subsys-clients.h"

#include "vtrc-client/vtrc-client.h"
#include "vtrc-errors.pb.h"
#include "vtrc-common/vtrc-delayed-call.h"

#include "common/tuntap.h"
#include "common/utilities.h"

#include "protocol/tuntap.pb.h"

#include "vtrc-common/vtrc-closure-holder.h"
#include "vtrc-common/vtrc-rpc-service-wrapper.h"
#include "vtrc-common/vtrc-stub-wrapper.h"
#include "vtrc-common/vtrc-mutex-typedefs.h"
#include "vtrc-server/vtrc-channels.h"

#define LOG(lev) log_(lev, "clients") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent {

namespace {

    namespace vcomm = vtrc::common;
    namespace vclnt = vtrc::client;
    namespace verrs = vtrc::rpc::errors;
    namespace ba    = boost::asio;
    namespace bs    = boost::system;

    using utilities::decorators::quote;

    logger_impl *gs_logger = nullptr;

    using delayed_call = vcomm::delayed_call;
    using delayed_call_ptr = std::unique_ptr<delayed_call>;
    using vtrc_client_sptr = vclnt::vtrc_client_sptr;

    using client_stub    = rpc::tuntap::server_instance_Stub;
    using client_wrapper = vcomm::stub_wrapper<client_stub,
                                               vcomm::rpc_channel>;

    struct client_info: public std::enable_shared_from_this<client_info> {

        application              *app;
        vtrc_client_sptr          client;
        std::string               device;
        utilities::endpoint_info  info;
        delayed_call              timer;
        bool                      active = true;

        using error_code = VTRC_SYSTEM::error_code;

        client_info( application *ap )
            :app(ap)
            ,client(create_client(app->pools( )))
            ,timer(app->pools( ).get_io_service( ))
        { }

        static
        vtrc_client_sptr create_client( vcomm::pool_pair &pp )
        {
            return vclnt::vtrc_client::create( pp.get_io_service( ),
                                               pp.get_rpc_service( ) );
        }

        static
        std::shared_ptr<client_info> create( application *app )
        {
            auto inst = std::make_shared<client_info>( app );

            return inst;
        }

        void start_connect( )
        {
            if( !active ) {
                return;
            }

            auto wthis = std::weak_ptr<client_info>(shared_from_this( ));
            auto callback = [this, wthis] ( const error_code &err ) {
                auto &log_(*gs_logger);
                if( err ) {
                    auto inst = wthis.lock( );
                    if( inst ) {
                        LOGDBG << "Connect failed to " << inst->info
                               << "; Restating connect...";
                        inst->start_timer( );
                    }
                }
            };

            if( info.is_local( ) ) {
                client->async_connect( info.addpess, callback);
            } else if( info.is_ip( ) ) {
                client->async_connect( info.addpess, info.service,
                                       callback, true );
            }
        }

        void handler( const VTRC_SYSTEM::error_code &err,
                      std::weak_ptr<client_info> winst)
        {
            if( !err ) {
                auto inst = winst.lock( );
                if( !inst ) {
                    return;
                }
                start_connect( );
            }
        }

        void start_timer( )
        {
            auto wthis = std::weak_ptr<client_info>(shared_from_this( ));
            timer.call_from_now(
            [this, wthis]( const VTRC_SYSTEM::error_code &err ) {
                handler( err, wthis );
            }, delayed_call::seconds( 5 ) ); /// TODO: settings?
        }

        void stop( )
        {
            active = false;
            timer.cancel( );
            client->disconnect( );
        }
    };

    /// p-t-p
    /// one client -> one device
    class client_transport: public common::tuntap_transport {

        //vclnt::base *c_;
        client_wrapper client_;
        std::uint64_t  tick_;

    public:

        using parent_type = common::tuntap_transport;
        static const auto default_opt = parent_type::OPT_DISPATCH_READ;

        client_transport( vclnt::base *c )
            :parent_type(c->get_io_service( ), 2048, default_opt )
            ,client_(c->create_channel( ), true)
        {
            client_.channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT);
        }

        ~client_transport( )
        { }

        void on_read( const char *data, size_t length ) override
        {
			if( common::extract_family( data, length ) == 4 ) {
				rpc::tuntap::push_req req;
				req.set_value( data, length );
				client_.call_request( &client_stub::push, &req );
			}
        }

        using shared_type = std::shared_ptr<client_transport>;

        static
        shared_type create( std::string &device, vclnt::base *c )
        {
            using std::make_shared;
            auto dev  = common::open_tun( device );
            auto inst = make_shared<client_transport>( c );
            inst->get_stream( ).assign( dev.release( ) );
            device = dev.name( );
            return inst;
        }

        void ping( )
        {
            client_.call( &client_stub::ping );
        }

        std::uint64_t tick( ) const { return tick_; }
        void set_tick( std::uint64_t val ) { tick_ = val; }

    };


    class cnt_impl: public rpc::tuntap::client_instance {

        client_transport::shared_type device_;
        vclnt::base *clnt_;

        delayed_call keep_alive_;

    public:
        cnt_impl( client_transport::shared_type device, vclnt::base *c )
            :device_(device)
            ,clnt_(c)
            ,keep_alive_(device->get_io_service( ))
        {
            device->set_tick( application::tick_count( ) );
            start_timer( 30 );
        }

        void start_timer( std::uint32_t sec )
        {
            auto wclient = clnt_->weak_from_this( );
            auto handler = [this, wclient]( const bs::error_code &err ) {
                this->keep_alive( err, wclient );
            };

            keep_alive_.call_from_now( handler, delayed_call::seconds( sec ) );
        }

        void keep_alive( const boost::system::error_code &err,
                         vclnt::base_wptr clnt )
        {
            auto &log_(*gs_logger);
            if( !err ) {
                auto lck = clnt.lock( );
                if( lck ) {
                    auto current = application::tick_count( );
                    if( current - device_->tick( ) >= 60000000 ) { /// 1 minute
                        LOGWRN << "Keep alive timer...Client disconnected.";
                        lck->disconnect( );
                    } else {
                        //LOGDBG << "Keep alive timer...Pinging server...";
                        device_->ping( );
                        start_timer( 30 );
                    }
                }
            }
        }

        ~cnt_impl( )
        {
            keep_alive_.cancel( );
            device_->close( );
            device_.reset( );
        }

        void push( ::google::protobuf::RpcController*   /*controller*/,
                   const ::msctl::rpc::tuntap::push_req* request,
                   ::msctl::rpc::tuntap::push_res*      /*response*/,
                   ::google::protobuf::Closure* done) override
        {
            vcomm::closure_holder done_holder( done );
            device_->write_post_notify( request->value( ),
                [ ](const boost::system::error_code &err)
                {
                    if( err ) {
                        (*gs_logger)(logger_impl::level::error)
                                << err.message( ) << "\n";
                    }
                });
        }

        void ping( ::google::protobuf::RpcController* /*controller*/,
                   const ::msctl::rpc::empty* /*request*/,
                   ::msctl::rpc::empty* /*response*/,
                   ::google::protobuf::Closure* done) override
        {
            vcomm::closure_holder done_holder( done );
            device_->set_tick( application::tick_count( ) );
        }

        class client_service_wrapper: public vcomm::rpc_service_wrapper {
        public:
            using service_sptr = vcomm::rpc_service_wrapper::service_sptr;
            client_service_wrapper( service_sptr svc )
                :vcomm::rpc_service_wrapper(svc)
            { }
        };

        using service_wrapper_sptr = std::shared_ptr<client_service_wrapper>;

        static service_wrapper_sptr create( client_transport::shared_type d,
                                            vclnt::base *clnt )
        {
            auto svc = std::make_shared<cnt_impl>( d, clnt );
            return std::make_shared<client_service_wrapper>( svc );
        }
    };

    using client_info_sptr = std::shared_ptr<client_info>;
    using client_info_wptr = std::weak_ptr<client_info>;
    using clients_map = std::map<std::string, client_info_sptr>;
    using clients_set = std::set<vclnt::base_sptr>;

}

    struct clients::impl {

        application  *app_;
        clients      *parent_;
        logger_impl  &log_;

        clients_map   points_;
        std::mutex    points_lock_;

        clients_set   clients_;
        std::mutex    clients_lock_;

        bool          working_ = true;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void start( )
        {

        }

        void stop( )
        {
            working_ = false;
            std::lock_guard<std::mutex> lck2(points_lock_);
            for( auto &p: points_ ) {
                p.second->stop( );
            }
        }

        void add_client( vclnt::base_sptr c, const std::string &dev_hint )
        {

            struct dev_keeper {
                client_transport::shared_type dev;
                vclnt::base_sptr              c;
                bool                          owned = true;
                ~dev_keeper( )
                {
                    if( owned ) {
                        if( dev ) dev->close( );
                        if( c )   c->disconnect( );
                    }
                }
                void release( )
                {
                    owned = false;
                }
            };

            dev_keeper keeper;

            std::string dev = dev_hint;
            keeper.dev = client_transport::create( dev, c.get( ) );
            keeper.c   = c;

            if( keeper.dev ) {

                c->assign_rpc_handler( cnt_impl::create( keeper.dev,
                                                         c.get( ) ) );

                client_wrapper cl(c->create_channel( ), true);

                try {
                    rpc::tuntap::register_req req;
                    rpc::tuntap::register_res res;
                    cl.call( &client_stub::register_me, &req, &res );

                    auto naddr = ntohl(res.iface_addr( ).v4_address( ));
                    auto nmask = ntohl(res.iface_addr( ).v4_mask( ));

                    ba::ip::address_v4 addr(naddr);
                    ba::ip::address_v4 mask(nmask);

                    LOGINF << "Got address: " << quote(addr.to_string( ))
                           << " and mask: " << quote(mask.to_string( ));

                    auto hdl = keeper.dev->get_stream( ).native_handle( );
                    common::setup_device( hdl, dev,
                                          addr.to_string( ), addr.to_string( ),
                                          mask.to_string( ) );

                    LOGINF << "Device setup success.";

                } catch( const std::exception &ex ) {
                    LOGERR << "Client failed to register: " << ex.what( );
                    return;
                }

                keeper.release( );

                cl.channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT );
                keeper.dev->start_read( );

                std::lock_guard<std::mutex> lck(clients_lock_);
                clients_.insert( c );

            } else {
                std::error_code ec(errno, std::system_category( ));
                LOGERR << "Failed to open device: " << ec.value( )
                       << " (" << ec.message( ) << ")";
            }
        }

        void del_client( vclnt::base_sptr c )
        {
            std::lock_guard<std::mutex> lck(points_lock_);
            auto f = clients_.find( c );
            if( f != clients_.end( ) ) {
                (*f)->erase_all_rpc_handlers( );
                clients_.erase( f );
            }
        }

        void on_init_error( const verrs::container &errs,
                            const char *mesg,
                            client_info_wptr wc,
                            const std::string &point )
        {
            if( working_ ) {
                LOGERR << "Client for '" << point << "' failed to init; "
                       << " error: " << errs.code( )
                       << " (" << errs.additional( ) << ")"
                       << "; " << mesg
                        ;
            }
        }

        void on_connect( client_info_wptr /*wc*/, const std::string &point )
        {
            LOGINF << "Client connected to '" << point
                   << "' successfully."
                      ;
        }

        void on_disconnect( client_info_wptr wc, const std::string &point )
        {
            if( working_ ) {
                using utilities::decorators::quote;
                auto c = wc.lock( );
                if( c ) {
                    LOGINF << "Client disconnected " << quote(point)
                                ;
                    parent_->get_on_client_disconnect( )( c->client );
                    del_client( c->client );
                    c->start_timer( );
                }
            }
        }

        void on_ready( client_info_wptr wc, const std::string &dev )
        {
            using utilities::decorators::quote;
            if( working_ ) {
                auto c = wc.lock( );
                if( c ) {
                    LOGINF << "Client is ready for device "
                           << quote(c->device)
                              ;
                    app_->get_rpc_service( ).post( [this, c, dev]( ) {
                        add_client( c->client, dev );
                    } );
                    parent_->get_on_client_ready( )( c->client, dev );
                }
            }
        }

        bool add( const clients::client_create_info &add_info, bool auto_start )
        {
            using utilities::decorators::quote;
            auto point = add_info.point;
            auto dev   = add_info.device;

            auto inf  = utilities::get_endpoint_info( add_info.point );
            auto clnt = client_info::create( app_ );
            clnt->device = dev;
            auto clnt_wptr = std::weak_ptr<client_info>( clnt );

            clnt->client->on_init_error_connect(
                [this, point, clnt_wptr]( const verrs::container &errs,
                               const char *mesg )
                {
                    this->on_init_error( errs, mesg, clnt_wptr, point );
                } );

            clnt->client->on_ready_connect(
                [this, clnt_wptr, dev]( )
                {
                    this->on_ready( clnt_wptr, dev );
                } );

            clnt->client->on_connect_connect(
                [this, clnt_wptr, point](  ) {
                    this->on_connect( clnt_wptr, point );
                } );

            clnt->client->on_disconnect_connect(
                [this, clnt_wptr, point](  ) {
                    this->on_disconnect( clnt_wptr, point );
                } );

            bool failed = inf.is_none( );

            if( !failed ) {
                clnt->info   = inf;
                clnt->device = dev;
                if( auto_start ) {
                    clnt->start_connect( );
                }
                std::lock_guard<std::mutex> lck(points_lock_);
                points_[point] = clnt;
            } else {
                LOGERR << "Failed to add client "
                       << quote(point) << "; Bad format";
            }

            return !failed;
        }

        void start_all( )
        {
            std::lock_guard<std::mutex> lck(points_lock_);
            for( auto &c: points_ ) {
                c.second->start_connect( );
            }
        }
    };

    clients::clients( application *app )
        :impl_(new impl(app))
    {
        gs_logger = &app->log( );
        impl_->parent_ = this;
    }

    void clients::init( )
    { }

    void clients::start( )
    { 
        impl_->start( );
        impl_->start_all( );
        impl_->LOGINF << "Started.";
    }

    void clients::stop( )
    { 
        impl_->stop( );
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<clients> clients::create( application *app )
    {
        return std::make_shared<clients>( app );
    }

    bool clients::add_client( const client_create_info &inf, bool start )
    {
        return impl_->add( inf, start );
    }

}}
