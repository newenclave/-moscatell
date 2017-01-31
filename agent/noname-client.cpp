
#include "srpc/client/connector/async/tcp.h"
#include "srpc/client/connector/async/udp.h"

#include "application.h"

#include "srpc/common/protocol/binary.h"

#include "noname-client.h"

#include "protocol/tuntap.pb.h"

#include "srpc/common/cache/simple.h"

namespace msctl { namespace agent { namespace noname {

namespace {

    using message_type    = msctl::rpc::tuntap::tuntap_message;
    using message_sptr    = std::shared_ptr<message_type>;

    using tcp_size_policy = srpc::common::sizepack::varint<size_t>;
    using udp_size_policy = srpc::common::sizepack::none;

    using tcp_connector    = srpc::client::connector::async::tcp;
    using udp_connector    = srpc::client::connector::async::udp;

    template <typename T>
    struct connector_to_size_policy;

    template <>
    struct connector_to_size_policy<tcp_connector> {
        using policy = tcp_size_policy;
        static const size_t maxlen = 8096;
    };

    template <>
    struct connector_to_size_policy<udp_connector> {
        using policy = udp_size_policy;
        static const size_t maxlen = 45 * 1024;
    };

    template <typename SizePack>
    using protocol_type = srpc::common::protocol::binary<SizePack>;

    using void_call = std::function<void (void)>;

    template <typename ConnectorType>
    struct impl: client::interface {

        using convertor_type        = connector_to_size_policy<ConnectorType>;
        using size_policy           = typename convertor_type::policy;
        static const size_t maxlen  = convertor_type::maxlen;

        using connector_type  =  ConnectorType;
        using connector_sptr  = std::shared_ptr<connector_type>;
        using endpoint        = typename connector_type::endpoint;
        using acceptor_sptr   = srpc::shared_ptr<connector_type>;
        using transport_type  = srpc::common::transport::interface;
        using this_type       = impl<connector_type>;

//        using cache_type      = srpc::common::cache::simple<lowlevel_type>;

        using connector_delegate = srpc::client::connector::interface::delegate;
        using parent_delegate    = protocol_type<size_policy>;

        impl( application *app, const std::string &svc, std::uint16_t port )
            :app_(app)
        { }

        struct protocol: public parent_delegate {

            using parent_type        = parent_delegate;
            using tag_type           = typename parent_type::tag_type;
            using buffer_type        = typename parent_type::buffer_type;
            using const_buffer_slice = typename parent_type::const_buffer_slice;
            using buffer_slice       = typename parent_type::buffer_slice;
            using track_type         = std::shared_ptr<void>;
            using track_weak         = std::weak_ptr<void>;

            protocol( size_t len )
                :parent_delegate( len )
            { }

            buffer_type unpack_message( const_buffer_slice & )
            {
                return buffer_type( );
            }

            buffer_slice pack_message( buffer_type, buffer_slice slice )
            {
                return slice;
            }

            void on_message_ready( tag_type t, buffer_type b,
                                   const_buffer_slice slice )
            {
            }

            impl<ConnectorType> *parent_;
            //cache_type           message_cache_;

        };

        using protocol_sptr = std::shared_ptr<protocol>;

        struct delegate: public connector_delegate {

            using iface_ptr     = srpc::common::transport::interface *;
            using error_code    = srpc::common::transport::error_code;

            impl<ConnectorType> *parent_;

            delegate( )
            { }

            void on_connect( iface_ptr i )
            {
                using convertor_type = connector_to_size_policy<ConnectorType>;
                static const size_t maxlen = convertor_type::maxlen;

                //parent_->transport_ = i->shared_from_this( );
//                parent_->proto_ =
//                        std::make_shared<protocol>( maxlen );
//                parent_->transport_->set_delegate( parent_->proto_.get( ) );

//                parent_->transport_->read( );
            }

            void on_connect_error( const error_code &err )
            {

            }

            void on_close( )
            {

            }
        };


        void init( )
        {
            connector_ =
                    std::make_shared<connector_type>(app_->get_io_service( ));
            deleg_->parent_ = connector_.get( );
            connector_->set_delegate( &deleg_ );
        }

        void start( )
        {
            connector_->open( );
            connector_->connect( );
        }

        void stop(  )
        {
            connector_->close( );
        }

        connector_type *connector( )
        {
            return connector_.get( );
        }

        application    *app_;
        delegate        deleg_;
        protocol_sptr   proto_;
        connector_sptr  connector_;
    };
}

namespace client {
    namespace tcp {
        client_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port )
        {
            auto inst = std::make_shared<impl<tcp_connector> >( app, svc, port);
            return inst;
        }
    }

    namespace ucp {
        client_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port )
        {
            auto inst = std::make_shared<impl<udp_connector> >( app, svc, port);
            return inst;
        }

    }
}

}}}

