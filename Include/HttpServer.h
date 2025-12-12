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
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

struct AsyncReaderWriter : std::enable_shared_from_this<AsyncReaderWriter>
{
    AsyncReaderWriter(
        std::unique_ptr<boost::asio::ip::tcp::socket> socket,
        boost::asio::any_io_executor strand
    )
        : _socket(std::move(socket)),
        _strand(std::move(strand))
    {
    }

    void StartAsyncRead() {
        AsyncReadNext();
    }

    void Process()
    {
        _response = std::make_unique<boost::beast::http::response<boost::beast::http::string_body>>(
            boost::beast::http::status::internal_server_error, _version);
        _response->keep_alive(_request.keep_alive());

        _response->set(boost::beast::http::field::server, "BoostBeastServer");

        if (_request.method() == boost::beast::http::verb::get && _request.target() == "/ready")
        {
            nlohmann::json json;
            json["status"] = "ok";

            const auto time = std::time(nullptr);
            std::ostringstream ss;
            std::tm tm_buf;
#if defined(_WIN32) || defined(_WIN64)
            gmtime_s(&tm_buf, &time);
#else
            gmtime_r(&time, &tm_buf);
#endif
            ss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%SZ");
            json["time"] = ss.str();

            _response->result(boost::beast::http::status::ok);
            _response->set(boost::beast::http::field::content_type, "application/json");
            _response->body() = json.dump();
        }
        else
        {
            _response->result(boost::beast::http::status::not_found);
            _response->set(boost::beast::http::field::content_type, "text/plain");
            _response->body() = "Not Found";
        }

        _response->prepare_payload();
        AsyncWrite();
    }

    void AsyncReadNext()
    {
        _request = {};
        _buffer.consume(_buffer.size());

        boost::beast::http::async_read(
            *_socket,
            _buffer,
            _request,
            boost::asio::bind_executor(
                _strand,
                [self = shared_from_this()](const boost::beast::error_code& ec, std::size_t)
                {
                    if (!ec)
                    {
                        self->_version = self->_request.version();
                        self->Process();
                    }
                }));
    }


    void AsyncWrite()
    {
        boost::beast::http::async_write(
            *_socket,
            *_response,
            boost::asio::bind_executor(
                _strand,
                [self = shared_from_this()](const boost::system::error_code& ec, std::size_t)
                {
                    if (ec)
                    {
                        boost::system::error_code ec2;
                        self->_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec2); // NOLINT(*-unused-return-value)
                        //TODO: add logging that shows connection tear downs.
                        return;
                    }

                    if (!self->_response->keep_alive())
                    {
                        boost::system::error_code ec2;
                        self->_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec2); // NOLINT(*-unused-return-value)
                        //TODO: add logging that shows connection tear downs.
                        return;
                    }

                    self->AsyncReadNext();
                }
            ));
    }


    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;
    boost::asio::any_io_executor _strand;
    boost::beast::flat_buffer _buffer;
    boost::beast::http::request<boost::beast::http::string_body> _request;
    unsigned _version = 11;
    std::unique_ptr<boost::beast::http::response<boost::beast::http::string_body>> _response;
};

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

    void Start(const std::string& address, const std::string& port) {
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
        _acceptor->async_accept(
            boost::asio::make_strand(_ioContext),
            [this](const boost::system::error_code& ec, boost::asio::ip::tcp::socket socket)
            {
                if (!ec)
                {
                    auto socketPtr = std::make_unique<boost::asio::ip::tcp::socket>(std::move(socket));
                    auto strand = boost::asio::make_strand(_ioContext);
                    const auto sharedResponder = std::make_shared<AsyncReaderWriter>(std::move(socketPtr), strand);
                    sharedResponder->StartAsyncRead();
                }
                StartAccept();
            });
    }


    void Stop() {
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
    boost::asio::io_context & _ioContext;
    int _numThreads;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
};


#endif //ATLASHTTP_HTTPSERVER_H