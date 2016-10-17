#ifndef SERVER_APPLICATION_H
#define SERVER_APPLICATION_H

#include "common/utilities.h"
#include "common/logger.hpp"
#include "common/subsys-root.h"
#include "vtrc-common/vtrc-pool-pair.h"

namespace msctl { namespace agent {

    class application: public common::subsys_root {

        using parent_type = common::subsys_root;
        vtrc::common::pool_pair &pp_;



    public:

        application( vtrc::common::pool_pair &pp );

        template <typename S, typename ...Agrs>
        void subsys_add( Agrs && ...args )
        {
            parent_type::subsys_add<S>( this, std::forward<Agrs>(args)... );
        }

        application( );
        ~application( );

    };

}}


#endif // APPLICATION_H
