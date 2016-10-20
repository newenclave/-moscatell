#include <mutex>

#include "subsys-clients.h"

#include "vtrc-client/vtrc-client.h"
#include "vtrc-errors.pb.h"

#define LOG(lev) log_(lev, "clients") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent {

    namespace vcomm = vtrc::common;
    namespace vclnt = vtrc::client;
    namespace verrs = vtrc::rpc::errors;

    struct client_info {
        vclnt::base_sptr client;
        std::string      device;

        client_info( ) = default;

        client_info( vclnt::base_sptr c, const std::string &d )
            :client(c)
            ,device(d)
        { }

        client_info &operator = ( const client_info &other )
        {
            client = other.client;
            device = other.device;
            return *this;
        }
    };

    using clients_map = std::map<std::string, client_info>;

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
                            const std::string &point )
        {
            LOGERR << "Client for '" << point << "' failed to init; "
                   << " error: " << errs.code( )
                   << " (" << errs.additional( ) << ")"
                   << "; " << mesg
                      ;
        }

        void on_connect( vclnt::base *c, const std::string &point )
        {
            LOGINF << "Client connected to '" << point
                   << "' successfully."
                      ;
        }

        void on_disconnect( vclnt::base *c, const std::string &point )
        {
              LOGINF << "Client disconnected '" << point << "'"
                        ;
              auto clnt = c->shared_from_this( );
              parent_->get_on_client_disconnect( )( clnt );
        }

        void on_ready( vclnt::base *c, const std::string &dev )
        {
            LOGINF << "Client is ready for device '" << dev << "'"
                      ;
            parent_->get_on_client_ready()( c->shared_from_this( ), dev );
        }

        bool add( const std::string &point, const std::string &dev )
        {
            auto inf  = utilities::get_endpoint_info( point );
            auto clnt = vclnt::vtrc_client::create( app_->pools( ) );
            auto clnt_ptr = clnt.get( );

            clnt->on_init_error_connect(
                [this, point]( const verrs::container &errs,
                               const char *mesg )
                {
                    this->on_init_error( errs, mesg, point );
                } );

            clnt->on_ready_connect(
                [this, clnt_ptr, dev]( )
                {
                    this->on_ready( clnt_ptr, dev );
                } );

            clnt->on_connect_connect(
                [this, clnt_ptr, point](  ) {
                    this->on_connect( clnt_ptr, point );
                } );

            clnt->on_disconnect_connect(
                [this, clnt_ptr, point](  ) {
                    this->on_disconnect( clnt_ptr, point );
                } );

            bool failed = false;
            if( inf.is_local( ) ) {
                clnt->connect( inf.addpess );
            } else if( inf.is_ip( ) ) {
                clnt->connect( inf.addpess, inf.service );
            } else {
                LOGERR << "Failed to add client '"
                       << point << "'; Bad format";
                failed = true;
            }

            std::lock_guard<std::mutex> lck(clients_lock_);
            clients_[point] = client_info( clnt, dev );

            return !failed;
        }

        void start_all( )
        {

        }

    };

    clients::clients( application *app )
        :impl_(new impl(app))
    {
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
