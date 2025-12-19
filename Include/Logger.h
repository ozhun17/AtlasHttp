//
// Created by mehme on 12/12/2025.
//

#ifndef ATLASHTTP_LOGGER_H
#define ATLASHTTP_LOGGER_H

#pragma once
#include <string>
#include <cstddef>
#include <iostream>

#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>
#include <boost/log/keywords/severity.hpp>
#include <boost/log/attributes/current_thread_id.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/attributes/value_extraction.hpp>
#include "Namespace.h"

AtlasNamespaceBegin
enum class LogLevel { Verbose = 0, Debug, Info, Warning, Error, Fatal };

class LogManager {
public:
    struct Config {
        bool toConsole = true;
        bool toFile = false;
        std::string filePath = "app.log";
        std::size_t maxFileSize = 10 * 1024 * 1024;
        unsigned maxFiles = 5;
        LogLevel minLevel = LogLevel::Verbose;
        bool colorConsole = true;
    };

    static void Init(const Config& cfg) {
        Config& c = config();
        c = cfg;
        namespace logging = boost::log;
        namespace sinks = boost::log::sinks;
        namespace expr = boost::log::expressions;
        namespace attrs = boost::log::attributes;
        const auto core = logging::core::get();
        core->remove_all_sinks();
        logging::add_common_attributes();
        core->add_global_attribute("ThreadID", attrs::current_thread_id());
        if (c.toConsole) {
            typedef sinks::synchronous_sink<sinks::text_ostream_backend> console_sink_t;
            const auto consoleSink = boost::make_shared<console_sink_t>();
            consoleSink->locked_backend()->auto_flush(true);
            consoleSink->locked_backend()->add_stream(boost::shared_ptr<std::ostream>(&std::cout, boost::null_deleter()));
            bool useColor = c.colorConsole;
            consoleSink->set_formatter([useColor](boost::log::record_view const& rec, boost::log::formatting_ostream& strm) {
                auto sev = boost::log::extract<LogLevel>("Severity", rec);
                LogLevel lv = sev ? sev.get() : LogLevel::Info;
                auto ts = boost::log::extract<boost::posix_time::ptime>("TimeStamp", rec);
                if (ts) strm << "[" << boost::posix_time::to_simple_string(ts.get()) << "] ";
                auto th = boost::log::extract<boost::log::attributes::current_thread_id::value_type>("ThreadID", rec);
                if (th) strm << "[" << th.get() << "] ";
                if (useColor) {
                    strm << sevToColor(lv) << "<" << sevToStr(lv) << ">" << "\x1b[0m" << ": ";
                } else {
                    strm << "<" << sevToStr(lv) << ">: ";
                }
                strm << rec[expr::smessage];
            });
            core->add_sink(consoleSink);
        }
        if (c.toFile) {
            typedef sinks::synchronous_sink<sinks::text_file_backend> file_sink_t;
            namespace keywords = boost::log::keywords;
            boost::shared_ptr<sinks::text_file_backend> backend = boost::make_shared<sinks::text_file_backend>(
                keywords::file_name = c.filePath + ".%N.log",
                keywords::rotation_size = c.maxFileSize
            );
            backend->set_file_collector(sinks::file::make_collector(
                keywords::target = ".",
                keywords::max_files = c.maxFiles
            ));
            backend->scan_for_files();
            boost::shared_ptr<file_sink_t> fileSink = boost::make_shared<file_sink_t>(backend);
            fileSink->set_formatter([](boost::log::record_view const& rec, boost::log::formatting_ostream& strm) {
                auto ts = boost::log::extract<boost::posix_time::ptime>("TimeStamp", rec);
                if (ts) strm << "[" << boost::posix_time::to_simple_string(ts.get()) << "] ";
                auto th = boost::log::extract<boost::log::attributes::current_thread_id::value_type>("ThreadID", rec);
                if (th) strm << "[" << th.get() << "] ";
                auto sev = boost::log::extract<LogLevel>("Severity", rec);
                LogLevel lv = sev ? sev.get() : LogLevel::Info;
                strm << "<" << LogManager::sevToStr(lv) << ">: ";
                strm << rec[expr::smessage];
            });
            core->add_sink(fileSink);
        }
        core->set_filter(boost::log::expressions::attr<LogLevel>("Severity") >= c.minLevel);
    }

    static bool ShouldLog(LogLevel lvl) noexcept {
        Config& c = config();
        return (static_cast<int>(lvl) >= static_cast<int>(c.minLevel)) && (c.toConsole || c.toFile);
    }

    static boost::log::sources::severity_logger_mt<LogLevel>& GetLogger() {
        return logger();
    }

private:
    using LoggerT = boost::log::sources::severity_logger_mt<LogLevel>;

    static Config& config() {
        static Config cfg;
        return cfg;
    }

    static LoggerT& logger() {
        static LoggerT lg;
        return lg;
    }

    static const char* sevToStr(const LogLevel & lv) {
        switch (lv) {
        case LogLevel::Verbose: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        }
        return "UNKNOWN";
    }

    static const char* sevToColor(const LogLevel & lv) {
        switch (lv) {
        case LogLevel::Verbose: return "\x1b[37m";
        case LogLevel::Debug: return "\x1b[36m";
        case LogLevel::Info: return "\x1b[32m";
        case LogLevel::Warning: return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
        case LogLevel::Fatal: return "\x1b[41;37m";
        }
        return "\x1b[0m";
    }
};

inline std::ostream& operator<<(std::ostream& os, const LogLevel & lv) {
    switch (lv) {
    case LogLevel::Verbose: return os << "TRACE";
    case LogLevel::Debug: return os << "DEBUG";
    case LogLevel::Info: return os << "INFO";
    case LogLevel::Warning: return os << "WARN";
    case LogLevel::Error: return os << "ERROR";
    case LogLevel::Fatal: return os << "FATAL";
    }
    return os << "UNKNOWN";
}

#define Logger(level) \
    BOOST_LOG_SEV(LogManager::GetLogger(), LogLevel::level)

AtlasNamespaceEnd
#endif //ATLASHTTP_LOGGER_H