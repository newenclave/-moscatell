
#include "subsys-listener2.h"

#include "noname-server.h"

#include "common/tuntap.h"
#include "common/utilities.h"
#include "common/net-ifaces.h"

#include "protocol/tuntap.pb.h"

#define LOG(lev) log_(lev, "listener2") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

namespace  {

    using utilities::decorators::quote;
    using size_policy = noname::tcp_size_policy;

    struct client_deledate: public noname::protocol_type<size_policy> {

        using parent_type        = noname::protocol_type<size_policy>;
        using this_type          = client_deledate;
        using tag_type           = typename parent_type::tag_type;
        using buffer_type        = typename parent_type::buffer_type;
        using const_buffer_slice = typename parent_type::const_buffer_slice;

        using message_type       = noname::message_type;

        using stub_type          = std::function<bool (message_type &)>;
        using call_map           = std::map<std::string, stub_type>;
        using void_call          = std::function<void ( )>;

        client_deledate( size_t mexlen )
            :parent_type( mexlen )
        { }

        bool call( message_type &mess )
        {
            auto f = calls_.find( mess.call( ) );
            if( f != calls_.end( ) ) {
                return f->second( mess );
            }
            return false;
        }

        void on_message_ready( tag_type, buffer_type,
                               const_buffer_slice ) override
        { }

        void on_close( ) override
        {
            on_close_( );
        }

        common::tuntap_transport       *my_device_;
        call_map                        calls_;
        void_call                       on_close_;
    };

    struct device: public common::tuntap_transport {

        using this_type   = device;
        using client_type = client_deledate;
        using client_sptr = std::shared_ptr<client_type>;

        using route_map   = std::map<std::uint32_t, client_sptr>;

        using parent_type = common::tuntap_transport;

        device( SRPC_ASIO::io_service &ios )
            :common::tuntap_transport( ios, 2048,
                                       parent_type::OPT_DISPATCH_READ )
        { }

        void on_read( char *data, size_t length ) override
        {

        }

        void on_read_error( const noname::error_code & ) override
        {

        }

        void on_write_error( const noname::error_code & ) override
        {

        }

        void on_write_exception(  ) override
        {
            throw;
        }


        route_map routes_;
    };

    template <typename Acceptor>
    struct server_point {

        using convertor   = noname::acceptor_to_size_policy<Acceptor>;

        using server_sptr  = noname::server::server_sptr;
        using device_sptr  = std::shared_ptr<device>;
        using client_proto = client_deledate;
        using client_sptr  = std::shared_ptr<client_proto>;

        server_sptr service_;
        device_sptr device_;

        client_sptr create_delegate( noname::server::transport_type *t )
        {
            auto inst = std::make_shared<client_proto>( convertor::maxlen );

            t->set_delegate( inst.get( ) );
            inst->assign_transport( t );

            return inst;
        }

    };

}

    struct listener2::impl {

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void on_new_client( noname::server::transport_type *c,
                            std::string addr, std::uint16_t )
        {

        }

        void on_error( const noname::server::error_code &e )
        {

        }

        void on_close(  )
        {

        }

        bool add_server( const listener2::server_create_info &inf,
                         bool start )
        {
            namespace ntcp = noname::server::tcp;
            namespace nudp = noname::server::udp;

            auto e = utilities::get_endpoint_info( inf.point );

            try {
                if( e.is_ip( ) ) {
                    auto svc = inf.udp
                            ? nudp::create( app_, e.addpess, e.service )
                            : ntcp::create( app_, e.addpess, e.service );

                } else {
                    LOGERR << "Invalid endpoint format: " << quote(inf.point);
                }
            } catch( const std::exception &ex ) {
                LOGERR << "Failed to create endpoint: " << quote(inf.point)
                       << "; " << ex.what( )
                          ;
                return false;
            }
            return true;
        }

        void start_all( )
        {

        }

        application  *app_;
        listener2    *parent_;
        logger_impl  &log_;
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

