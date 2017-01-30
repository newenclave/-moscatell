
#include <memory>
#include <functional>

#include "noname-server.h"

#include "srpc/server/acceptor/async/tcp.h"
#include "srpc/server/acceptor/async/udp.h"
#include "srpc/server/acceptor/interface.h"

#include "srpc/common/sizepack/varint.h"
#include "srpc/common/sizepack/fixint.h"
#include "srpc/common/sizepack/none.h"
#include "srpc/common/cache/simple.h"

#include "vtrc-common/vtrc-rpc-service-wrapper.h"

#include "srpc/common/protocol/binary.h"

#include "protocol/tuntap.pb.h"

namespace msctl { namespace agent { namespace noname {

namespace {

    using message_type    = msctl::rpc::tuntap::tuntap_message;
    using message_sptr    = std::shared_ptr<message_type>;

    using tcp_size_policy = srpc::common::sizepack::varint<size_t>;
    using udp_size_policy = srpc::common::sizepack::none;

    using tcp_acceptor    = srpc::server::acceptor::async::tcp;
    using udp_acceptor    = srpc::server::acceptor::async::udp;

    template <typename T>
    struct acceptor_to_size_policy;

    template <>
    struct acceptor_to_size_policy<tcp_acceptor> {
        using policy = tcp_size_policy;
        static const size_t maxlen = 8096;
    };

    template <>
    struct acceptor_to_size_policy<udp_acceptor> {
        using policy = udp_size_policy;
        static const size_t maxlen = 45 * 1024;
    };

    template <typename SizePack>
    using protocol_type = srpc::common::protocol::binary<SizePack>;

    using void_call = std::function<void (void)>;

    using parent_calls = noname::client_info::calls_sptr;

    template <typename SizePolicy>
    struct client_delegate: public protocol_type<SizePolicy> {

        using this_type          = client_delegate<SizePolicy>;
        using parent_type        = protocol_type<SizePolicy>;
        using size_policy        = SizePolicy;

        using tag_type           = typename parent_type::tag_type;
        using buffer_type        = typename parent_type::buffer_type;
        using const_buffer_slice = typename parent_type::const_buffer_slice;
        using buffer_slice       = typename parent_type::buffer_slice;

        using track_type         = std::shared_ptr<void>;
        using track_weak         = std::weak_ptr<void>;

        using call_type = std::function<void (tag_type,
                                              buffer_type,
                                              const_buffer_slice)>;

        using cache_type        = srpc::common::cache::simple<lowlevel_type>;
        using rpc_service_sptr  = vtrc::common::rpc_service_wrapper_sptr;
        using service_cache     = std::map<std::string, rpc_service_sptr>;

        ~client_delegate( )
        {
            //std::cout << "client_delegate dtor"  << std::endl;
        }

        client_delegate( size_t maxlen )
            :parent_type(maxlen)
            ,message_cache_(10)
        {
            call_ = [this]( tag_type t,
                            buffer_type b,
                            const_buffer_slice c)
            {
                init_call( t, b, c );
            };
        }

        void set_default_call(  )
        {
            call_ = [this]( tag_type t,
                            buffer_type b,
                            const_buffer_slice c)
            {
                on_message_ready( t, b, c );
            };
        }

        virtual buffer_type unpack_message( const_buffer_slice & )
        {
            return buffer_type( );
        }

        virtual buffer_slice pack_message( buffer_type, buffer_slice slice )
        {
            return slice;
        }

        void init_call( tag_type, buffer_type, const_buffer_slice )
        {

        }

        void on_message_ready( tag_type, buffer_type buf,
                               const_buffer_slice slice )
        {
            auto ll = message_cache_.get( );
            ll->ParseFromArray( slice.data( ), slice.size( ) );

            if( ll->id( ) > 100 ) {

                rpc_service_sptr serv;

                auto f = cache_.find( ll->svc( ) );
                if( f == cache_.end( ) ) {
                    //serv = app_->get_service_by_name(  );
                } else {
                    serv = f->second;
                }

            } else {

            }
        }

        void on_close( )
        {
            on_close_( );
        }

        application  *app_;
        track_weak    track_;
        void_call     on_close_;
        call_type     call_;
        cache_type    message_cache_;
        service_cache cache_;
    };

    template <typename AcceptorType>
    struct client_info_impl: public client_info {

        using convertor     = acceptor_to_size_policy<AcceptorType>;
        using size_policy   = typename convertor::policy;
        using delegate_type = client_delegate<size_policy>;

        std::unique_ptr<delegate_type>  client_;
        transport_sptr                  t_;
        parent_calls                    calls_;

        ~client_info_impl( )
        {
            //std::cout << "client_info_impl dtor"  << std::endl;
        }

        transport_sptr transport( )
        {
            return t_;
        }

        bool post_message( lowlevel_sptr mess )
        {
            return false;
        }

        bool send_message( lowlevel_sptr mess )
        {
            return false;
        }

        lowlevel_sptr create_message( )
        {
            return client_->message_cache_.get( );
        }

        static
        std::shared_ptr<client_info_impl<AcceptorType> > create( )
        {
            return std::make_shared<client_info_impl<AcceptorType> >( );
        }

        void reset_delegate( transport_type *t )
        {
            t_ = t->shared_from_this( );
            client_.reset( new delegate_type( convertor::maxlen ) );
            client_->assign_transport( t );
            t->set_delegate( client_.get( ) );
        }

        void start( )
        {
            client_->get_transport( )->read( );
        }

        bool ready( )
        {
            return false;
        }

        void close( )
        {
            std::cout << "close!" << std::endl;
            t_.reset( );
        }

        void set_calls( noname::client_info::calls_sptr calls )
        {
            calls_ = calls;
        }

        noname::client_info::calls_sptr get_calls( )
        {
            return calls_;
        }

    };

    using namespace srpc;
    template <typename AcceptorType>
    struct impl: public server::interface {

        using acceptor_type   = AcceptorType;
        using endpoint        = typename acceptor_type::endpoint;
        using acceptor_sptr   = srpc::shared_ptr<acceptor_type>;
        using transport_type  = srpc::common::transport::interface;
        using this_type       = impl<acceptor_type>;
        using parent_delegete = srpc::server::acceptor::interface::delegate;

        struct accept_delegate: public parent_delegete {

            typedef impl<AcceptorType> parent_type;

            void on_accept_client( transport_type *c,
                                   const std::string &addr, srpc::uint16_t svc )
            {
                srpc::shared_ptr<parent_type> lck(lst_.lock( ));
                if( lck ) {

                    using my_client = client_info_impl<AcceptorType>;

                    auto cli = my_client::create( );
                    cli->reset_delegate( c );

                    auto plist = lst_;
                    std::weak_ptr<my_client> wcli = cli;

                    cli->client_->on_close_ = [plist, wcli]( ) {
                        srpc::shared_ptr<parent_type> lck(plist.lock( ));
                        if( lck ) {
                            lck->on_client_close( wcli.lock( ) );
                        }
                    };

                    lck->on_accept( cli, addr, svc );
                    lck->acceptor_->start_accept( );
                }
            }

            void on_accept_error( const error_code &e )
            {
                srpc::shared_ptr<parent_type> lck(lst_.lock( ));
                if( lck ) {
                    lck->on_accept_error( e );
                }
            }

            void on_close( )
            {
                srpc::shared_ptr<parent_type> lck(lst_.lock( ));
                if( lck ) {
                    lck->on_close( );
                }
            }

            srpc::weak_ptr<parent_type>  lst_;
            application                 *aps_;
        };

        static endpoint epcreate( const std::string &addr, srpc::uint16_t port )
        {
            return endpoint( SRPC_ASIO::ip::address::from_string(addr), port );
        }

        impl( SRPC_ASIO::io_service &ios,
              const std::string &addr, srpc::uint16_t port )
            :ios_(ios)
            ,ep_(epcreate(addr, port))
        { }

        static
        std::shared_ptr<this_type> create( application *app,
                                           const std::string &svc,
                                           std::uint16_t port )
        {
            std::shared_ptr<this_type> inst =
                    std::make_shared<this_type>( app->get_io_service( ),
                                                 svc, port );
            inst->init( );
            inst->delegate_.lst_ = inst;
            return inst;
        }

        void init( )
        {
            acceptor_ = acceptor_type::create( ios_, 4096, ep_ );
            acceptor_->set_delegate( &delegate_ );
        }

        void start( )
        {
            acceptor_->open( );
            acceptor_->bind( );
            acceptor_->start_accept( );
        }

        void stop( )
        {
            acceptor_->close( );
        }

        acceptor_type *acceptor( )
        {
            return acceptor_.get( );
        }

        SRPC_ASIO::io_service  &ios_;
        endpoint                ep_;
        bool                    nowait_;
        acceptor_sptr           acceptor_;
        accept_delegate         delegate_;
    };

}

namespace server {
    namespace tcp {
        std::shared_ptr<interface> create( application *app,
                                           const std::string &svc,
                                           std::uint16_t port )
        {
            return impl<tcp_acceptor>::create( app, svc, port );
        }
    }

    namespace udp {
        std::shared_ptr<interface> create( application *app,
                                           const std::string &svc,
                                           std::uint16_t port )
        {
            return impl<udp_acceptor>::create( app, svc, port );
        }
    }
}

}}}

