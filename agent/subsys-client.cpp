#include "subsys-client.h"


#define LOG(lev) log_(lev, "client")
#define LOGINF   LOG(logger_impl::level::info)
#define LOGDBG   LOG(logger_impl::level::debug)
#define LOGERR   LOG(logger_impl::level::error)
#define LOGWRN   LOG(logger_impl::level::warning)
namespace msctl { namespace agent {

    struct client::impl {
        application *app_;
        client      *parent_;
    };

    client::client( application *app )
        :impl_(new impl)
    {
        impl_->app_ = app;
        impl_->parent_ = this;
    }

    void client::init( )
    { }

    void client::start( )
    { }

    void client::stop( )
    { }

    std::shared_ptr<client> client::create( application *app )
    {
        return std::make_shared<client>( app );
    }

}}
