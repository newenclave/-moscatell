
#ifndef SUBSYS_logging_H
#define SUBSYS_logging_H

#include <vector>

#include "application.h"

namespace msctl { namespace agent {

    class logging: public common::subsys_iface {

        struct  impl;
        impl   *impl_;

    protected:

        logging( application *app );

    public:

        ~logging( );

        typedef std::shared_ptr<logging> shared_type;
        static shared_type create( application *app,
                                   const std::vector<std::string> &def,
                                   std::int32_t flush_timeout );
        static shared_type create( application *app );

        void add_logger_output( const std::string &params, bool safe = true );
        void del_logger_output( const std::string &name );

    public:

        static const char* name( )
        {
            return "logging";
        }

        void init( )  override;
        void start( ) override;
        void stop( )  override;
    };

}}

#endif

    
