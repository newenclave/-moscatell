
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
        using message_sptr       = noname::message_sptr;

        using stub_type          = std::function<bool (message_type &)>;
        using call_map           = std::map<std::string, stub_type>;
        using void_call          = std::function<void ( )>;

        using bufer_cache        = srpc::common::cache::simple<std::string>;
        using message_cache      = srpc::common::cache::simple<message_type>;

        using callbacks          = transport_type::write_callbacks;

        using push_call          = std::function<void(const char *, size_t)>;


        client_delegate( size_t mexlen )
            :parent_type( mexlen )
            ,next_tag_(1)
            ,next_id_(100)
        {
            push_ = [this]( ... ){ };
            calls_["init"] = [this]( message_type &mess )
                             { return on_ready( mess ); };
        }

        bool call( message_type &mess )
        {
            auto f = calls_.find( mess.call( ) );
            if( f != calls_.end( ) ) {
                return f->second( mess );
            }
            return false;
        }

        bool on_ready( message_type &/*mess*/ )
        {
            ready_ = true;
            calls_["push"] = [this]( message_type &mess )
                             { return on_push( mess ); };

            push_ = [this]( const char * d, size_t l){ send_impl( d, l ); };
            return true;

            std::cout << "Ready!!!\n";
        }

        bool on_push( message_type &mess );

        void send( const char *data, size_t len )
        {
            push_( data, len );
        }

        void send_impl( const char *data, size_t d )
        {
            message_type mess;

            mess.set_call( "push" );
            mess.set_body( data, d );

            buffer_type buf = std::make_shared<std::string>( );
            auto slice = prepare_message( buf, mess );
            get_transport( )->write( slice.data( ), slice.size( ),
                                     callbacks::post([buf](...){ } ) );
        }

        void on_message_ready( tag_type t, buffer_type b,
                               const_buffer_slice sl )
        {
            message_type mess;
            mess.ParseFromArray( sl.data( ), sl.size( ) );
            std::cout << "Got message " << mess.DebugString( ) << std::endl;
            call( mess );
        }

        std::uint64_t next_id( )
        {
            return next_id_++;
        }

        std::uint64_t next_tag( )
        {
            return next_tag_++;
        }

        buffer_slice prepare_message( buffer_type buf, const message &mess )
        {
            typedef typename parent_type::size_policy size_policy;

            auto tag = next_tag( );

            buf->resize( size_policy::max_length );

            const size_t old_len   = buf->size( );
            const size_t hash_size = hash( )->length( );

            tag_policy::append( tag, *buf );
            mess.AppendToString( buf.get( ) );

            buf->resize( buf->size( ) + hash_size );

            hash( )->get( buf->c_str( ) + old_len,
                          buf->size( ) - old_len - hash_size,
                       &(*buf)[buf->size( ) - hash_size]);

            buffer_slice res( &(*buf)[old_len], buf->size( ) - old_len );

            buffer_slice packed = pack_message( buf, res );

            return insert_size_prefix( buf, packed );
        }

        void init( )
        {
            message_sptr mess = std::make_shared<message_type>( );
            mess->set_call( "I0" );
            mess->set_body( "FUUUUUUUUUUUUUUUU!" );

            buffer_type buf = std::make_shared<std::string>( );
            auto slice = prepare_message( buf, *mess );

            get_transport( )->write( slice.data( ), slice.size( ),
                                     callbacks::post([buf](...){ } ) );
            get_transport( )->read( );
        }

        bool ready( ) const
        {
            return ready_;
        }

        void on_error( const char *mess )
        {
            std::cout << "err " << mess << std::endl;
        }

        device *my_device_ = nullptr;

        std::atomic<std::uint64_t> next_tag_;
        std::atomic<std::uint64_t> next_id_;

        call_map  calls_;
        push_call push_;

        bool                    ready_ = false;
//        std::condition_variable ready_var_;
//        std::mutex              ready_lock_;
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
                    proto_ = std::make_shared<client_delegate>( 4096 );
                    proto_->assign_transport( t );
                    t->set_delegate( proto_.get( ) );
                    proto_->assign_transport( t );
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

        proto_sptr                      proto_;
        noname::client::client_sptr     client_;
        std::string                     dev_name_;
    };

    using device_sptr = std::shared_ptr<device>;
    using device_map  = std::map<std::string, device_sptr>;

    //////////////////////////// CLIENT IMPL

    bool client_delegate::on_push( message_type &mess )
    {
        my_device_->write( mess.body( ).c_str( ), mess.body( ).size( ) );
        return true;
    }
    ////////////////////////////////////////

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

		
