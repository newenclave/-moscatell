#include <mutex>

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

    logger_impl *gs_logger = nullptr;

    using delayed_call = vcomm::delayed_call;
    using delayed_call_ptr = std::unique_ptr<delayed_call>;
    using vtrc_client_sptr = vclnt::vtrc_client_sptr;

    using client_stub    = rpc::tuntap::server_instance_Stub;
    using client_wrapper = vcomm::stub_wrapper<client_stub,
                                               vcomm::rpc_channel>;

    struct client_info: public std::enable_shared_from_this<client_info> {

        vtrc_client_sptr          client;
        std::string               device;
        utilities::endpoint_info  info;
        delayed_call              timer;
        //application              *app_;

        using error_code = VTRC_SYSTEM::error_code;

        client_info( vcomm::pool_pair &pp )
            :client(create_client(pp))
            ,timer(pp.get_io_service( ))
        { }

        static
        vtrc_client_sptr create_client( vcomm::pool_pair &pp )
        {
            return vclnt::vtrc_client::create( pp.get_io_service( ),
                                               pp.get_rpc_service( ) );
        }

        static
        std::shared_ptr<client_info> create( vcomm::pool_pair &pp )
        {
            auto inst = std::make_shared<client_info>( std::ref(pp) );

            return inst;
        }

        void start_connect( )
        {
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
    };

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

        ~client_transport( )
        { }

        void on_read( const char *data, size_t length ) override
        {
//                auto &log_ = *gs_logger;
//                LOGINF << "Got data: " << length << " bytes";
            rpc::tuntap::push_req req;
            req.set_value( data, length );
            client_.call_request( &client_stub::push, &req );
        }

        using shared_type = std::shared_ptr<client_transport>;

        static
        shared_type create( const std::string &device, vclnt::base *c )
        {
            using std::make_shared;
            auto dev  = common::open_tun( device, false );
            if( dev < 0 ) {
                return shared_type( );
            }
//                if( common::device_up( device ) < 0) {
//                    return shared_type( );
//                }

            auto inst = make_shared<client_transport>( c );
            inst->get_stream( ).assign( dev );
            return inst;
        }
    };


    class cnt_impl: public rpc::tuntap::client_instance {
        client_transport::shared_type device_;
    public:
        cnt_impl( client_transport::shared_type device )
            :device_(device)
        { }

        ~cnt_impl( )
        {
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

        class client_service_wrapper: public vcomm::rpc_service_wrapper {
        public:
            using service_sptr = vcomm::rpc_service_wrapper::service_sptr;
            client_service_wrapper( service_sptr svc )
                :vcomm::rpc_service_wrapper(svc)
            { }
        };

        using service_wrapper_sptr = std::shared_ptr<client_service_wrapper>;

        static service_wrapper_sptr create( client_transport::shared_type d)
        {
            auto svc = std::make_shared<cnt_impl>( d );
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

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void add_client( vclnt::base_sptr c, const std::string &dev )
        {
            auto device = client_transport::create( dev, c.get( ) );

            if( device ) {

                c->assign_rpc_handler( cnt_impl::create( device ) );
                device->start_read( );

                std::lock_guard<std::mutex> lck(clients_lock_);
                clients_.insert( c );
                clients_lock_.unlock( );

                client_wrapper cl(c->create_channel( ), true);
                cl.channel( )->set_flag( vcomm::rpc_channel::DISABLE_WAIT );

//                rpc::tuntap::route_add_req req;
//                auto tun_addr = common::get_iface_ipv4( dev );
//                req.add_v4( )->set_address(htonl(tun_addr.first.to_ulong( ) ) );

//                cl.call_request( &client_stub::route_add, &req );

            } else {
                LOGERR << "Failed to open device: " << errno;
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
            LOGERR << "Client for '" << point << "' failed to init; "
                   << " error: " << errs.code( )
                   << " (" << errs.additional( ) << ")"
                   << "; " << mesg
                      ;
        }

        void on_connect( client_info_wptr /*wc*/, const std::string &point )
        {
            LOGINF << "Client connected to '" << point
                   << "' successfully."
                      ;
        }

        void on_disconnect( client_info_wptr wc, const std::string &point )
        {
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

        void on_ready( client_info_wptr wc, const std::string &dev )
        {
            using utilities::decorators::quote;
            auto c = wc.lock( );
            if( c ) {
                LOGINF << "Client is ready for device "
                       << quote(c->device)
                          ;

                add_client( c->client, dev );
                parent_->get_on_client_ready( )( c->client, dev );
            }
        }

        bool add( const clients::client_create_info &add_info, bool auto_start )
        {
            using utilities::decorators::quote;
            auto point = add_info.point;
            auto dev   = add_info.device;

            auto inf  = utilities::get_endpoint_info( add_info.point );
            auto clnt = client_info::create( app_->pools( ) );
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
        impl_->start_all( );
        impl_->LOGINF << "Started.";
    }

    void clients::stop( )
    { 
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
