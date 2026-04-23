#ifndef ATLASHTTP_ASYNCMETHODRESPONDER_H
#define ATLASHTTP_ASYNCMETHODRESPONDER_H
#include <memory>
#include <functional>
#include "AsyncReader.h"
#include "ConnectionContext.h"
#include "HttpLogs.h"
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
        std::function<void(HttpServerLogLevel, const std::string&)> onLog,
        std::weak_ptr<AsyncReader> weakReader,
        std::weak_ptr<ConnectionContext> connectionContext,
        std::function<void(std::shared_ptr<AsyncMethodResponder>)> requestHandler
        )
        :
		_onLog(std::move(onLog)),
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
		auto sharedConnectionContext = _connectionContext.lock();
        if (!sharedConnectionContext)
        {
			return;
        }
        sharedConnectionContext->_response->prepare_payload();
        sharedConnectionContext->AsyncWrite(
            *sharedConnectionContext->_response,
            boost::asio::bind_executor(
                sharedConnectionContext->_strand,
                [weakConnectionContext = _connectionContext, sharedReader, onLog = _onLog](const boost::system::error_code& ec, std::size_t)
                {
                    ++MetricManager::The()._httpResponses;
					auto sharedConnectionContext = weakConnectionContext.lock();
                    if (!sharedConnectionContext)
                    {
                        return;
                    }
                    if (ec)
                    {
                        boost::system::error_code ec2;
                        sharedConnectionContext->ShutdownBoth(ec2);
                        if (ec2)
                        {
                            if (onLog)
                            {
                                onLog(HttpServerLogLevel::Error, "Connection Teardown! " + ec2.message());
                            }
                        }
                        sharedReader->AsyncReadNextRequest();
                        return;
                    }
                    if (onLog)
                    {
                        onLog(HttpServerLogLevel::Verbose, "Http Server wrote async for connection: " + sharedConnectionContext->RemoteEndpoint().address().to_string());
                    }

                    if (!(sharedConnectionContext->_response->keep_alive()))
                    {
                        boost::system::error_code ec2;
                        sharedConnectionContext->ShutdownSend(ec2);
                        if (ec2)
                        {
                            if (onLog)
                            {
                                onLog(HttpServerLogLevel::Error, "Connection Teardown! " + ec2.message());
                            }
                        }
                        sharedReader->AsyncReadNextRequest();
                        return;
                    }
                    sharedReader->AsyncReadNextRequest();
                }
            ));
    }
    void Log(HttpServerLogLevel level, const std::string& message)
    {
        if (_onLog)
        {
            _onLog(level, message);
        }
	}
	std::function<void(HttpServerLogLevel, const std::string&)> _onLog;
    std::function<void(std::shared_ptr<AsyncMethodResponder>)> _requestHandler;
    std::weak_ptr<AsyncReader> _weakReader;
    std::weak_ptr<ConnectionContext> _connectionContext;
};

AtlasHttpNamespaceEnd

#endif //ATLASHTTP_ASYNCMETHODRESPONDER_H