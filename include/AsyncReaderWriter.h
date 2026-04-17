//
// Created by mehme on 12/14/2025.
//

#ifndef ATLASHTTP_ASYNCREADERWRITER_H
#define ATLASHTTP_ASYNCREADERWRITER_H
#include "Namespace.h"
#include "MetricManager.h"
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

struct AsyncReader
{
    virtual ~AsyncReader() = default;
    virtual void AsyncReadNextRequest() = 0;
};

struct AsyncMethodResponder: std::enable_shared_from_this<AsyncMethodResponder>
{
    AsyncMethodResponder
    (
        std::weak_ptr<AsyncReader> weakReader,
        ConnectionContext & connectionContext,
        std::function<void(std::shared_ptr<AsyncMethodResponder>)> requestHandler
        )
        :
        _weakReader(std::move(weakReader)),
        _connectionContext(connectionContext),
        _requestHandler(std::move(requestHandler))
    {
    }

    void RespondAsync()
    {
        try
        {
            _requestHandler(shared_from_this());
        }
        catch (...)
        {

        }
    }

    ~AsyncMethodResponder()
    {
        auto sharedReader = _weakReader.lock();
        if (!sharedReader)
        {
            return;
        }
        _connectionContext._response->prepare_payload();
        boost::beast::http::async_write(
            *_connectionContext._socket,
            *_connectionContext._response,
            boost::asio::bind_executor(
                _connectionContext._strand,
                [&connectionContext = _connectionContext, sharedReader](const boost::system::error_code& ec, std::size_t)
                {
                    ++MetricManager::The()._httpResponses;
                    if (ec)
                    {
                        boost::system::error_code ec2;
                        connectionContext._socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec2); // NOLINT(*-unused-return-value)
                        if (ec2)
                        {
                            Logger(Error) << "Connection Teardown!" << ec.message();
                        }
                        sharedReader->AsyncReadNextRequest();
                        return;
                    }
                    Logger(Verbose) << "Http Server wrote async for connection: " << connectionContext._socket->remote_endpoint().address();

                    if (!(connectionContext._response->keep_alive()))
                    {
                        boost::system::error_code ec2;
                        connectionContext._socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec2); // NOLINT(*-unused-return-value)
                        if (ec2)
                        {
                            Logger(Error) << "Connection Teardown!" << ec.message();
                        }
                        sharedReader->AsyncReadNextRequest();
                        return;
                    }
                    sharedReader->AsyncReadNextRequest();
                }
            ));
    }
    std::function<void(std::shared_ptr<AsyncMethodResponder>)> _requestHandler;
    std::weak_ptr<AsyncReader> _weakReader;
    ConnectionContext & _connectionContext;
};


struct AsyncReaderWriter : std::enable_shared_from_this<AsyncReaderWriter>, AsyncReader
{
    AsyncReaderWriter(
        std::unique_ptr<boost::asio::ip::tcp::socket> socket,
        boost::asio::any_io_executor strand
    )
        : _connectionContext(std::move(socket), std::move(strand))
    {
    }

    void StartAsyncRead() {
        AsyncReadNextRequest();
    }
    ~AsyncReaderWriter() override
    {
        Logger(Verbose) << "Closing Async ReaderWriter";
    }

    void Process()
    {
        _connectionContext._response->keep_alive(_connectionContext._request.keep_alive());
        if (_connectionContext._request.method() == boost::beast::http::verb::get && _connectionContext._request.target() == "/ready")
        {
            const auto asyncResponder = std::make_shared<AsyncMethodResponder>(weak_from_this(), _connectionContext, []
                (const std::shared_ptr<AsyncMethodResponder> & responder)
            {
                nlohmann::json json;
                json["status"] = "ok";
                responder->_connectionContext._response->result(boost::beast::http::status::ok);
                responder->_connectionContext._response->set(boost::beast::http::field::content_type, "application/json");
                responder->_connectionContext._response->set(boost::beast::http::field::keep_alive, "false");
                responder->_connectionContext._response->body() = json.dump();
            });
            asyncResponder->RespondAsync();
        }
        else if (_connectionContext._request.method() == boost::beast::http::verb::get && _connectionContext._request.target() == "/metrics")
        {
            const auto asyncResponder = std::make_shared<AsyncMethodResponder>(weak_from_this(), _connectionContext, []
                (const std::shared_ptr<AsyncMethodResponder> & responder)
            {
                nlohmann::json json;
                json["CurrentHttpConnections"] = MetricManager::The()._currentHttpConnections.load();
                json["FinishedHttpConnections"] = MetricManager::The()._finishedHttpConnections.load();
                json["HttpRequests"] = MetricManager::The()._httpRequests.load();
                json["HttpResponses"] = MetricManager::The()._httpResponses.load();
                responder->_connectionContext._response->result(boost::beast::http::status::ok);
                responder->_connectionContext._response->set(boost::beast::http::field::content_type, "application/json");
                responder->_connectionContext._response->set(boost::beast::http::field::keep_alive, "false");
                responder->_connectionContext._response->body() = json.dump();
            });
            asyncResponder->RespondAsync();
        }
        else
        {
            _connectionContext._response->result(boost::beast::http::status::not_found);
            _connectionContext._response->set(boost::beast::http::field::content_type, "text/plain");
            _connectionContext._response->body() = "Not Found";
        }
    }

    void AsyncReadNextRequest() override
    {
        _connectionContext.Reset();
        _buffer.consume(_buffer.size());

        boost::beast::http::async_read(
            *_connectionContext._socket,
            _buffer,
            _connectionContext._request,
            boost::asio::bind_executor(
                _connectionContext._strand,
                [self = shared_from_this()](const boost::beast::error_code& ec, std::size_t)
                {
                    if (!ec)
                    {
                        Logger(Verbose) << "Http Server read some from connection: " << self->_connectionContext._socket->remote_endpoint().address();
                        self->_connectionContext._version = self->_connectionContext._request.version();
                        ++MetricManager::The()._httpRequests;
                        self->Process();
                    }
                    else {
                        Logger(Verbose) << "Http server couldn't read next for connection: " << self->_connectionContext._socket->remote_endpoint().address();
                    }
                }));
    }

    ConnectionContext _connectionContext;
    boost::beast::flat_buffer _buffer;
};


AtlasHttpNamespaceEnd
#endif //ATLASHTTP_ASYNCREADERWRITER_H