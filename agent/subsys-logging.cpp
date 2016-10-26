#include <memory>
#include <chrono>
#include <fstream>
#include <map>
#include <functional>

#ifndef _WIN32
#include <syslog.h>
#endif

#include "subsys-logging.h"
#include "application.h"

#include "logger-impl.h"
#include "common/utilities.h"

#include "vtrc-common/vtrc-delayed-call.h"

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/algorithm/string.hpp"

#include "vtrc-common/vtrc-stub-wrapper.h"
#include "vtrc-common/vtrc-closure-holder.h"
#include "vtrc-server/vtrc-channels.h"

#include "vtrc-common/vtrc-delayed-call.h"

#include "protocol/logger.pb.h"
#include "protocol/moscatell.pb.h"

#define LOG(lev) log_(lev, "log")
#define LOGINF   LOG(loglevel::info)
#define LOGDBG   LOG(loglevel::debug)
#define LOGERR   LOG(loglevel::error)
#define LOGWRN   LOG(loglevel::warning)

namespace msctl { namespace agent {

    namespace {

        const std::string stdout_name  = "stdout";
        const std::string stdout_name2 = "-";
        const std::string stderr_name  = "stderr";
        const std::string syslog_name  = "syslog";

        using loglevel     = agent::logger_impl::level;
        using stringlist   = std::vector<std::string>;
        using delayed_call = vtrc::common::delayed_call;
        using void_call    = std::function<void(void)>;

        const std::string subsys_name( "logging" );
        namespace bsig   = boost::signals2;
        namespace bpt    = boost::posix_time;
        namespace vcomm  = vtrc::common;
        namespace vserv  = vtrc::server;

        inline loglevel str2lvl( const char *str )
        {
            return logger_impl::str2level( str );
        }

        struct level_color {
            std::ostream &o_;
            level_color( std::ostream &o, loglevel lvl )
                :o_(o)
            {
                switch( lvl ) {
                case loglevel::zero:
                    o << utilities::console::cyan;
                    break;
                case loglevel::error:
                    o << utilities::console::red;
                    break;
                case loglevel::warning:
                    o << utilities::console::yellow;
                    break;
                case loglevel::info:
                    o << utilities::console::green;
                    break;
                case loglevel::debug:
                    o << utilities::console::light;
                    break;
                default:
                    o << utilities::console::none;
                }
            }

            ~level_color( )
            {
                o_ << utilities::console::none;
                o_.flush( );
            }
        };

        using ostream_uptr = std::unique_ptr<std::ostream>;

        ostream_uptr open_file( const std::string &path, size_t *size )
        {
            std::unique_ptr<std::ofstream>
                   res( new std::ofstream( path.c_str( ), std::ios::app ) );

            if( !res->is_open( ) ) {
                return ostream_uptr( );
            }

            if( size )  {
                *size = res->tellp( );
            }
            return std::move(res);
        }

        namespace sproto = msctl::rpc;
        typedef   sproto::events::Stub events_stub_type;
        typedef   vcomm::stub_wrapper<
            events_stub_type, vcomm::rpc_channel
        > event_client_type;

        using vserv::channels::unicast::create_event_channel;

        std::string str2logger( const std::string &str,
                                std::string &fromlvl, std::string &tolvl )
        {
            size_t delim_pos = str.find_last_of( '[' );
            std::string path;

            if( delim_pos == std::string::npos ) {
                path = str;
            } else {

                path = std::string( str.begin( ), str.begin( ) + delim_pos );
                auto *to = &fromlvl;
                bool found_int = true;

                for( auto d = ++delim_pos; d < str.size( ); ++d ) {
                    switch( str[d] ) {
                    case ']':
                        found_int = false;
                    case '-':
                        to->assign( str.begin( ) + delim_pos,
                                    str.begin( ) + d );
                        to = &tolvl;
                        delim_pos = d + 1;
                        break;
                    }
                }

                if( found_int ) {
                    to->assign( str.begin( ) + delim_pos, str.end( ) );
                }
            }
            return path;
        }

        void chan_err( const char * /*mess*/ )
        {
            //std::cerr << "logger channel error: " << mess << "\n";
        }

        loglevel proto2lvl( unsigned proto_level )
        {
            using lvl = logger_impl::level;
            switch(proto_level) {
            case static_cast<unsigned>(lvl::zero    ):
            case static_cast<unsigned>(lvl::error   ):
            case static_cast<unsigned>(lvl::warning ):
            case static_cast<unsigned>(lvl::info    ):
            case static_cast<unsigned>(lvl::debug   ):
                return static_cast<lvl>(proto_level);
            }
            return lvl::info;
        }

        msctl::rpc::logger::log_level lvl2proto( unsigned lvl )
        {
            switch( lvl ) {
            case msctl::rpc::logger::zero    :
            case msctl::rpc::logger::error   :
            case msctl::rpc::logger::warning :
            case msctl::rpc::logger::info    :
            case msctl::rpc::logger::debug   :
                return static_cast<msctl::rpc::logger::log_level>(lvl);
            }
            return msctl::rpc::logger::info;
        }
#ifndef _WIN32
        int level2syslog( int /*logger::level*/ val )
        {
            using lvl = logger_impl::level;
            switch( val ) {
            case static_cast<int>( lvl::zero    ): return LOG_EMERG;
            case static_cast<int>( lvl::error   ): return LOG_ERR;
            case static_cast<int>( lvl::warning ): return LOG_WARNING;
            case static_cast<int>( lvl::info    ): return LOG_INFO;
            case static_cast<int>( lvl::debug   ): return LOG_DEBUG;
            }
            return LOG_INFO;
        }
#endif

        class proto_looger_impl: public msctl::rpc::logger::instance {

            typedef proto_looger_impl this_type;

            logger_impl           &lgr_;
            bsig::connection       connect_;
            event_client_type      eventor_;
            std::atomic<size_t>    id_;

        public:

            size_t next_op_id( )
            {
                return id_++;
            }

            static const std::string &name( )
            {
                namespace mlog = msctl::rpc::logger;
                return mlog::instance::descriptor( )->full_name( );
            }

            ~proto_looger_impl( )
            {
                connect_.disconnect( );
            }

            proto_looger_impl( application *app,
                               vcomm::connection_iface_wptr cli )
                :lgr_(app->log( ))
                ,eventor_(create_event_channel(cli.lock( ), true), true)
                ,id_(100)
            {
                eventor_.channel( )->set_channel_error_callback( chan_err );
            }

            void send_log( ::google::protobuf::RpcController*  /*controller*/,
                           const ::msctl::rpc::logger::log_req* request,
                           ::msctl::rpc::empty*                 /*response*/,
                           ::google::protobuf::Closure* done ) override
            {
                vcomm::closure_holder holder( done );
                loglevel lvl = request->has_level( )
                                  ? proto2lvl( request->level( ) )
                                  : loglevel::info;
                lgr_(lvl) << request->text( );
            }

            void set_level(::google::protobuf::RpcController*   /*controller*/,
                         const ::msctl::rpc::logger::set_level_req* request,
                         ::msctl::rpc::empty*                    /*response*/,
                         ::google::protobuf::Closure* done) override
            {
                vcomm::closure_holder holder( done );
                loglevel lvl = request->has_level( )
                                  ? proto2lvl( request->level( ) )
                                  : loglevel::info;
                lgr_.set_level( lvl );
            }

            void get_level(::google::protobuf::RpcController* /*controller*/,
                         const ::msctl::rpc::empty*            /*request*/,
                         ::msctl::rpc::logger::get_level_res*  response,
                         ::google::protobuf::Closure* done) override
            {
                vcomm::closure_holder holder( done );
                response->set_level( lvl2proto(
                         static_cast<unsigned>(lgr_.get_level( )) ) );
            }

            static vtrc::uint64_t time2ticks( const boost::posix_time::ptime &t)
            {
                using namespace boost::posix_time;
                static const ptime epoch( ptime::date_type( 1970, 1, 1 ) );
                time_duration from_epoch = t - epoch;
                return from_epoch.ticks( );
            }

            void on_write2( const log_record_info info,
                           logger_data_type const &data,
                           size_t opid )
            {
                msctl::rpc::logger::write_data req;
                req.set_level( lvl2proto( static_cast<unsigned>(info.level) ) );
                std::ostringstream oss;
                for( auto &d: data ) {
                    oss << d << "\n";
                }
                req.set_text( oss.str( ) );
                req.set_microsec( time2ticks( info.when ) );

                msctl::rpc::async_op_data areq;
                areq.set_id( opid );
                areq.set_data( req.SerializeAsString( ) );
                areq.set_tick_count( application::tick_count( ) );

                eventor_.call_request( &events_stub_type::async_op, &areq );

            }

#if 0
            void on_write( logger::level lvl, uint64_t microsec,
                           const std::string &data,
                           const std::string &/*format*/,
                           size_t opid )
            {
                fr::proto::logger::write_data req;
                req.set_level( level2proto( static_cast<unsigned>(lvl) ) );
                req.set_text( data );
                req.set_microsec( microsec );

                fr::proto::async_op_data areq;
                areq.set_id( opid );
                areq.set_data( req.SerializeAsString( ) );
                eventor_.call_request( &events_stub_type::async_op, &areq );

            }
#endif

            void subscribe(::google::protobuf::RpcController* /*controller*/,
                         const ::msctl::rpc::empty*            /*request*/,
                         ::msctl::rpc::logger::subscribe_res* response,
                         ::google::protobuf::Closure* done) override
            {
                vcomm::closure_holder holder( done );
                size_t op_id = next_op_id( );

                namespace ph = std::placeholders;

                connect_ = lgr_.on_write_connect(
                                std::bind( &this_type::on_write2, this,
                                           ph::_1, ph::_2, op_id ) );

                response->set_async_op_id( op_id );
            }

            void unsubscribe(::google::protobuf::RpcController* /*controller*/,
                         const ::msctl::rpc::empty*      /*request*/,
                         ::msctl::rpc::empty*            /*response*/,
                         ::google::protobuf::Closure* done) override
            {
                vcomm::closure_holder holder( done );
                connect_.disconnect( );
            }

        };

        application::service_wrapper_sptr create_service( application *app,
                                              vcomm::connection_iface_wptr cl )
        {
            auto inst(vtrc::make_shared<proto_looger_impl>( app, cl ));
            return app->wrap_service( cl, inst );
        }

        std::ostream &output( std::ostream &o,
                              const log_record_info &loginf,
                              const std::string &s )
        {
            loglevel lvl = static_cast<loglevel>(loginf.level);
            o << loginf.when
              << " " << loginf.tprefix
              << " (" << logger_impl::level2str(lvl) << ") "
              << "[" << loginf.name << "] "
              << s
                   ;
            return o;
        }

        std::ostream &output2( std::ostream &o,
                               const log_record_info &loginf,
                               const std::string &s )
        {
            loglevel lvl = static_cast<loglevel>(loginf.level);
            o << loginf.when.time_of_day( )
              << " " << loginf.tprefix
              << " <" << logger_impl::level2str(lvl) << "> "
              << "[" << loginf.name << "] "
              << s
                   ;
            return o;
        }

        struct log_output { /// shared_from_this?

            int min_;
            int max_;

            log_output( int min, int max )
                :min_(min)
                ,max_(max)
            { }
            virtual ~log_output( ) { }

            /// level getter setter
            int  get_min( ) const   { return min_; }
            int  get_max( ) const   { return max_; }
            void set_min( int val ) { min_ = val;  }
            void set_max( int val ) { max_ = val;  }

            virtual const char *name( ) const = 0;
            virtual void flush( ) { ;;; }

            virtual void write( const log_record_info &loginf,
                                stringlist const &data ) = 0;
            virtual size_t length( ) const = 0;
        };

        struct console_output: log_output {
            bsig::scoped_connection conn_;
            std::ostream &stream_;
            console_output( int min, int max, std::ostream &stream )
                :log_output(min, max)
                ,stream_(stream)
            { }

            void write( const log_record_info &loginf, stringlist const &data )
            {
                int lvl = loginf.level;
                level_color _( stream_, static_cast<loglevel>(lvl) );
                for( auto &s: data ) {
                    output( stream_, loginf, s ) << std::endl;
                }
            }

            void flush( )
            {
                stream_.flush( );
            }

            size_t length( ) const
            {
                return 0;
            }
        };

        struct cerr_output: console_output {
            cerr_output( int min, int max )
                :console_output(min, max, std::cerr)
            { }

            const char *name( ) const
            {
                return "stderr";
            }

        };

        struct cout_output: console_output {
            cout_output( int min, int max )
                :console_output(min, max, std::cout)
            { }

            const char *name( ) const
            {
                return "stdout";
            }

        };
#ifndef _WIN32
        struct syslog_output: log_output {

            syslog_output(int min, int max)
                :log_output(min, max)
            { }

            void write( const log_record_info &loginf, stringlist const &data )
            {
                for( auto &s: data ) {
                    std::ostringstream oss;
                    output2( oss, loginf, s );
                    ::syslog( level2syslog( loginf.level ),
                              "%s", oss.str( ).c_str( ) );
                }
            }

            const char *name( ) const
            {
                return "syslog";
            }

            size_t length( ) const
            {
                return 0;
            }
        };
#endif
        struct file_output: log_output {
            std::atomic<size_t> length_;
            ostream_uptr        stream_;
            const std::string   path_;
            file_output( int min, int max, const std::string &path )
                :log_output(min, max)
                ,length_(0)
                ,path_(path)
            {
                size_t len = 0;
                stream_ = open_file( path, &len );
                length_ = len;
            }

            void flush( )
            {
                stream_->flush( );
            }

            void write( const log_record_info &loginf, stringlist const &data )
            {
                for( auto &s: data ) {
                    output( *stream_, loginf, s ) << "\n";
                }
                length_ = stream_->tellp( );
            }

            const char *name( ) const
            {
                return path_.c_str( );
            }

            size_t length( ) const
            {
                return length_;
            }
        };

        struct output_connection {
            bsig::scoped_connection connection_;
            std::unique_ptr<log_output> output_;
        };

        using connection_map = std::map<std::string, output_connection>;
#ifndef _WIN32
        bool is_syslog( const std::string &path )
        {
            return path == syslog_name;
        }
#endif
        bool is_stdout( const std::string &path )
        {
            return path == stdout_name || path == stdout_name2;
        }

        bool is_stderr( const std::string &path )
        {
            return path == stderr_name;
        }

        std::unique_ptr<log_output> create_by_name( const std::string &path,
                                                    int minlvl, int maxlvl,
                                                    logger_impl    &log_ )
        {
            std::unique_ptr<log_output> res;

            if( is_stdout( path ) ) {                           /// cout
                res.reset(new cout_output( minlvl, maxlvl ) );
            } else if( is_stderr( path ) ) {                    /// cerr
                res.reset(new cerr_output( minlvl, maxlvl ) );
#ifndef _WIN32
            } else if( is_syslog( path ) ) {                    /// syslog
                res.reset(new syslog_output( minlvl, maxlvl ) );
#endif
            } else {
                try {
                    res.reset(new file_output( minlvl, maxlvl, path ));
                } catch( std::exception &ex ) {
                    //std::cerr
                    LOGERR
                        << "failed to add log file " << path << "; "
                        << ex.what( );
                    std::cerr
                        << "failed to add log file " << path << "; "
                        << ex.what( ) << std::endl;
                }
            }
            return std::move(res);
        }

    }

    struct logging::impl {

        application          *app_;
        agent::logger_impl   &log_;
#ifndef _WIN32
        bool                  syslog_;
#endif
        connection_map        connections_;
        delayed_call          flusher_;
        void_call             flush_worker_;
        std::int32_t          timeout_;

        impl( application *app )
            :app_(app)
            ,log_(app_->log( ))
#ifndef _WIN32
            ,syslog_(false)
#endif
            ,flusher_(log_.get_io_service( ))
        {
            flush_worker_ = [this]( ) {
                flush_all( );
                start_flusher( );
            };
            start_flusher( );
        }

        void init_flush( )
        {

        }

        void stop( )
        {
            flusher_.cancel( );
        }

        void flush_all( )
        {
            for( auto &l: connections_ ) {
//                std::cerr << l.second.output_->name( )
//                          << ": " << l.second.output_->length( )
//                          << "\n";
                l.second.output_->flush( );
            }
        }

        void start_flusher( )
        {
            auto runner = [this]( const VTRC_SYSTEM::error_code &e ) {
                if( !e ) {
                    log_.dispatch( flush_worker_ );
                }
            };

            flusher_.call_from_now( runner, delayed_call::seconds(timeout_) );
        }

        void reg_creator( const std::string &name,
                          application::service_getter_type func )
        {
            app_->register_service_factory( name, func );
        }

        void unreg_creator( const std::string &name )
        {
            app_->unregister_service_factory( name );
        }

        void log_output_slot( log_output *out, const log_record_info &inf,
                              stringlist const &data )
        {
            const auto lvl = inf.level;
            if( (lvl >= out->get_min( )) && (lvl <= out->get_max( )) ) {
                out->write( inf, data );
            }
        }

        /// dispatcher!
        void add_logger( const std::string &path, loglevel minl, loglevel maxl )
        {

            namespace ph = std::placeholders;

#ifndef _WIN32
            bool slog = is_syslog( path );
            if( slog && syslog_ ) {
                LOGDBG << "Syslog is already here!";
                return;
            }
#endif
            output_connection &conn(   is_stdout(path)
                                     ? connections_[stdout_name]
                                     : connections_[path] );

            conn.output_ = create_by_name( path,
                                           static_cast<int>(minl),
                                           static_cast<int>(maxl), log_ );
#ifndef _WIN32
            if( slog ) {
                openlog( "moscatell_agent", 0, LOG_USER );
                syslog_ = true;
            }
#endif
            conn.connection_ = log_.on_write_connect(
                        std::bind( &impl::log_output_slot, this,
                                   conn.output_.get( ),
                                   ph::_1, ph::_2 ) );

            LOGDBG << "New logger slot '" << path << "';"
                   << "\n  minimum level = "
                            << agent::logger_impl::level2str( minl )
                   << "\n  maximum level = "
                            << agent::logger_impl::level2str( maxl )
                   ;

            return;
        }

        /// dispatcher!
        void add_logger( const std::string &path,
                         const std::string &from, const std::string &to )
        {
            loglevel minl = loglevel::zero;
            loglevel maxl = log_.get_level( );

            if( !to.empty( ) ) {
                maxl = logger_impl::str2level( to.c_str( ) );
                minl = logger_impl::str2level( from.c_str( ), loglevel::zero );
            } else if( !from.empty( ) ) {
                maxl = logger_impl::str2level( from.c_str( ) );
            }
            add_logger( path, minl, maxl );
        }

        /// dispatcher!
        void add_logger_output( const std::string &params )
        {
            std::string from;
            std::string to;
            auto path = str2logger( params, from, to );
            add_logger( path, from, to );
        }

        /// dispatcher!
        void del_logger_output( const std::string &name )
        {
#ifndef _WIN32
            bool slog = is_syslog( name );
#endif
            auto count = connections_.erase( name );

            if( count ) {
                LOGINF << "Slot '" << name << "' erased";
            } else {
                LOGWRN << "Slot '" << name << "' was not found";
            }
#ifndef _WIN32
            if( slog && syslog_ ) {
                syslog_ = false;
                closelog( );
            }
#endif
            return;
        }
    };

    logging::logging( application *app )
        :impl_(new impl(app))
    { }

    logging::~logging( )
    {
        delete impl_;
    }

    /// static
    logging::shared_type logging::create( application *app,
                                          const std::vector<std::string> &def,
                                          std::int32_t flush_timeout )
    {
        shared_type new_inst(new logging(app));

        new_inst->impl_->timeout_ = flush_timeout >= 3 ? flush_timeout : 3;

        for( auto &d: def ) {
            new_inst->impl_->add_logger_output( d );
        }

        return new_inst;
    }

    logging::shared_type logging::create(application *app)
    {
        auto res = create(app, std::vector<std::string>(), 3);
        //res->add_logger_output( "-" );
        return res;
    }

    void logging::add_logger_output( const std::string &params )
    {
        impl_->log_.dispatch( [this, params]( ) {
            impl_->add_logger_output( params );
        } );
    }

    void logging::del_logger_output( const std::string &name )
    {
        impl_->log_.dispatch( [this, name]( ) {
            impl_->del_logger_output( name );
        } );
    }

    void logging::init( )
    {

    }

    void logging::start( )
    {
        impl_->reg_creator( proto_looger_impl::name( ),  create_service );
        impl_->LOGINF << "Started.";
    }

    void logging::stop( )
    {
        impl_->stop( );
        impl_->unreg_creator( proto_looger_impl::name( ) );
        impl_->LOGINF << "Stopped.";
    }

}}

    
