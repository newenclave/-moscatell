#ifndef LOWLEVELPROTOCOLSERVER_H
#define LOWLEVELPROTOCOLSERVER_H

#include "vtrc-common/vtrc-lowlevel-protocol-iface.h"

#include "application.h"

namespace msctl { namespace agent { namespace lowlevel {

    struct server_proto_option {
        std::string hello_message;
    };

    vtrc::common::lowlevel::protocol_layer_iface *server_proto( application *a,
                                            const server_proto_option &opts );

}}}

#endif // LOWLEVELPROTOCOLSERVER_H
