#ifndef ATLASHTTP_ASYNCMETHODRESPONDER_H
#define ATLASHTTP_ASYNCMETHODRESPONDER_H
#include <memory>
#include <functional>
#include <Logger.h>
#include "AsyncReader.h"
#include "ConnectionContext.h"
#include "Namespace.h"
#include "MetricManager.h"
AtlasHttpNamespaceBegin

struct AsyncMethodResponder: std::enable_shared_from_this<AsyncMethodResponder>
{
    struct MethodAliveKeeper
    {
        std::shared_ptr<AsyncReader> _reader;
        std::shared_ptr<AsyncMethodResponder> _responder;
        MethodAliveKeeper(std::shared_ptr<AsyncMethodResponder> responder)
            : 
            _reader(responder->_weakReader.lock()),
            _responder(std::move(responder))
        {
        }
        bool IsAlive() const
        {
            return _responder != nullptr && _reader != nullptr;
        }
        void Reset()
        {
            _responder.reset();
            _reader.reset();
        }
    };

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

    MethodAliveKeeper GetAliveKeeper()
    {
        return MethodAliveKeeper(shared_from_this());
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
        _connectionContext.AsyncWrite(
            *_connectionContext._response,
            boost::asio::bind_executor(
                _connectionContext._strand,
                [&connectionContext = _connectionContext, sharedReader](const boost::system::error_code& ec, std::size_t)
                {
                    ++MetricManager::The()._httpResponses;
                    if (ec)
                    {
                        boost::system::error_code ec2;
                        connectionContext.ShutdownBoth(ec2);
                        if (ec2)
                        {
                            Logger(Error) << "Connection Teardown! " << ec2.message();
                        }
                        sharedReader->AsyncReadNextRequest();
                        return;
                    }
                    Logger(Verbose) << "Http Server wrote async for connection: " << connectionContext.RemoteEndpoint().address();

                    if (!(connectionContext._response->keep_alive()))
                    {
                        boost::system::error_code ec2;
                        connectionContext.ShutdownSend(ec2);
                        if (ec2)
                        {
                            Logger(Error) << "Connection Teardown! " << ec2.message();
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