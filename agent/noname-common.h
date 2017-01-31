#ifndef MSCTL_NONAME_COMMON_H
#define MSCTL_NONAME_COMMON_H

#include "protocol/tuntap.pb.h"
#include "srpc/common/protocol/binary.h"

#include "srpc/client/connector/async/tcp.h"
#include "srpc/client/connector/async/udp.h"

#include "srpc/server/acceptor/async/tcp.h"
#include "srpc/server/acceptor/async/udp.h"

namespace msctl { namespace agent { namespace noname {

    using message_type     = msctl::rpc::tuntap::tuntap_message;
    using message_sptr     = std::shared_ptr<message_type>;

    using tcp_size_policy  = srpc::common::sizepack::varint<size_t>;
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

}}}

#endif
