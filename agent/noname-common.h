#ifndef MSCTL_NONAME_COMMON_H
#define MSCTL_NONAME_COMMON_H

#include <functional>

#include "protocol/tuntap.pb.h"
#include "srpc/common/protocol/binary.h"

#include "srpc/client/connector/async/tcp.h"
#include "srpc/client/connector/async/udp.h"

#include "srpc/server/acceptor/async/tcp.h"
#include "srpc/server/acceptor/async/udp.h"

namespace msctl { namespace agent { namespace noname {

    using error_code       = srpc::common::transport::error_code;
    using transport_type   = srpc::common::transport::interface;
    using message_type     = msctl::rpc::tuntap::tuntap_message;
    using message_sptr     = std::shared_ptr<message_type>;

    using tcp_size_policy  = srpc::common::sizepack::varint<std::uint64_t>;
    using udp_size_policy  = srpc::common::sizepack::none;

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


    using tcp_acceptor = srpc::server::acceptor::async::tcp;
    using udp_acceptor = srpc::server::acceptor::async::udp;

    template <typename T>
    struct acceptor_to_size_policy;

    template <>
    struct acceptor_to_size_policy<tcp_acceptor> {
        using policy = tcp_size_policy;
        static const size_t maxlen = 4 * 1024;
    };

    template <>
    struct acceptor_to_size_policy<udp_acceptor> {
        using policy = udp_size_policy;
        static const size_t maxlen = 45 * 1024;
    };

    template <typename SizePack>
    using protocol_type = srpc::common
                              ::protocol::binary<SizePack, tcp_size_policy>;

    template <typename T>
    std::uintptr_t uint_cast( const T *val )
    {
        return reinterpret_cast<std::uintptr_t>(val);
    }

    struct transport_delegate: public noname::protocol_type<tcp_size_policy> {

        using message            = google::protobuf::Message;
        using parent_type        = noname::protocol_type<size_policy>;
        using this_type          = transport_delegate;
        using tag_type           = typename parent_type::tag_type;
        using buffer_type        = typename parent_type::buffer_type;
        using const_buffer_slice = typename parent_type::const_buffer_slice;
        using buffer_slice       = typename parent_type::buffer_slice;

        using message_type       = noname::message_type;
        using message_sptr       = noname::message_sptr;

        using stub_type          = std::function<bool (message_sptr &)>;
        using call_map           = std::map<std::string, stub_type>;
        using void_call          = std::function<void ( )>;

        using bufer_cache        = srpc::common::cache::simple<std::string>;
        using message_cache      = srpc::common::cache::simple<message_type>;

        using callbacks          = transport_type::write_callbacks;

        transport_delegate( size_t mexlen )
            :parent_type( mexlen )
            ,bcache_(10)
            ,mcache_(10)
            ,next_tag_(0)
            ,next_id_(100)
        { }

        virtual ~transport_delegate( )
        { }

        bool call( message_sptr &mess )
        {
            auto f = calls_.find( mess->call( ) );
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

        template <typename Cb>
        void send_message( message_sptr &mess, Cb cb )
        {
            auto buf = bcache_.get( );
            auto slice = prepare_message( buf, *mess );

            auto rcb =  [this, buf, cb]( const error_code &e, size_t )
                        {
                            if( !e ) {
                                bcache_.push( buf );
                            }
                            cb( e );
                        };

            get_transport( )->write( slice.data( ), slice.size( ) ,
                                     callbacks::post( rcb ) );

            mcache_.push( mess );
        }

        void send_message( message_sptr &mess )
        {
            static const auto ccb = [ ](...){ };
            send_message( mess, ccb );
        }

        call_map        calls_;
        void_call       on_close_;

        bufer_cache     bcache_;
        message_cache   mcache_;

        std::atomic<std::uint64_t> next_tag_;
        std::atomic<std::uint64_t> next_id_;
    };


}}}

#endif
