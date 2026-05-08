#ifndef ATLASHTTP_ASYNCREADERWRITER_H
#define ATLASHTTP_ASYNCREADERWRITER_H
#include "ConnectionContext.h"
#include "WebSocketSession.h"
#include "AsyncMethodResponder.h"
#include "AsyncReader.h"
#include "MetricManager.h"
#include "HttpLogs.h"
#include "Namespace.h"
AtlasHttpNamespaceBegin

struct AsyncRequestProcessor : std::enable_shared_from_this<AsyncRequestProcessor>, AsyncReader
{
    AsyncRequestProcessor(
		std::function<void(HttpServerLogLevel, std::string)> onLog,
        std::shared_ptr<ConnectionContext> connectionContext,
        const std::unordered_map<std::string, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>> & requestHandlers,
        const std::unordered_map<std::string, WebSocketSession::WebSocketHandlers> & websocketHandlers
    )
        : 
		_onLog(std::move(onLog)),
        _connectionContext(std::move(connectionContext)), 
        _requestHandlers(requestHandlers)
    {
        (void)websocketHandlers; // stored below
        _websocketHandlers = &websocketHandlers;
    }

    void StartAsyncRead() {
        if (_connectionContext->IsSecure())
        {
            _connectionContext->AsyncHandshake(boost::asio::ssl::stream_base::server,
                boost::asio::bind_executor(
                    _connectionContext->_strand,
                    [self = shared_from_this()](const boost::system::error_code& ec)
                    {
                        if (!ec)
                        {
                            self->AsyncReadNextRequest();
                        }
                        else
                        {
							self->Log(HttpServerLogLevel::Error, "HTTPS handshake failed: " + ec.message());
                        }
                    }));
            return;
        }

        AsyncReadNextRequest();
    }
    ~AsyncRequestProcessor() override
    {
        Log(HttpServerLogLevel::Verbose, "Closing Async ReaderWriter");
    }

    void Process()
    {
        _connectionContext->_response->keep_alive(_connectionContext->_request.keep_alive());
        if (boost::beast::websocket::is_upgrade(_connectionContext->_request))
        {
            --MetricManager::The()._httpRequests;
            const auto path = std::string(_connectionContext->_request.target());
            if (_websocketHandlers && _websocketHandlers->contains(path))
            {
                // Move ownership of connection into a websocket session
                auto session = std::make_shared<WebSocketSession>(std::move(_connectionContext), _onLog);
                const auto& handlers = _websocketHandlers->at(path);

                if (handlers.onConnect)
                {
                    session->SetOnClientConnected(handlers.onConnect);
                }
                if (handlers.onText)
                {
                    session->SetOnTextMessage(handlers.onText);
                }
                if (handlers.onBinary)
                {
                    session->SetOnBinaryMessage(handlers.onBinary);
                }
                if (handlers.onDisconnect)
                {
                    session->SetOnDisconnect(handlers.onDisconnect);
                }

                session->Start();
                return;
            }
        }
        auto notFoundResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
			auto sharedConnectionContext = responder->_connectionContext.lock();
            if (!sharedConnectionContext)
            {
                return;
            }
            sharedConnectionContext->_response->result(boost::beast::http::status::not_found);
            sharedConnectionContext->_response->set(boost::beast::http::field::content_type, "text/plain");
            sharedConnectionContext->_response->body() = "Not Found";
        };
        if(!_requestHandlers.contains(std::string(_connectionContext->_request.target())))
        {
            auto asyncResponder = std::make_shared<AsyncMethodResponder>(_onLog, weak_from_this(), std::weak_ptr(_connectionContext), notFoundResponder);
            asyncResponder->RespondAsync();
            return;
        }
        auto notAllowedResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
            auto sharedConnectionContext = responder->_connectionContext.lock();
            if (!sharedConnectionContext)
            {
                return;
            }
            sharedConnectionContext->_response->result(boost::beast::http::status::method_not_allowed);
            sharedConnectionContext->_response->set(boost::beast::http::field::content_type, "text/plain");
            sharedConnectionContext->_response->body() = "Method Not Allowed";
        };
        if(!_requestHandlers.at(std::string(_connectionContext->_request.target())).contains(_connectionContext->_request.method()))
        {
            auto asyncResponder = std::make_shared<AsyncMethodResponder>(_onLog, weak_from_this(), std::weak_ptr(_connectionContext), notAllowedResponder);
            asyncResponder->RespondAsync();
            return;
        }
        const auto asyncResponder = std::make_shared<AsyncMethodResponder>(_onLog, weak_from_this(), std::weak_ptr(_connectionContext), _requestHandlers.at(std::string(_connectionContext->_request.target())).at(_connectionContext->_request.method()));
        asyncResponder->RespondAsync();
    }

    void AsyncReadNextRequest() override
    {
        _connectionContext->Reset();
        _buffer.consume(_buffer.size());

        _connectionContext->AsyncRead(
            _buffer,
            _connectionContext->_request,
            boost::asio::bind_executor(
                _connectionContext->_strand,
                [self = shared_from_this()](const boost::system::error_code& ec, std::size_t)
                {
                    if (!ec)
                    {
						self->Log(HttpServerLogLevel::Verbose, "Successfully read from connection: " + self->_connectionContext->RemoteEndpoint().address().to_string());
                        self->_connectionContext->_version = self->_connectionContext->_request.version();
                        ++MetricManager::The()._httpRequests;
                        self->Process();
                    }
                    else {
                        self->Log(HttpServerLogLevel::Verbose, "Http server couldn't read next for connection: " + self->_connectionContext->RemoteEndpoint().address().to_string());
                    }
                }));
    }
    void Log(HttpServerLogLevel level, const std::string& message)
    {
        if (_onLog)
        {
            _onLog(level, message);
        }
	}
	std::function<void(HttpServerLogLevel, std::string)> _onLog;
    const std::unordered_map<std::string, std::unordered_map<boost::beast::http::verb, std::function<void(const std::shared_ptr<AsyncMethodResponder>&)>>> & _requestHandlers;
    const std::unordered_map<std::string, WebSocketSession::WebSocketHandlers>* _websocketHandlers = nullptr;
    std::shared_ptr<ConnectionContext> _connectionContext;
    boost::beast::flat_buffer _buffer;
};


AtlasHttpNamespaceEnd
#endif //ATLASHTTP_ASYNCREADERWRITER_H