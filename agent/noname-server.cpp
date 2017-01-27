#if 0

#include "noname-server.h"
#include "srpc/server/acceptor/async/tcp.h"
#include "srpc/server/acceptor/async/udp.h"

#include "srpc/common/protocol/noname.h"
#include "srpc/common/sizepack/varint.h"
#include "srpc/common/sizepack/fixint.h"
#include "srpc/common/sizepack/none.h"

#include "application.h"

namespace msctl { namespace agent { namespace noname {

namespace {

    using namespace srpc;


    template<typename SizePolicy>
    using client_delegate = common::protocol::noname<SizePolicy>;

    using udp_size_policy = common::sizepack::none;
    using tcp_size_policy = common::sizepack::varint<size_t>;

    template<typename SizePolicy>
    struct protoimpl: public client_delegate<SizePolicy> {

        using parent_type        = client_delegate<SizePolicy>;
        using this_type          = protoimpl<SizePolicy>;
        using transport_type     = srpc::common::transport::interface;
        using tag_type           = client_delegate::tag_type;
        using buffer_type        = client_delegate::buffer_type;
        using const_buffer_slice = client_delegate::const_buffer_slice;
        using buffer_slice       = client_delegate::buffer_slice;
        using cache_type         = common::cache::simple<std::string>;
        using io_service         = SRPC_ASIO::io_service;
        using cb_type            = transport_type;

    public:

        protoimpl( io_service &ios, application *app )
            :client_delegate(true, 44000)
            ,ios_(ios)
            ,app_(app)
        { }

        static
        srpc::shared_ptr<this_type> create( io_service &ios )
        {
            srpc::shared_ptr<this_type> inst
                    = srpc::make_shared<this_type>( srpc::ref(ios) );
            inst->track( inst );
            inst->init( );
            inst->set_ready( true );
            return inst;
        }

        void init( )
        {
//            test::run rr;
//            set_default_call( );
//            send_message( rr );
        }

        void on_close( )
        {

        }

        virtual
        service_sptr get_service( const message_type & )
        {
            //std::cout << "request service!\n";
            return svc_;// create_svc( );
        }

        io_service  &ios_;
        application *app_;
    };

    template <typename AcceptorType>
    struct impl: public interface {

        typedef AcceptorType                        acceptor_type;
        typedef typename acceptor_type::endpoint    endpoint;
        typedef srpc::shared_ptr<acceptor_type>     acceptor_sptr;
        typedef srpc::common::transport::interface  transport_type;

        struct accept_delegate: public server::acceptor::interface::delegate {

            typedef impl<AcceptorType> parent_type;

            void on_accept_client( transport_type *c,
                                   const std::string &addr, srpc::uint16_t svc )
            {

                srpc::shared_ptr<parent_type> lck(lst_.lock( ));
                if( lck ) {

                    lck->on_accept( c, addr, svc );
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

            srpc::weak_ptr<parent_type> lst_;
        };

        //friend class accept_delegate;

        static endpoint epcreate( const std::string &addr, srpc::uint16_t port )
        {
            return endpoint( SRPC_ASIO::ip::address::from_string(addr), port );
        }

        impl( SRPC_ASIO::io_service &ios,
              const std::string &addr, srpc::uint16_t port )
            :ios_(ios)
            ,ep_(epcreate(addr, port))
        { }

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

    class server {

    };

}}}

#endif
