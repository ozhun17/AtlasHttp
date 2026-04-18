//
// Created by mehme on 12/12/2025.
//

#ifndef ATLASHTTP_HTTPSERVER_H
#define ATLASHTTP_HTTPSERVER_H

#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include "AsyncRequestProcessor.h"
#include "Namespace.h"

AtlasHttpNamespaceBegin

class HTTPServer
{
public:
    explicit HTTPServer(boost::asio::io_context & context)
        : _ioContext(context)
    {
    }
    ~HTTPServer() {
        Stop();
    }

    void Start(const std::string& address, const std::string& port)
    {
        auto const addr = boost::asio::ip::make_address(address);
        auto const endpoint = boost::asio::ip::tcp::endpoint{ addr, static_cast<uint16_t>(std::stoi(port)) };

        std::cout << "HTTP Server initializing at " << address << ":" << port << std::endl;

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
        Logger(Verbose) << "Http Server Accepting Connections...";
        _acceptor->async_accept(
            boost::asio::make_strand(_ioContext),
            [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
                    Logger(Verbose) << "Http Server Accepted Connection. remote_id = " << socket.remote_endpoint().address();;
                    auto socketPtr = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
                    auto strand = boost::asio::make_strand(_ioContext);
                    const auto sharedResponder = std::make_shared<AsyncRequestProcessor>(std::move(socketPtr), strand, _requestHandlers);
                    sharedResponder->StartAsyncRead();
                }
                StartAccept();
            });
    }

    void AddRequestHandler(const std::string& target, boost::beast::http::verb verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)> handler)
    {
        _requestHandlers.emplace(target, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>
        (
            std::initializer_list<std::pair<const boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>>{
                { verb, std::move(handler) }
            }
        ));
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
                    std::cerr << "Error closing acceptor: " << ec.message() << std::endl;
                }
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Exception while closing acceptor: " << e.what() << std::endl;
        }
    }

private:
    bool _shouldStop = false;
    std::unordered_map<std::string, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>> _requestHandlers;
    boost::asio::io_context & _ioContext;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
};

AtlasHttpNamespaceEnd
#endif //ATLASHTTP_HTTPSERVER_H