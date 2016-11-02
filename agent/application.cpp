#include "application.h"

#include "google/protobuf/descriptor.h"

namespace msctl { namespace agent {

    namespace {
        namespace vcomm = vtrc::common;
        namespace gpb = google::protobuf;

        std::uint64_t s_time_point = 0;

    }

//////// service wrapper

    application::service_wrapper_impl::service_wrapper_impl( application *app,
                              vcomm::connection_iface_wptr c,
                              service_sptr serv)
        :vcomm::rpc_service_wrapper( serv )
        ,app_(app)
        ,client_(c)
    { }

    application::service_wrapper_impl::~service_wrapper_impl( )
    { }

    const
    application::service_wrapper_impl::method_type *
                application::service_wrapper_impl
                    ::get_method( const std::string &name ) const
    {
        const method_type* m = super_type::find_method( name );
        return m;
    }


    application *application::service_wrapper_impl::get_application( )
    {
        return app_;
    }

    const application *application::service_wrapper_impl
                                        ::get_application( ) const
    {
        return app_;
    }

    application::service_wrapper_sptr
        application::wrap_service( vcomm::connection_iface_wptr cl,
                                   service_wrapper_impl::service_sptr serv )
    {
        return std::make_shared<application::service_wrapper>( this, cl, serv );
    }

/////////////////////////////

    application::application(vtrc::common::pool_pair &pp)
        :vtrc::server::application(pp)
        ,pp_(pp)
        ,logger_(pp.get_io_service( ), logger_impl::level::debug)
    {
        s_time_point = application::now( );
    }

    application::~application( )
    { }

    std::uint64_t application::now( )
    {
        using std::chrono::duration_cast;
        using microsec = std::chrono::microseconds;
        auto n = std::chrono::high_resolution_clock::now( );
        return duration_cast<microsec>(n.time_since_epoch( )).count( );
    }

    std::uint64_t application::tick_count( )
    {
        return application::now( ) - s_time_point;
    }

    std::uint64_t application::start_tick( )
    {
        return s_time_point;
    }

    ///
    /// services
    ///
    void application::register_service_factory( const std::string &name,
                                                service_getter_type func )
    {
        vtrc::lock_guard<vtrc::mutex> lck(services_lock_);
        auto f = services_.find( name );
        if( f != services_.end( ) ) {
            std::ostringstream oss;
            oss << "Service '" << name << "'' already exists.";
            throw std::runtime_error( oss.str( ) );
        }
        services_.insert( std::make_pair( name, func ) );
    }

    void application::unregister_service_factory( const std::string &name )
    {
        vtrc::lock_guard<vtrc::mutex> lck(services_lock_);
        auto f = services_.find( name );
        if( f != services_.end( ) ) {
            services_.erase( f );
        }
    }

    application::parent_service_sptr
        application::get_service_by_name ( vcomm::connection_iface* c,
                                           const std::string &name )
    {
        vtrc::lock_guard<vtrc::mutex> lck( services_lock_ );

        auto f = services_.find( name );

        if( f != services_.end( ) ) {
            return f->second( this, c->shared_from_this( ) );
        } else {
            return application::service_wrapper_sptr( );
        }
    }

    void application::quit()
    {
        stop( );
        pp_.stop_all( );
    }

}}
