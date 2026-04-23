//
// Created by mehme on 12/12/2025.
//

#ifndef ATLASHTTP_HTTPSERVER_H
#define ATLASHTTP_HTTPSERVER_H

#pragma once

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <boost/asio/ssl.hpp>
#include "AsyncRequestProcessor.h"
#include "HttpLogs.h"
#include "Namespace.h"
#include "WebSocketSession.h"

AtlasHttpNamespaceBegin



class HTTPServer
{
public:
    explicit HTTPServer(boost::asio::io_context & context)
        : _ioContext(context)
    {
    }
    explicit HTTPServer(
        boost::asio::io_context& context, 
        std::function<void(HttpServerLogLevel, std::string)> onLog
        )
        : 
        _ioContext(context),
        _onLog(std::move(onLog))
    {
    }

    ~HTTPServer() {
        Stop();
    }

    void Start(const std::string& address, const std::string& port, bool useHttps = false, const std::string& certificateChainFile = {}, const std::string& privateKeyFile = {}, const std::string& dhParamsFile = {})
    {
        _useHttps = useHttps;
        if (_useHttps)
        {
            if (certificateChainFile.empty() || privateKeyFile.empty())
            {
                throw std::invalid_argument("HTTPS requires certificate chain and private key files.");
            }
            ConfigureSslContext(certificateChainFile, privateKeyFile, dhParamsFile);
        }

        auto const addr = boost::asio::ip::make_address(address);
        auto const endpoint = boost::asio::ip::tcp::endpoint{ addr, static_cast<uint16_t>(std::stoi(port)) };

        Log(HttpServerLogLevel::Info, (_useHttps ? "HTTPS" : "HTTP") + std::string(" Server initializing at ") + address + ":" + port);   
        
        // create and open the acceptor on the server io_context
        _acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(_ioContext);
        _acceptor->open(endpoint.protocol());
        _acceptor->set_option(boost::asio::socket_base::reuse_address(true));
        _acceptor->bind(endpoint);
        _acceptor->listen(boost::asio::socket_base::max_listen_connections);
        StartAccept();

    }
    void StartAccept()
    {
        if(_shouldStop)
        {
            return;
        }
		Log(HttpServerLogLevel::Verbose, "Http Server Accepting Connections...");
        _acceptor->async_accept(
            boost::asio::make_strand(_ioContext),
            [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
					Log(HttpServerLogLevel::Verbose, "Http Server Accepted Connection. remote_id = " + socket.remote_endpoint().address().to_string());
                    auto strand = boost::asio::make_strand(_ioContext);
                    if (_useHttps)
                    {
                        auto sslStream = std::make_unique<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(std::move(socket), *_sslContext);
                        auto connection = std::make_shared<ConnectionContext>(_onLog, std::move(sslStream), strand);
                        const auto sharedResponder = std::make_shared<AsyncRequestProcessor>(_onLog, std::move(connection), _requestHandlers, _websocketHandlers);
                        sharedResponder->StartAsyncRead();
                    }
                    else
                    {
                        auto socketPtr = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
                        auto connection = std::make_shared<ConnectionContext>(_onLog, std::move(socketPtr), strand);
                        const auto sharedResponder = std::make_shared<AsyncRequestProcessor>(_onLog, std::move(connection), _requestHandlers, _websocketHandlers);
                        sharedResponder->StartAsyncRead();
                    }
                }
                else
                {
					Log(HttpServerLogLevel::Error, "Accept error: " + ec.message());
                }
                StartAccept();
            });
    }

    void AddRequestHandler(const std::string& target, boost::beast::http::verb verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)> handler)
    {
        auto & methods = _requestHandlers[target];
        methods.emplace(verb, std::move(handler));
    }

    void AddWebSocketHandler(const std::string& target, const WebSocketSession::WebSocketHandlers& handlers)
    {
        _websocketHandlers.emplace(target, handlers);
    }

    
    void Stop()
    {
        _shouldStop = true;
        // Close the acceptor first to unblock any blocking accept() call
        try
        {
            if (_acceptor && _acceptor->is_open())
            {
                boost::system::error_code ec;
                (void)_acceptor->close(ec); // NOLINT(*-unused-return-value)
                if (ec)
                {
                    Log(HttpServerLogLevel::Error, "Error closing acceptor: " + ec.message());
                }
            }
        }
        catch (const std::exception& e)
        {
			Log(HttpServerLogLevel::Error, "Exception while closing acceptor: " + std::string(e.what()));
        }
    }


private:
    void ConfigureSslContext(const std::string& certificateChainFile, const std::string& privateKeyFile, const std::string& dhParamsFile)
    {
        _sslContext = std::make_unique<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_server);
        _sslContext->set_options(
            boost::asio::ssl::context::default_workarounds |
            boost::asio::ssl::context::no_sslv2 |
            boost::asio::ssl::context::no_sslv3 |
            boost::asio::ssl::context::single_dh_use);
        _sslContext->use_certificate_chain_file(certificateChainFile);
        _sslContext->use_private_key_file(privateKeyFile, boost::asio::ssl::context::pem);
        if (!dhParamsFile.empty())
        {
            _sslContext->use_tmp_dh_file(dhParamsFile);
        }
    }

    void Log(HttpServerLogLevel level, const std::string& message)
    {
        if (_onLog)
        {
            _onLog(level, message);
        }
    }

    bool _useHttps = false;
    bool _shouldStop = false;
    std::unique_ptr<boost::asio::ssl::context> _sslContext;
    std::unordered_map<std::string, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>> _requestHandlers;
    std::unordered_map<std::string, WebSocketSession::WebSocketHandlers> _websocketHandlers;
    boost::asio::io_context & _ioContext;
	std::function<void(HttpServerLogLevel, std::string)> _onLog;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
};

AtlasHttpNamespaceEnd
#endif //ATLASHTTP_HTTPSERVER_H