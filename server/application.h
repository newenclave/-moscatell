#ifndef SERVER_APPLICATION_H
#define SERVER_APPLICATION_H

#include "common/utilities.h"
#include "common/subsys-root.h"

namespace msctl { namespace server {

    class application: public common::subsys_root {

        using parent_type = common::subsys_root;

    public:

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
