#ifndef LOWLEVELPROTOCOLCLIENT_H
#define LOWLEVELPROTOCOLCLIENT_H

#include "vtrc-common/vtrc-lowlevel-protocol-iface.h"
#include "application.h"

namespace msctl { namespace agent { namespace lowlevel {

    vtrc::common::lowlevel::protocol_layer_iface *client_proto( application * );

}}}


#endif // LOWLEVELPROTOCOLCLIENT_H
