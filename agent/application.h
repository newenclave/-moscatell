#ifndef SERVER_APPLICATION_H
#define SERVER_APPLICATION_H

#include "common/utilities.h"
#include "common/logger.hpp"
#include "common/subsys-root.h"
#include "common/logger-impl.h"

#include "vtrc-common/vtrc-pool-pair.h"
#include "vtrc-common/vtrc-rpc-service-wrapper.h"
#include "vtrc-common/vtrc-connection-iface.h"

#include <mutex>

namespace msctl { namespace agent {

    class application: public common::subsys_root {

    public:

        class service_wrapper_impl: public vtrc::common::rpc_service_wrapper {

            application *app_;
            vtrc::common::connection_iface_wptr client_;

            typedef vtrc::common::rpc_service_wrapper super_type;

        public:

            typedef super_type::service_type service_type;
            typedef super_type::service_ptr  service_ptr;
            typedef super_type::service_sptr service_sptr;

            typedef super_type::method_type  method_type;

            service_wrapper_impl( application *app,
                                  vtrc::common::connection_iface_wptr c,
                                  service_sptr serv );

            ~service_wrapper_impl( );

        protected:

            const method_type *get_method ( const std::string &name ) const;
            application *get_application( );
            const application *get_application( ) const;
        };

        typedef service_wrapper_impl service_wrapper;
        typedef std::shared_ptr<service_wrapper> service_wrapper_sptr;

        typedef vtrc::function<
            service_wrapper_sptr ( application *,
                                   vtrc::common::connection_iface_wptr )
        > service_getter_type;

    private:

        using parent_type = common::subsys_root;

        vtrc::common::pool_pair                     &pp_;
        logger_impl                                  logger_;
        std::map<std::string, service_getter_type>   services_;
        std::mutex                                   services_lock_;

    public:

        application( vtrc::common::pool_pair &pp );
        ~application( );


        typedef vtrc::common::rpc_service_wrapper     parent_service_type;
        typedef vtrc::shared_ptr<parent_service_type> parent_service_sptr;

        ///
        /// func( app, connection )
        ///

        service_wrapper_sptr wrap_service (
                                    vtrc::common::connection_iface_wptr c,
                                    service_wrapper_impl::service_sptr serv );
        void register_service_factory( const std::string &name,
                                       service_getter_type func );

        void unregister_service_factory( const std::string &name );

        template <typename S, typename ...Agrs>
        void subsys_add( Agrs && ...args )
        {
            parent_type::add_subsys<S>( this, std::forward<Agrs>(args)... );
        }

        template <typename S>
        void subsys_add( )
        {
            parent_type::add_subsys<S>( this );
        }

        static std::uint64_t now( );
        static std::uint64_t tick_count( );
        static std::uint64_t start_tick( );

        vtrc::common::pool_pair &pools( )
        {
            return pp_;
        }

        logger_impl &log( )
        {
            return logger_;
        }

        const logger_impl &log( ) const
        {
            return logger_;
        }

        void quit( );

    };

}}


#endif // APPLICATION_H
