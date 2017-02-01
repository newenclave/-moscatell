
#include "srpc/client/connector/async/tcp.h"
#include "srpc/client/connector/async/udp.h"

#include "application.h"

#include "srpc/common/protocol/binary.h"

#include "noname-client.h"
#include "noname-common.h"

#include "protocol/tuntap.pb.h"

#include "srpc/common/cache/simple.h"

namespace msctl { namespace agent { namespace noname {

namespace {

    namespace ip = SRPC_ASIO::ip;

    template <typename ConnectorType>
    struct impl: client::interface {

        using message               = google::protobuf::Message;
        using convertor_type        = connector_to_size_policy<ConnectorType>;
        using size_policy           = typename convertor_type::policy;
        static const size_t maxlen  = convertor_type::maxlen;

        using connector_type  = ConnectorType;
        using connector_sptr  = std::shared_ptr<connector_type>;
        using endpoint        = typename connector_type::endpoint;
        using acceptor_sptr   = srpc::shared_ptr<connector_type>;
        using transport_type  = srpc::common::transport::interface;
        using this_type       = impl<connector_type>;

        using connector_delegate = srpc::client::connector::interface::delegate;
        using parent_delegate    = protocol_type<size_policy>;

        impl( application *app, const std::string &svc, std::uint16_t port )
            :app_(app)
            ,ep_(ip::address::from_string(svc), port)
        { }

        struct delegate: public connector_delegate {

            using iface_ptr     = srpc::common::transport::interface *;
            using error_code    = srpc::common::transport::error_code;

            impl<ConnectorType> *parent_;

            delegate( )
            { }

            void on_connect( iface_ptr i )
            {
                parent_->on_connect_( i );
            }

            void on_connect_error( const error_code &err )
            {
                parent_->on_error_( err );
            }

            void on_close( )
            {
                parent_->on_disconnect_( );
            }
        };

        void init( )
        {
            std::unique_ptr<delegate> new_deleg(new delegate);

            auto new_conn = connector_type::create( app_->get_io_service( ),
                                                    maxlen, ep_ );

            new_deleg->parent_ = this;
            new_conn->set_delegate( new_deleg.get( ) );

            deleg_.swap( new_deleg );
            connector_.swap( new_conn );
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

        application                 *app_;
        std::unique_ptr<delegate>    deleg_;
        connector_sptr               connector_;
        endpoint                     ep_;
    };
}

namespace client {
    namespace tcp {
        client_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port )
        {
            auto inst = std::make_shared<impl<tcp_connector> >( app, svc, port);
            inst->init( );
            return inst;
        }
    }

    namespace ucp {
        client_sptr create( application *app,
                            const std::string &svc,
                            std::uint16_t port )
        {
            auto inst = std::make_shared<impl<udp_connector> >( app, svc, port);
            inst->init( );
            return inst;
        }

    }
}

}}}

