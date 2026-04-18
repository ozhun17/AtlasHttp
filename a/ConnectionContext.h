#ifndef ATLASHTTP_CONNECTIONCONTEXT_H
#define ATLASHTTP_CONNECTIONCONTEXT_H
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include "MetricManager.h"
#include "Namespace.h"
AtlasHttpNamespaceBegin


struct ConnectionContext
{
    ConnectionContext(
        std::unique_ptr<boost::asio::ip::tcp::socket> socket,
        boost::asio::any_io_executor strand
    )
        : _socket(std::move(socket)),
        _strand(std::move(strand))
    {
        ++MetricManager::The()._currentHttpConnections;
        _response->set(boost::beast::http::field::server, "AtlasHttpServer");
    }
    ~ConnectionContext()
    {
        --MetricManager::The()._currentHttpConnections;
        ++MetricManager::The()._finishedHttpConnections;
    }

    void Reset()
    {
         _request = {};
        _response = std::make_unique<boost::beast::http::response<boost::beast::http::string_body>>(boost::beast::http::status::internal_server_error, _version);
    }
    std::unique_ptr<boost::asio::ip::tcp::socket> _socket;
    boost::asio::any_io_executor _strand;
    boost::beast::http::request<boost::beast::http::string_body> _request;
    unsigned _version = 11;
    std::unique_ptr<boost::beast::http::response<boost::beast::http::string_body>> _response = std::make_unique<boost::beast::http::response<boost::beast::http::string_body>>(boost::beast::http::status::internal_server_error, _version);
};

AtlasHttpNamespaceEnd
#endif