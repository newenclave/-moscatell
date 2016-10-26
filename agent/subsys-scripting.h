
#ifndef SUBSYS_scripting_H
#define SUBSYS_scripting_H

#include "application.h"

namespace msctl { namespace agent {

    class scripting: public common::subsys_iface {

        struct          impl;
        friend struct   impl;
        impl           *impl_;

    public:

        scripting( application *app );
        static std::shared_ptr<scripting> create( application *app );
        static const char *name( ) 
        {
            return "scripting";
        }

        void run_config( const std::string &path );

    private:

        void init( )  override;
        void start( ) override;
        void stop( )  override;

    };

}}

#endif // SUBSYS_scripting_H

    
