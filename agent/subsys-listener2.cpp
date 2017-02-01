
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

    using size_policy           = noname::tcp_size_policy;
    using server_create_info    = listener2::server_create_info;
    using error_code            = noname::error_code;

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
            ,bcache_(10)
            ,mcache_(10)
            ,next_tag_(0)
            ,next_id_(100)
        { }

        bool call( message_type &mess )
        {
            auto f = calls_.find( mess.call( ) );
            if( f != calls_.end( ) ) {
                return f->second( mess );
            }
            return false;
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

        void on_message_ready( tag_type, buffer_type,
                               const_buffer_slice ) override;

        void on_close( ) override;

        device         *my_device_ = nullptr;
        call_map        calls_;
        void_call       on_close_;
        bufer_cache     bcache_;
        message_cache   mcache_;

        std::uint32_t   my_ip_   = 0;
        std::uint16_t   my_port_ = 0;

        std::atomic<std::uint64_t> next_tag_;
        std::atomic<std::uint64_t> next_id_;
    };

    using delegate_sptr = std::shared_ptr<client_delegate>;
    using delegate_wptr = std::weak_ptr<client_delegate>;

    ///////////// DEVICE
    struct device: public common::tuntap_transport {

        using this_type   = device;
        using routev4_map = std::map<std::uint32_t, delegate_sptr>;
        using client_set  = std::map<std::uintptr_t, delegate_sptr>;
        using parent_type = common::tuntap_transport;

        device( application *app, utilities::address_v4_poll poll )
            :common::tuntap_transport( app->get_io_service( ), 2048,
                                       parent_type::OPT_DISPATCH_READ )
            ,app_(app)
            ,poll_(poll)
        { }

        static
        std::shared_ptr<device> create( application *app,
                                        const server_create_info &inf )
        {
            auto inst = std::make_shared<device>( app, inf.addr_poll );
            auto hdl  = common::open_tun( inf.device );

            inst->device_name_ = hdl.name( );
            inst->get_stream( ).assign( hdl.release( ) );

            return inst;
        }

        void add_tmp_client( delegate_sptr deleg )
        {
            weak_type wptr( shared_from_this( ) );
            dispatch(
                [this, wptr, deleg]( )
                {
                    auto lck(wptr.lock( ));
                    if( lck ) {
                        auto id = uint_cast(deleg.get( ));
                        deleg->get_transport( )->read( );
                        tmp_clients_.insert( std::make_pair(id, deleg) );
                    }
                } );
        }

        void del_client( client_delegate *deleg )
        {
            weak_type wptr( shared_from_this( ) );
            auto addr = deleg->my_ip_;
            dispatch(
                [this, wptr, deleg, addr]( )
                {
                    auto lck(wptr.lock( ));
                    if( lck ) {
                        routes_.erase( addr );
                        tmp_clients_.erase( uint_cast( deleg ) );
                    }
                } );
        }

        void register_client( client_delegate *deleg )
        {
            weak_type wptr( shared_from_this( ) );

            dispatch(
                [this, wptr, deleg]( )
                {
                    auto lck(wptr.lock( ));
                    if( lck ) {
                        auto id = uint_cast( deleg );
                        auto f = tmp_clients_.find( id );
                        if( f != tmp_clients_.end( ) ) {

                            auto inst = f->second;
                            tmp_clients_.erase( f );

                            auto addr = inst->my_ip_;
                            routes_.insert( std::make_pair( addr, inst ) );
                        }
                    }
                } );
        }

        void on_read( char *data, size_t length ) override
        {

        }

        void on_read_error( const error_code & ) override
        {

        }

        void on_write_error( const error_code & ) override
        {

        }

        std::uint32_t next_ip4( )
        {
            return poll_.next( );
        }

        void on_write_exception(  ) override
        {
            throw;
        }

        application                *app_ = nullptr;
        utilities::address_v4_poll  poll_;

        routev4_map                 routes_;
        client_set                  tmp_clients_;

        std::string                 device_name_;
        noname::server::server_sptr server_;

    };

    using device_sptr = std::shared_ptr<device>;
    using device_map  = std::map<std::string, device_sptr>;

    ///////////// CLIENT IMPL

    void client_delegate::on_message_ready( tag_type, buffer_type,
                                            const_buffer_slice )
    { }

    void client_delegate::on_close( )
    {
        //on_close_( );
        my_device_->del_client( this );
    }

    //////////////////

    template <typename Acceptor>
    struct server_point {

        using convertor    = noname::acceptor_to_size_policy<Acceptor>;
        using server_sptr  = noname::server::server_sptr;
        using device_sptr  = std::shared_ptr<device>;
        using client_proto = client_delegate;
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

        using transport_type = noname::server::transport_type;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void on_new_client( device *d, transport_type *c,
                            std::string addr, std::uint16_t svc, bool udp )
        {
            try {

                auto prot = std::make_shared<client_delegate>( 2048 );
                c->set_delegate( prot.get( ) );
                prot->my_device_ = d;
                prot->assign_transport( c );
                d->add_tmp_client( prot );

            } catch( const std::exception &ex ) {
                LOGERR << "Failed to create protocol for client "
                       << addr << ":" << svc << "; " << ex.what( );
            }
        }

        void on_error( device *d, const noname::server::error_code &e )
        {
            std::cout << "Error " << e.message( ) << "\n";
        }

        void on_close( device *d )
        {
            std::cout << "Close\n";
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

                    auto dev = device::create( app_, inf );

                    {
                        std::lock_guard<std::mutex> lck(devs_lock_);
                        auto res = devs_.insert(
                                    std::make_pair(dev->device_name_, dev) );
                        if( !res.second ) {
                            LOGERR << "Device is already open "
                                   << quote(inf.device);
                            return false;
                        }
                    }

                    dev->server_ = svc;
                    auto devptr  = dev.get( );
                    bool is_udp  = inf.udp;

                    svc->assignt_accept_call(
                        [this, devptr, is_udp]( transport_type *t,
                                                const std::string &addr,
                                                std::uint16_t port )
                        {
                            this->on_new_client( devptr, t, addr, port,
                                                 is_udp );
                        } );

                    svc->assignt_error_call(
                        [this, devptr]( const error_code &e )
                        {
                            this->on_error( devptr, e );
                        } );

                    svc->assignt_close_call(
                        [this, devptr]( )
                        {
                            this->on_close( devptr );
                        } );

                    if( start ) {
                        dev->start_read( );
                        LOGERR << "Starting endpoint " << quote(inf.point);
                        svc->start( );
                    }

                } else {
                    LOGERR << "Invalid endpoint format: " << quote(inf.point);
                    return false;
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
            for( auto &d: devs_ ) {
                d.second->start_read( );
                d.second->server_->start( );
            }
        }

        application  *app_;
        listener2    *parent_;
        logger_impl  &log_;

        device_map    devs_;
        std::mutex    devs_lock_;
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

