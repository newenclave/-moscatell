#ifndef LOWLEVELPROTOCOLSERVER_H
#define LOWLEVELPROTOCOLSERVER_H

#include "vtrc-common/vtrc-lowlevel-protocol-iface.h"

#include "application.h"

namespace msctl { namespace agent { namespace lowlevel {

    vtrc::common::lowlevel::protocol_layer_iface *server_proto( application * );

}}}

#endif // LOWLEVELPROTOCOLSERVER_H
