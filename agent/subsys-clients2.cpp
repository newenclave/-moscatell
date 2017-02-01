
#include "subsys-clients2.h"

#include "noname-common.h"
#include "noname-client.h"

#include "common/tuntap.h"
#include "common/utilities.h"
#include "common/net-ifaces.h"

#include "protocol/tuntap.pb.h"

#define LOG(lev) log_(lev, "clients2") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

namespace {

    using client_create_info = clients2::client_create_info;
    using utilities::decorators::quote;
    using error_code = srpc::common::transport::error_code;
    using io_service = SRPC_ASIO::io_service;

    using size_policy           = noname::tcp_size_policy;
    using server_create_info    = listener2::server_create_info;
    using error_code            = noname::error_code;
    using transport_type        = noname::client::transport_type;

    template <typename T>
    std::uintptr_t uint_cast( const T *val )
    {
        return reinterpret_cast<std::uintptr_t>(val);
    }

    struct device;

    struct client_delegate: public noname::protocol_type<size_policy> {

        using message            = google::protobuf::Message;
        using parent_type        = noname::protocol_type<size_policy>;
        using this_type          = client_delegate;

        using tag_type           = typename parent_type::tag_type;
        using buffer_type        = typename parent_type::buffer_type;
        using const_buffer_slice = typename parent_type::const_buffer_slice;
        using buffer_slice       = typename parent_type::buffer_slice;

        using message_type       = noname::message_type;

        using stub_type          = std::function<bool (message_type &)>;
        using call_map           = std::map<std::string, stub_type>;
        using void_call          = std::function<void ( )>;

        using bufer_cache        = srpc::common::cache::simple<std::string>;
        using message_cache      = srpc::common::cache::simple<message_type>;

        client_delegate( size_t mexlen )
            :parent_type( mexlen )
        { }

        void on_message_ready( tag_type, buffer_type, const_buffer_slice )
        { }

        void init( )
        {

        }

        device *my_device_            = nullptr;
        transport_type *my_transport_ = nullptr;

    };

    using proto_sptr = std::shared_ptr<client_delegate>;

    struct device: public common::tuntap_transport {

        using parent_type = common::tuntap_transport;

        device( application *app )
            :common::tuntap_transport( app->get_io_service( ), 2048,
                                       parent_type::OPT_DISPATCH_READ )
        { }

        void on_read( char *data, size_t length )
        {

        }

        static
        std::shared_ptr<device> create( application *app,
                                        const client_create_info &inf )
        {
            auto inst = std::make_shared<device>( app );
            auto d = common::open_tun( inf.device );

            inst->dev_name_ = d.name( );
            inst->get_stream( ).assign( d.release( ) );

            return inst;
        }

        void init( noname::client::client_sptr c )
        {

            c->assign_on_connect(
                [this]( transport_type *t )
                {
                    transport_ = t->shared_from_this( );
                    proto_     = std::make_shared<client_delegate>( 4096 );
                    t->set_delegate( proto_.get( ) );
                    proto_->my_transport_ = transport_.get( );
                    proto_->init( );

                } );

            c->assign_on_error(
                [this]( const error_code &err )
                {
                } );

            c->assign_on_disconnect(
                [this](  )
                {

                } );

            client_.swap( c );

        }

        void start( )
        {
            start_read( );
            client_->start( );
        }

        void on_read_error( const error_code &/*code*/ )
        { }

        void on_write_error( const error_code &/*code*/ )
        { }

        void on_write_exception(  )
        {
            throw;
        }

        std::shared_ptr<transport_type> transport_;
        proto_sptr                      proto_;
        noname::client::client_sptr     client_;
        std::string                     dev_name_;
    };

    using device_sptr = std::shared_ptr<device>;
    using device_map  = std::map<std::string, device_sptr>;

}

    struct clients2::impl {

        application     *app_;
        clients2        *parent_;
        logger_impl     &log_;

        device_map       devs_;
        std::mutex       devs_lock_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void start_all( )
        {
            for( auto &d: devs_ ) {
                d.second->start( );
            }
        }

        bool add_client( const client_create_info &inf, bool start )
        {
            try {

                namespace nudp = noname::client::udp;
                namespace ntcp = noname::client::tcp;

                auto e = utilities::get_endpoint_info( inf.point );
                if( e.is_ip( ) ) {

                    auto cln = inf.udp
                             ? nudp::create( app_, e.addpess, e.service )
                             : ntcp::create( app_, e.addpess, e.service );

                    auto dev = device::create( app_, inf );
                    dev->init( cln );

                    {
                        std::lock_guard<std::mutex> lck(devs_lock_);
                        auto res = devs_.insert(
                                    std::make_pair( inf.device, dev ) );
                        if( !res.second ) {
                            LOGERR << "Failed to add device "
                                   << quote(inf.device);
                            return false;
                        }
                    }

                    if( start ) {
                        dev->start( );
                    }

                } else {
                    LOGERR << "Invalid client format " << quote(inf.point);
                    return false;
                }

            } catch( const std::exception &ex ) {
                LOGERR << "Create client failed: " << ex.what( );
            }

        }
    };

    clients2::clients2( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }


    bool clients2::add_client( const client_create_info &inf, bool start )
    {
        return impl_->add_client( inf, start );
    }

    void clients2::init( )
    { }

    void clients2::start( )
    { 
        impl_->start_all( );
        impl_->LOGINF << "Started.";
    }

    void clients2::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<clients2> clients2::create( application *app )
    {
        return std::make_shared<clients2>( app );
    }
}}

		
