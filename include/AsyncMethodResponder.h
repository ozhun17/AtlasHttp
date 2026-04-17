#ifndef ATLASHTTP_ASYNCMETHODRESPONDER_H
#define ATLASHTTP_ASYNCMETHODRESPONDER_H
#include <memory>
#include <functional>
#include "AsyncReader.h"
#include "ConnectionContext.h"
#include "Namespace.h"
#include "MetricManager.h"
AtlasHttpNamespaceBegin

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

AtlasHttpNamespaceEnd

#endif //ATLASHTTP_ASYNCMETHODRESPONDER_H