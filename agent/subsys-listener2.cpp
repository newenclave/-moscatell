
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

    using namespace SRPC_ASIO::ip;

    using size_policy           = noname::tcp_size_policy;
    using server_create_info    = listener2::server_create_info;
    using error_code            = noname::error_code;
    using transport_type        = noname::server::transport_type;

    namespace uip   = utilities::ip;
    namespace uipv4 = uip::v4;

    struct device;

    struct client_delegate: public noname::transport_delegate {

        using parent_type = noname::transport_delegate;

        client_delegate( application *app, size_t mexlen )
            :parent_type( mexlen )
            ,app_(app)
        {
            calls_["init"] = [this]( message_sptr &mess )
                             { return on_init( mess ); };
            calls_["reg"] = [this]( message_sptr &mess )
                            { return on_register_me( mess ); };
        }

        void send_on_register( std::uint32_t addr, std::uint32_t mask,
                               const std::string &name );

        bool on_init( message_sptr &mess )
        {
            send_message( mess );
            mcache_.push( mess );
        }

        bool on_register_me( message_sptr &mess );
        bool on_push( message_sptr &mess );

        void on_message_ready( tag_type, buffer_type,
                               const_buffer_slice ) override;

        void on_close( ) override;

        buffer_type unpack_message( const_buffer_slice & ) override
        {
            return buffer_type( );
        }

        buffer_slice pack_message( buffer_type, buffer_slice slice ) override
        {
            return slice;
        }

        application             *app_;
        std::shared_ptr<device>  my_device_;

        std::uint32_t   my_ip_   = 0;
        std::uint16_t   my_mask_ = 0;
        std::string     name_;

    };

    using delegate_sptr = std::shared_ptr<client_delegate>;
    using delegate_wptr = std::weak_ptr<client_delegate>;

    ///////////// DEVICE
    struct device: public common::tuntap_transport {

        using this_type   = device;
        using routev4_map = std::map<std::uint32_t, delegate_sptr>;
        using client_set  = std::map<std::uintptr_t, delegate_sptr>;
        using ipcache_map = std::map<std::string, std::uint32_t>;
        using parent_type = common::tuntap_transport;

        device( application *app, utilities::address_v4_poll poll )
            :common::tuntap_transport( app->get_io_service( ), 2048,
                                       parent_type::OPT_DISPATCH_READ )
            ,app_(app)
            ,log_(app->log( ))
            ,poll_(poll)
        { }

        ~device( )
        {
            LOGINF << "Destroy device " << device_name_
                   << " address: " << addr_.to_string( )
                   << " mask: " << mask_.to_string( )
                   ;
        }

        static
        std::shared_ptr<device> create( application *app,
                                        const server_create_info &inf )
        {
            auto &log_(app->log( ));
            auto inst = std::make_shared<device>( app, inf.addr_poll );
            auto hdl  = common::open_tun( inf.device );

            auto addr_mask = common::iface_v4_addr( inf.device );

            inst->addr_ = address_v4( htonl( addr_mask.first  ) );
            inst->mask_ = address_v4( htonl( addr_mask.second ) );

            inst->LOGINF << "Got ip for " << quote( inf.device )
                   << " " << quote( inst->addr_.to_string( ) )
                   << " mask " << quote( inst->mask_.to_string( ) )
                      ;

            inst->device_name_ = hdl.name( );
            inst->get_stream( ).assign( hdl.release( ) );

            LOGINF << "Create new device " << quote(inf.device)
                   << " address: " << inst->addr_.to_string( )
                   << " mask: " << inst->mask_.to_string( )
                   ;
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
            auto mess = std::make_shared<noname::message_type>( );
            mess->set_call( "push" );
            mess->set_body( data, length );
            auto srcdst = common::extract_ip_v4( data, length );
            if( srcdst.second ) {

                //std::cerr << std::hex << (srcdst.second & 0xFF000000) << "\n";

                if( uipv4::is_multicast( ntohl( srcdst.second ) ) ) {

                    for( auto &r: routes_ ) {
                        r.second->send_message( mess );
                    }
                } else {
                    auto f = routes_.find( srcdst.second );
                    if( f != routes_.end( ) ) {
                        f->second->send_message( mess );
                    }
                }
            }

        }

        void on_read_error( const error_code & ) override
        {

        }

        void on_write_error( const error_code & ) override
        {

        }

        void on_write_exception(  ) override
        {
            throw;
        }

        application                  *app_ = nullptr;
        logger_impl                  &log_;
        utilities::address_v4_poll    poll_;

        routev4_map                   routes_;
        client_set                    tmp_clients_;

        std::string                   device_name_;

        ipcache_map                   ips_;

        address                       addr_;
        address                       mask_;
    };

    using device_sptr = std::shared_ptr<device>;
    using device_wptr = std::weak_ptr<device>;
    using device_map  = std::map<std::string, device_wptr>;

    using servers_map = std::map<std::string, noname::server::server_sptr>;

    ///////////// CLIENT IMPL

    void client_delegate::on_message_ready( tag_type, buffer_type,
                                            const_buffer_slice slice )
    {
        auto mess = mcache_.get( );
        mess->ParseFromArray( slice.data( ),
                              slice.size( ) );
        //std::cout << "Got message " << mess->DebugString( ) << std::endl;
        call( mess );
        //get_transport( )->close( );
    }

    bool client_delegate::on_register_me( message_sptr &mess )
    {

        my_ip_       = my_device_->poll_.next( );
        my_mask_     = htonl( my_device_->poll_.mask( ) );
        auto my_addr = htonl( my_device_->addr_.to_v4( ).to_ulong( ) );

        mess->set_call( "regok" );
        if( my_ip_ == 0 ) {
            mess->clear_body( );
            mess->mutable_err( )->set_mess( "Server is full." );
            send_message( mess );
            return false;
        };

        rpc::tuntap::register_req req;
        req.ParseFromString( mess->body( ) );

        name_ = req.name( );

        rpc::tuntap::register_res res;
        res.mutable_iface_addr( )->set_v4_saddr( htonl(my_ip_) );
        res.mutable_iface_addr( )->set_v4_mask ( my_mask_ );
        res.mutable_iface_addr( )->set_v4_daddr( my_addr  );

        mess->clear_err( );
        mess->mutable_body( )->assign( res.SerializeAsString( ) );

        send_message( mess );

        my_device_->register_client( this );

        calls_["push"] = [this]( message_sptr &mess )
                         { return on_push( mess ); };
    }

    bool client_delegate::on_push( message_sptr &mess )
    {
        my_device_->write( mess->body( ) );
        mcache_.push( mess );
        return true;
    }

    void client_delegate::on_close( )
    {
        //on_close_( );
        my_device_->del_client( this );
    }

    //////////////////

}

    struct listener2::impl {

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void on_new_client( device_sptr dev, transport_type *c,
                            std::string addr, std::uint16_t svc, bool /*udp*/ )
        {
            try {

                auto prot = std::make_shared<client_delegate>( app_, 2048 );
                c->set_delegate( prot.get( ) );
                prot->my_device_ = dev;
                prot->assign_transport( c );
                dev->add_tmp_client( prot );

            } catch( const std::exception &ex ) {
                LOGERR << "Failed to create protocol for client "
                       << addr << ":" << svc << "; " << ex.what( );
            }
        }

        void on_error( device_sptr dev, const noname::server::error_code &e )
        {
            std::cout << "Error " << e.message( ) << "\n";
        }

        void on_close( device_sptr dev )
        {
            std::cout << "Close\n";
        }

        device_sptr get_device( const listener2::server_create_info &inf )
        {
            device_sptr dev;
            std::lock_guard<std::mutex> lck(devs_lock_);
            auto f = devs_.find( inf.device );
            if( f != devs_.end( ) ) {
                dev = f->second.lock( );
                if( !dev ) {
                    dev = device::create( app_, inf );
                    f->second = dev;
                }
            } else {
                dev = device::create( app_, inf );
                devs_[inf.device] = dev;
            }
            return dev;
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

                    auto dev = get_device( inf );

                    {
                        std::lock_guard<std::mutex> lck(serv_lock_);
                        auto res = serv_.insert(
                                         std::make_pair(inf.point, svc) );
                        if( !res.second ) {
                            LOGERR << "Server is already open "
                                   << quote(inf.point);
                            return false;
                        }
                    }

                    bool is_udp  = inf.udp;

                    svc->assignt_accept_call(
                        [this, dev, is_udp]( transport_type *t,
                                             const std::string &addr,
                                             std::uint16_t port )
                        {
                            this->on_new_client( dev, t, addr, port,
                                                 is_udp );
                        } );

                    svc->assignt_error_call(
                        [this, dev]( const error_code &e )
                        {
                            this->on_error( dev, e );
                        } );

                    svc->assignt_close_call(
                        [this, dev]( )
                        {
                            this->on_close( dev );
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
                auto dev = d.second.lock( );
                if( dev ) {
                    dev->start_read( );
                }
            }

            for( auto &d: serv_ ) {
                d.second->start( );
            }
        }

        application  *app_;
        listener2    *parent_;
        logger_impl  &log_;

        device_map    devs_;
        std::mutex    devs_lock_;

        servers_map   serv_;
        std::mutex    serv_lock_;
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

