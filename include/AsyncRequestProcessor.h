#ifndef ATLASHTTP_ASYNCREADERWRITER_H
#define ATLASHTTP_ASYNCREADERWRITER_H
#include "ConnectionContext.h"
#include "AsyncMethodResponder.h"
#include "AsyncReader.h"
#include "MetricManager.h"
#include "Namespace.h"
AtlasHttpNamespaceBegin

struct AsyncRequestProcessor : std::enable_shared_from_this<AsyncRequestProcessor>, AsyncReader
{
    AsyncRequestProcessor(
        ConnectionContext connectionContext,
        const std::unordered_map<std::string, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>> & requestHandlers
    )
        : _connectionContext(std::move(connectionContext)), _requestHandlers(requestHandlers)
    {
    }

    void StartAsyncRead() {
        if (_connectionContext.IsSecure())
        {
            _connectionContext.AsyncHandshake(boost::asio::ssl::stream_base::server,
                boost::asio::bind_executor(
                    _connectionContext._strand,
                    [self = shared_from_this()](const boost::system::error_code& ec)
                    {
                        if (!ec)
                        {
                            self->AsyncReadNextRequest();
                        }
                        else
                        {
                            Logger(Error) << "HTTPS handshake failed: " << ec.message();
                        }
                    }));
            return;
        }

        AsyncReadNextRequest();
    }
    ~AsyncRequestProcessor() override
    {
        Logger(Verbose) << "Closing Async ReaderWriter";
    }

    void Process()
    {
        _connectionContext._response->keep_alive(_connectionContext._request.keep_alive());
        auto notFoundResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
            responder->_connectionContext._response->result(boost::beast::http::status::not_found);
            responder->_connectionContext._response->set(boost::beast::http::field::content_type, "text/plain");
            responder->_connectionContext._response->body() = "Not Found";
        };
        if(!_requestHandlers.contains(std::string(_connectionContext._request.target())))
        {
            auto asyncResponder = std::make_shared<AsyncMethodResponder>(weak_from_this(), _connectionContext, notFoundResponder);
            asyncResponder->RespondAsync();
            return;
        }
        auto notAllowedResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
            responder->_connectionContext._response->result(boost::beast::http::status::method_not_allowed);
            responder->_connectionContext._response->set(boost::beast::http::field::content_type, "text/plain");
            responder->_connectionContext._response->body() = "Method Not Allowed";
        };
        if(!_requestHandlers.at(std::string(_connectionContext._request.target())).contains(_connectionContext._request.method()))
        {
            auto asyncResponder = std::make_shared<AsyncMethodResponder>(weak_from_this(), _connectionContext, notAllowedResponder);
            asyncResponder->RespondAsync();
            return;
        }
        const auto asyncResponder = std::make_shared<AsyncMethodResponder>(weak_from_this(), _connectionContext, _requestHandlers.at(std::string(_connectionContext._request.target())).at(_connectionContext._request.method()));
        asyncResponder->RespondAsync();
    }

    void AsyncReadNextRequest() override
    {
        _connectionContext.Reset();
        _buffer.consume(_buffer.size());

        _connectionContext.AsyncRead(
            _buffer,
            _connectionContext._request,
            boost::asio::bind_executor(
                _connectionContext._strand,
                [self = shared_from_this()](const boost::system::error_code& ec, std::size_t)
                {
                    if (!ec)
                    {
                        Logger(Verbose) << "Http Server read some from connection: " << self->_connectionContext.RemoteEndpoint().address();
                        self->_connectionContext._version = self->_connectionContext._request.version();
                        ++MetricManager::The()._httpRequests;
                        self->Process();
                    }
                    else {
                        Logger(Verbose) << "Http server couldn't read next for connection: " << self->_connectionContext.RemoteEndpoint().address();
                    }
                }));
    }
    const std::unordered_map<std::string, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>> & _requestHandlers;
    ConnectionContext _connectionContext;
    boost::beast::flat_buffer _buffer;
};


AtlasHttpNamespaceEnd
#endif //ATLASHTTP_ASYNCREADERWRITER_H