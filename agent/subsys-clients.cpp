#include <mutex>

#include "subsys-clients.h"

#include "vtrc-client/vtrc-client.h"
#include "vtrc-errors.pb.h"
#include "vtrc-common/vtrc-delayed-call.h"

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
                        LOGERR << "Connect failed to " << inst->info;
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

    using client_info_sptr = std::shared_ptr<client_info>;
    using client_info_wptr = std::weak_ptr<client_info>;
    using clients_map = std::map<std::string, client_info_sptr>;
}

    struct clients::impl {

        application  *app_;
        clients      *parent_;
        logger_impl  &log_;

        clients_map  clients_;
        std::mutex   clients_lock_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

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
            auto c = wc.lock( );
            if( c ) {
                LOGINF << "Client disconnected '" << point << "'"
                            ;
                parent_->get_on_client_disconnect( )( c->client );
                c->start_timer( );
            }
        }

        void on_ready( client_info_wptr wc, const std::string &dev )
        {
            auto c = wc.lock( );
            if( c ) {
                LOGINF << "Client is ready for device '"
                       << c->device << "'"
                          ;
                parent_->get_on_client_ready( )( c->client, dev );
            }
        }

        bool add( const std::string &point, const std::string &dev )
        {
            auto inf  = utilities::get_endpoint_info( point );
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
                std::lock_guard<std::mutex> lck(clients_lock_);
                clients_[point] = clnt;
            } else {
                LOGERR << "Failed to add client '"
                       << point << "'; Bad format";
            }

            return !failed;
        }

        void start_all( )
        {
            std::lock_guard<std::mutex> lck(clients_lock_);
            for( auto &c: clients_ ) {
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

    bool clients::add_client( const std::string &point, const std::string &dev)
    {
        return impl_->add( point, dev );
    }

    void clients::start_all( )
    {
        impl_->start_all( );
    }
}}
