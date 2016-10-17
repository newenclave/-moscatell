#ifndef TUNTAP_H
#define TUNTAP_H

#include <string>

namespace msctl { namespace common {

#ifndef _WIN32
    int open_tun( const std::string &name );
    int open_tap( const std::string &name );
#else

#endif
}}

#endif // TUNTAP_H
