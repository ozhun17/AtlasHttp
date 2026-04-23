#ifndef ATLASHTTP_CONNECTIONCONTEXT_H
#define ATLASHTTP_CONNECTIONCONTEXT_H
#include <memory>
#include <type_traits>
#include <variant>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include "HttpLogs.h"
#include "MetricManager.h"
#include "Namespace.h"
AtlasHttpNamespaceBegin

struct ConnectionContext
{
    using PlainSocket = boost::asio::ip::tcp::socket;
    using SslSocket = boost::asio::ssl::stream<PlainSocket>;
    using StreamVariant = std::variant<std::shared_ptr<PlainSocket>, std::shared_ptr<SslSocket>>;

    ConnectionContext(
        std::function<void(HttpServerLogLevel, const std::string &)> onLog,
        std::shared_ptr<PlainSocket> socket,
        boost::asio::any_io_executor strand
    )
        : 
		_onLog(std::move(onLog)),
        _stream(std::move(socket)),
        _strand(std::move(strand))
    {
        ++MetricManager::The()._currentHttpConnections;
        Log(HttpServerLogLevel::Verbose, "Constructing ConnectionContext PlainSocket");
        _response->set(boost::beast::http::field::server, "AtlasHttpServer");
    }

    ConnectionContext(
        std::function<void(HttpServerLogLevel, const std::string&)> onLog,
        std::shared_ptr<SslSocket> sslStream,
        boost::asio::any_io_executor strand
    )
        :
		_onLog(std::move(onLog)),
        _stream(std::move(sslStream)),
        _strand(std::move(strand))
    {
        ++MetricManager::The()._currentHttpConnections;
        Log(HttpServerLogLevel::Verbose, "Constructing ConnectionContext SslSocket");
        _response->set(boost::beast::http::field::server, "AtlasHttpServer");
    }

    ~ConnectionContext()
    {
        Log(HttpServerLogLevel::Verbose, "Destructing ConnectionContext");
        --MetricManager::The()._currentHttpConnections;
        ++MetricManager::The()._finishedHttpConnections;
    }

    // Delete copy operations due to unique_ptr member
    ConnectionContext(const ConnectionContext&) = delete;
    ConnectionContext& operator=(const ConnectionContext&) = delete;

    // Allow move operations
    ConnectionContext(ConnectionContext&&) = default;
    ConnectionContext& operator=(ConnectionContext&&) = default;

    void Reset()
    {
        _request = {};
        _response = std::make_unique<boost::beast::http::response<boost::beast::http::string_body>>(boost::beast::http::status::internal_server_error, _version);
    }

    bool IsSecure() const
    {
        return std::holds_alternative<std::shared_ptr<SslSocket>>(_stream);
    }

    boost::asio::ip::tcp::endpoint RemoteEndpoint() const
    {
        return std::visit([](auto const& streamPtr) -> boost::asio::ip::tcp::endpoint
        {
            return streamPtr->lowest_layer().remote_endpoint();
        }, _stream);
    }

    template<class Request, class CompletionToken>
    void AsyncRead(boost::beast::flat_buffer& buffer, Request& request, CompletionToken&& token)
    {
        if (auto plainPtr = std::get_if<std::shared_ptr<PlainSocket>>(&_stream))
        {
            boost::beast::http::async_read(
                **plainPtr, buffer, request, 
                std::forward<CompletionToken>(token));
        }
        else if (auto sslPtr = std::get_if<std::shared_ptr<SslSocket>>(&_stream))
        {
            boost::beast::http::async_read(
                **sslPtr, buffer, request,
                std::forward<CompletionToken>(token));
        }
    }

    template<class Response, class CompletionToken>
    void AsyncWrite(Response& response, CompletionToken&& token)
    {
        if (auto plainPtr = std::get_if<std::shared_ptr<PlainSocket>>(&_stream))
        {
            boost::beast::http::async_write(
                **plainPtr, response, 
                std::forward<CompletionToken>(token));
        }
        else if (auto sslPtr = std::get_if<std::shared_ptr<SslSocket>>(&_stream))
        {
            boost::beast::http::async_write(
                **sslPtr, response,
                std::forward<CompletionToken>(token));
        }
    }

    template<class HandshakeHandler>
    void AsyncHandshake(boost::asio::ssl::stream_base::handshake_type type, HandshakeHandler&& handler)
    {
        auto handshakeHandler = std::forward<HandshakeHandler>(handler);
        std::visit([&](auto& streamPtr) mutable
        {
            using StreamType = std::remove_reference_t<decltype(*streamPtr)>;
            if constexpr (std::is_same_v<StreamType, SslSocket>)
            {
                streamPtr->async_handshake(type, std::move(handshakeHandler));
            }
            else
            {
                boost::asio::post(_strand, [handshakeHandler = std::move(handshakeHandler)]()
                {
                    handshakeHandler(boost::system::error_code{});
                });
            }
        }, _stream);
    }

    void ShutdownSend(boost::system::error_code& ec)
    {
        std::visit([&](auto& streamPtr)
        {
            using StreamType = std::remove_reference_t<decltype(*streamPtr)>;
            if constexpr (std::is_same_v<StreamType, PlainSocket>)
            {
                streamPtr->shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec);
            }
            else
            {
                streamPtr->shutdown(ec);
            }
        }, _stream);
    }

    void ShutdownBoth(boost::system::error_code& ec)
    {
        std::visit([&](auto& streamPtr)
        {
            using StreamType = std::remove_reference_t<decltype(*streamPtr)>;
            if constexpr (std::is_same_v<StreamType, PlainSocket>)
            {
                streamPtr->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
            }
            else
            {
                streamPtr->shutdown(ec);
            }
        }, _stream);
    }
	void Log(HttpServerLogLevel level, const std::string& message)
    {
        if (_onLog)
        {
            _onLog(level, message);
        }
    }
	std::function<void(HttpServerLogLevel, const std::string&)> _onLog;
    StreamVariant _stream;
    boost::asio::any_io_executor _strand;
    boost::beast::http::request<boost::beast::http::string_body> _request;
    unsigned _version = 11;
    std::unique_ptr<boost::beast::http::response<boost::beast::http::string_body>> _response = std::make_unique<boost::beast::http::response<boost::beast::http::string_body>>(boost::beast::http::status::internal_server_error, _version);
};

AtlasHttpNamespaceEnd
#endif