#ifndef SUBSYS_IFACE_H
#define SUBSYS_IFACE_H

#include <string>
#include <memory>

namespace msctl { namespace common {

    struct subsys_iface: public std::enable_shared_from_this<subsys_iface> {

        subsys_iface( ) { }
        subsys_iface( const subsys_iface& )               = delete;
        subsys_iface & operator = ( const subsys_iface& ) = delete;
        subsys_iface( subsys_iface && )                   = delete;
        subsys_iface & operator = ( subsys_iface && )     = delete;

        virtual ~subsys_iface( ) { }

        virtual void init( )              = 0;
        virtual void start( )             = 0;
        virtual void stop( )              = 0;
    };

    using subsys_ptr  = subsys_iface *;
    using subsys_sptr = std::shared_ptr<subsys_iface>;
    using subsys_uptr = std::unique_ptr<subsys_iface>;

}}

#endif // SUBSYSIFACE_H
