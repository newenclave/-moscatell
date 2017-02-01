
#include "subsys-clients2.h"

#include "noname-common.h"
#include "noname-client.h"

#include "common/tuntap.h"
#include "common/utilities.h"
#include "common/net-ifaces.h"

#include "protocol/tuntap.pb.h"

#define LOG(lev) log_(lev, "clients2") 
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)

namespace msctl { namespace agent {

namespace {

    using client_create_info = clients2::client_create_info;
    using utilities::decorators::quote;
    using error_code = srpc::common::transport::error_code;
    using io_service = SRPC_ASIO::io_service;

    using size_policy           = noname::tcp_size_policy;
    using server_create_info    = listener2::server_create_info;
    using error_code            = noname::error_code;

    template <typename T>
    std::uintptr_t uint_cast( const T *val )
    {
        return reinterpret_cast<std::uintptr_t>(val);
    }

    struct device;

    struct client_delegate: public noname::protocol_type<size_policy> {

        device *my_device_ = nullptr;
    };

    using proto_sptr = std::shared_ptr<client_delegate>;

    struct device: public common::tuntap_transport {

        void on_read( char *data, size_t length )
        {

        }

        std::shared_ptr<device> create( application *app,
                                        const client_create_info &inf )
        {

        }

        void on_read_error( const error_code &/*code*/ )
        { }

        void on_write_error( const error_code &/*code*/ )
        { }

        void on_write_exception(  )
        {
            throw;
        }

        proto_sptr                  proto_;
        noname::client::client_sptr client_;
    };

    using device_sptr = std::shared_ptr<device>;
    using device_map  = std::map<std::string, device_sptr>;

}

    struct clients2::impl {

        application     *app_;
        clients2        *parent_;
        logger_impl     &log_;

        device_map       devs_;
        std::mutex       devs_lock_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
        { }

        void start_all( )
        {

        }
    };

    clients2::clients2( application *app )
        :impl_(new impl(app))
    {
        impl_->parent_ = this;
    }


    bool clients2::add_client( const client_create_info &inf, bool start )
    {
        try {


        } catch( const std::exception &ex ) {
            impl_->LOGERR << "Create client failed " << ex.what( );
        }
    }

    void clients2::init( )
    { }

    void clients2::start( )
    { 
        impl_->start_all( );
        impl_->LOGINF << "Started.";
    }

    void clients2::stop( )
    { 
        impl_->LOGINF << "Stopped.";
    }
    
    std::shared_ptr<clients2> clients2::create( application *app )
    {
        return std::make_shared<clients2>( app );
    }
}}

		
