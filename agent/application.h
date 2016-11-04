#ifndef SERVER_APPLICATION_H
#define SERVER_APPLICATION_H

#include "common/utilities.h"
#include "common/logger.hpp"
#include "common/subsys-root.h"
#include "common/logger-impl.h"

#include "vtrc-common/vtrc-pool-pair.h"
#include "vtrc-common/vtrc-rpc-service-wrapper.h"
#include "vtrc-common/vtrc-connection-iface.h"
#include "vtrc-server/vtrc-application.h"

#include "boost/program_options.hpp"

#include <mutex>

namespace msctl { namespace agent {

    class application: public vtrc::server::application {

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

        typedef std::function<
            service_wrapper_sptr ( application *,
                                   vtrc::common::connection_iface_wptr )
        > service_getter_type;

    private:

        using parent_type = common::subsys_root;

        vtrc::common::pool_pair                     &pp_;
        logger_impl                                  logger_;
        std::map<std::string, service_getter_type>   services_;
        std::mutex                                   services_lock_;
        common::subsys_root                          subsystems_;
        boost::program_options::variables_map        cmd_options_;

        std::uint32_t                                io_pools_  = 1;
        std::uint32_t                                rpc_pools_ = 1;

        std::string                                  name_;

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

        parent_service_sptr get_service_by_name(
                                      vtrc::common::connection_iface* c,
                                      const std::string &service_name );

        ////////////// SUBSYSTEMS //////////////
        template <typename S, typename ...Agrs>
        void subsys_add( Agrs && ...args )
        {
            subsystems_.subsys_add<S>( this, std::forward<Agrs>(args)... );
        }

        template <typename T>
        T &subsys( )
        {
            return subsystems_.subsys<T>( );
        }

        void start( )
        {
            subsystems_.start( );
        }

        void stop( )
        {
            subsystems_.stop( );
        }

        void init( )
        {
            subsystems_.init( );
        }

        boost::program_options::variables_map &cmd_opts( )
        {
            return cmd_options_;
        }

        const boost::program_options::variables_map &cmd_opts( ) const
        {
            return cmd_options_;
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

        const std::string &name( ) const
        {
            return name_;
        }

        void set_name( const std::string &val )
        {
            name_ = val;
        }

        std::uint32_t io_pools( ) const { return io_pools_; }
        void set_io_pools( std::uint32_t val ) { io_pools_ = val; }

        std::uint32_t rpc_pools( ) const { return rpc_pools_; }
        void set_rpc_pools( std::uint32_t val ) { rpc_pools_ = val; }

        void quit( );

    };

}}


#endif // APPLICATION_H
