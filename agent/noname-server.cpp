
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

//    template <typename SizePolicy>
//    struct client_delegate: public protocol_type<SizePolicy> {

//        using this_type          = client_delegate<SizePolicy>;
//        using parent_type        = protocol_type<SizePolicy>;
//        using size_policy        = SizePolicy;

//        using tag_type           = typename parent_type::tag_type;
//        using buffer_type        = typename parent_type::buffer_type;
//        using const_buffer_slice = typename parent_type::const_buffer_slice;
//        using buffer_slice       = typename parent_type::buffer_slice;
//        using tag_policy         = typename parent_type::tag_policy;

//        using track_type         = std::shared_ptr<void>;
//        using track_weak         = std::weak_ptr<void>;

//        using call_type = std::function<void (tag_type,
//                                              buffer_type,
//                                              const_buffer_slice)>;

//        using cache_type        = srpc::common::cache::simple<lowlevel_type>;
//        using buf_cache_type    = srpc::common::cache::simple<std::string>;
//        using rpc_service_sptr  = vtrc::common::rpc_service_wrapper_sptr;
//        using service_cache     = std::map<std::string, rpc_service_sptr>;

//        using message_lite      = google::protobuf::MessageLite;

//        ~client_delegate( )
//        {
//            //std::cout << "client_delegate dtor"  << std::endl;
//        }

//        client_delegate( size_t maxlen )
//            :parent_type(maxlen)
//            ,message_cache_(10)
//            ,buf_cache_(10)
//        {
//            call_ = [this]( tag_type t,
//                            buffer_type b,
//                            const_buffer_slice c)
//            {
//                init_call( t, b, c );
//            };
//        }

//        void set_default_call(  )
//        {
//            call_ = [this]( tag_type t,
//                            buffer_type b,
//                            const_buffer_slice c)
//            {
//                on_message_ready( t, b, c );
//            };
//        }

//        buffer_slice prepare_message( buffer_type buf, tag_type tag,
//                                      const message_lite &mess )
//        {
//            typedef typename parent_type::size_policy size_policy;

//            buf->resize( size_policy::max_length );

//            const size_t old_len   = buf->size( );
//            const size_t hash_size = this->hash( )->length( );

//            tag_policy::append( tag, *buf );
//            mess.AppendToString( buf.get( ) );

//            buf->resize( buf->size( ) + hash_size );

//            this->hash( )->get( buf->c_str( ) + old_len,
//                                buf->size( )  - old_len - hash_size,
//                             &(*buf)[buf->size( )    - hash_size]);

//            buffer_slice res( &(*buf)[old_len],
//                                 buf->size( ) - old_len );

//            buffer_slice packed = this->pack_message( buf, res );

//            return this->insert_size_prefix( buf, packed );
//        }

//        virtual buffer_type unpack_message( const_buffer_slice & )
//        {
//            return buffer_type( );
//        }

//        virtual buffer_slice pack_message( buffer_type, buffer_slice slice )
//        {
//            return slice;
//        }

//        void init_call( tag_type, buffer_type, const_buffer_slice )
//        {

//        }

//        void on_message_ready( tag_type, buffer_type /*buf*/,
//                               const_buffer_slice slice )
//        {
//            auto ll = message_cache_.get( );
//            ll->ParseFromArray( slice.data( ), slice.size( ) );

//            if( ll->id( ) > 100 ) {
//                if( ll->call( ) == "push" ) {
//                    me_->get_calls( )->push( ll->body( ).c_str( ),
//                                             ll->body( ).size( ) );
//                } else if( ll->call( ) == "reg" ) {
//                    client_info::register_info inf;
//                    me_->get_calls( )->register_me( inf );
//                }
//            } else {

//            }
//        }

//        void on_close( )
//        {
//            on_close_( );
//        }

//        application     *app_;
//        void_call        on_close_;
//        call_type        call_;
//        cache_type       message_cache_;
//        buf_cache_type   buf_cache_;
//        client_info     *me_ = nullptr;
//    };

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
                    lck->accept_call_( c, addr, svc );
                    lck->acceptor_->start_accept( );
                }
            }

            void on_accept_error( const error_code &e )
            {
                srpc::shared_ptr<parent_type> lck(lst_.lock( ));
                if( lck ) {
                    lck->error_call_( e );
                }
            }

            void on_close( )
            {
                srpc::shared_ptr<parent_type> lck(lst_.lock( ));
                if( lck ) {
                    lck->close_call_( );
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
        server_sptr create( application *app,
                            std::string addr, std::uint16_t port )
        {
            auto inst = impl<noname::tcp_acceptor>::create( app, addr, port );
            return inst;
        }
    }

    namespace udp {
        server_sptr create( application *app,
                            std::string addr, std::uint16_t port )
        {
            auto inst = impl<noname::udp_acceptor>::create( app, addr, port );
            return inst;
        }
    }

}


}}}

