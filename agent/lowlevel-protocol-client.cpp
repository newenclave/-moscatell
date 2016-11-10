
#include "vtrc-common/vtrc-lowlevel-protocol-default.h"

namespace msctl { namespace agent { namespace lowlevel {

    namespace {
        namespace vcomm = vtrc::common;

        class impl: public vcomm::lowlevel::default_protocol {

        };
    }

    vtrc::common::lowlevel::protocol_layer_iface *client_proto( )
    {
        return new impl;
    }



}}}

