
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

    using namespace SRPC_ASIO::ip;
    using client_create_info = clients2::client_create_info;
    using utilities::decorators::quote;
    using error_code = srpc::common::transport::error_code;
    using io_service = SRPC_ASIO::io_service;

    using size_policy           = noname::tcp_size_policy;
    using error_code            = noname::error_code;
    using transport_type        = noname::client::transport_type;

    struct device;

    struct client_delegate: public noname::transport_delegate {

        using parent_type        = noname::transport_delegate;
        using this_type          = client_delegate;

        using push_call          = std::function<void(const char *, size_t)>;

        client_delegate( application *app, size_t mexlen )
            :parent_type( mexlen )
            ,app_(app)
        {
            push_ = [this]( ... ){ };
            calls_["init"] = [this]( message_sptr &mess )
                             { return on_ready( mess ); };
        }

        bool on_ready( message_sptr &mess )
        {
            calls_["push"] = [this]( message_sptr &mess )
                             { return on_push( mess ); };

            calls_["regok"] = [this]( message_sptr &mess )
                              { return on_register_ok( mess ); };

            push_ = [this]( const char * d, size_t l)
                    { send_impl( d, l ); };

            std::cout << "Ready!\n";
            send_register_me( mess );

            return true;
        }

        void send_register_me( message_sptr &mess );

        bool on_push( message_sptr &mess );
        bool on_register_ok( message_sptr &mess );

        void send( const char *data, size_t len )
        {
            push_( data, len );
        }

        void send_impl( const char *data, size_t len )
        {
            auto mess = mcache_.get( );
            mess->clear_err( );
            mess->set_call( "push" );
            mess->set_body( data, len );
            send_message( mess );
        }

        buffer_type unpack_message( const_buffer_slice & ) override
        {
            return buffer_type( );
        }

        buffer_slice pack_message( buffer_type, buffer_slice slice ) override
        {
            return slice;
        }

        void on_message_ready( tag_type /*t*/, buffer_type /*b*/,
                               const_buffer_slice sl )
        {
            auto mess = mcache_.get( );
            mess->ParseFromArray( sl.data( ), sl.size( ) );
            call( mess );
        }

        void init( )
        {
            message_sptr mess = mcache_.get( );
            mess->set_call( "init" );
            send_message( mess );
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

        application *app_;
        device      *my_device_ = nullptr;
        push_call    push_;
        bool         ready_     = false;
        std::string  name_;
//        std::condition_variable ready_var_;
//        std::mutex              ready_lock_;
    };

    using proto_sptr = std::shared_ptr<client_delegate>;

    struct device: public common::tuntap_transport {

        using parent_type = common::tuntap_transport;

        device( application *app )
            :common::tuntap_transport( app->get_io_service( ), 2048,
                                       parent_type::OPT_DISPATCH_READ )
            ,app_(app)
            ,log_(app->log( ))
        { }

        void on_read( char *data, size_t length )
        {
            proto_->send( data, length );
        }

        static
        std::shared_ptr<device> create( application *app,
                                        const client_create_info &inf )
        {
            auto inst = std::make_shared<device>( app );
            auto d = common::open_tun( inf.device );

            inst->cln_name_ = inf.id;
            inst->dev_name_ = d.name( );

            inst->get_stream( ).assign( d.release( ) );

            return inst;
        }

        void init( noname::client::client_sptr c )
        {

            c->assign_on_connect(
                [this]( transport_type *t )
                {
                    proto_ = std::make_shared<client_delegate>( app_, 4096 );
                    proto_->my_device_ = this;
                    proto_->assign_transport( t );
                    t->set_delegate( proto_.get( ) );
                    proto_->assign_transport( t );
                    proto_->init( );

                } );

            c->assign_on_error(
                [this]( const error_code & err )
                {
                    LOGERR << "Client error " << err.message( );
                } );

            c->assign_on_disconnect(
                [this](  )
                {

                } );

            client_.swap( c );

        }

        void start( )
        {
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

        application                    *app_;
        logger_impl                    &log_;
        proto_sptr                      proto_;
        noname::client::client_sptr     client_;
        std::string                     dev_name_;
        std::string                     cln_name_;

    };

    using device_sptr = std::shared_ptr<device>;
    using device_map  = std::map<std::string, device_sptr>;

    //////////////////////////// CLIENT IMPL

    bool client_delegate::on_push(message_sptr &mess )
    {
        my_device_->write( mess->body( ).c_str( ),
                           mess->body( ).size( ) );
        mcache_.push( mess );
        return true;
    }

    void client_delegate::send_register_me( message_sptr &mess )
    {
        mess->set_call( "reg" );
        rpc::tuntap::register_req req;
        req.set_name( my_device_->cln_name_ );
        mess->set_body( req.SerializeAsString( ) );

        send_message( mess );
    }

    bool client_delegate::on_register_ok( message_sptr &mess )
    {
        static auto &log_(app_->log( ));

        try {

            rpc::tuntap::register_res res;

            res.ParseFromString( mess->body( ) );

            auto naddr  = ntohl( res.iface_addr( ).v4_saddr( ));
            auto ndaddr = ntohl( res.iface_addr( ).v4_daddr( ));
            auto nmask  = ntohl( res.iface_addr( ).v4_mask( ));

            address_v4 saddr(naddr);
            address_v4 daddr(ndaddr);
            address_v4 mask(nmask);

            LOGINF << "Got address: "
                   << quote(saddr.to_string( )
                            + "->"
                            + daddr.to_string( ))
                   << " and mask: " << quote(mask.to_string( ))
                      ;

            auto hdl = my_device_->get_stream( ).native_handle( );

            clients2::register_info reginfo;

            reginfo.ip        = saddr.to_string( );
            reginfo.mask      = mask.to_string( );
            reginfo.server_ip = daddr.to_string( );

            common::setup_device( hdl, my_device_->dev_name_,
                                  reginfo.ip,
                                  reginfo.server_ip,
                                  reginfo.mask );

    //        cli->new_client_registered( clnt_->shared_from_this( ),
    //                                    *devhint_, reginfo );

            ready_ = true;
            my_device_->start_read( );

            LOGINF << "Device " << quote(my_device_->dev_name_)
                   << " setup success.";

            mcache_.push( mess );
        } catch( const std::exception &ex ) {
            LOGERR << "Device setup failed: " << ex.what( ) << "\n";
            get_transport( )->close( );
        }

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

