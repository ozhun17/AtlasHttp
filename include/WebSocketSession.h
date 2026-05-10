#ifndef ATLASHTTP_WEBSOCKETSESSION_H
#define ATLASHTTP_WEBSOCKETSESSION_H

#include <memory>
#include <functional>
#include <string>
#include <deque>
#include <variant>
#include <any>
#include <boost/beast/websocket.hpp>
#include "ConnectionContext.h"
#include "MetricManager.h"
#include "Namespace.h"

AtlasHttpNamespaceBegin

struct WebSocketSession : public std::enable_shared_from_this<WebSocketSession>
{
    WebSocketSession(
        std::shared_ptr<ConnectionContext> connectionContext,
        std::function<void(HttpServerLogLevel, std::string)> onLog
    )
		: _connectionContext(std::move(connectionContext)),
        _onLog(std::move(onLog))

    {
        Log(HttpServerLogLevel::Verbose, "Constructing WebSocketSession");
		MetricManager::The()._currentWebsocketSessions++;
    }

    ~WebSocketSession()
    {
		Log(HttpServerLogLevel::Verbose, "Destructing WebSocketSession");
        MetricManager::The()._currentWebsocketSessions--;
        MetricManager::The()._closedWebsocketSessions++;
    }

    using OnClientConnectedHandler = std::function<void(std::shared_ptr<WebSocketSession>)>;
    using OnTextMessageHandler = std::function<void(std::shared_ptr<WebSocketSession>, const std::string&)>;
    using OnBinaryMessageHandler = std::function<void(std::shared_ptr<WebSocketSession>, const std::string&)>;
    using OnDisconnectHandler = std::function<void(std::shared_ptr<WebSocketSession>)>;

    struct WebSocketHandlers
    {
        OnClientConnectedHandler onConnect;
        OnBinaryMessageHandler onBinary;
        OnTextMessageHandler onText;
        OnDisconnectHandler onDisconnect;
    };

    struct SessionAliveKeeper
    {
        std::shared_ptr<WebSocketSession> _session;

        explicit SessionAliveKeeper(
            std::shared_ptr<WebSocketSession> session
            )
            : _session(std::move(session))
        {
        }

        bool IsAlive() const
        {
            return _session != nullptr;
        }

        void Reset()
        {
            _session.reset();
        }
    };

    void SetOnClientConnected(OnClientConnectedHandler handler)
    {
        _onClientConnected = std::move(handler);
    }

    void SetOnTextMessage(OnTextMessageHandler handler)
    {
        _onTextMessage = std::move(handler);
    }

    void SetOnBinaryMessage(OnBinaryMessageHandler handler)
    {
        _onBinaryMessage = std::move(handler);
    }

    void SetOnDisconnect(OnDisconnectHandler handler)
    {
        _onDisconnect = std::move(handler);
    }

    void Start()
    {
        Log(HttpServerLogLevel::Verbose, "Starting WebSocket session");

        // Accept the websocket handshake using the already-read HTTP request
        auto self = shared_from_this();
        auto acceptHandler = [this, self](const boost::system::error_code& ec)
        {
            if (ec)
            {
                Log(HttpServerLogLevel::Error, "WebSocket accept failed: " + ec.message());
                return;
            }
            if (_onClientConnected)
            {
                _connected = true;
                _onClientConnected(self);
            }
            DoRead();
        };

        if (_connectionContext->IsSecure())
        {
            // Create websocket stream from SSL socket
            auto sslPtr = std::get<std::shared_ptr<ConnectionContext::SslSocket>>(_connectionContext->_stream);
            // Store raw pointer for use in async operations (ConnectionContext keeps shared_ptr alive)
            using WsType = boost::beast::websocket::stream<ConnectionContext::SslSocket&>;
            auto wsStream = std::make_shared<WsType>(*sslPtr);
            _sslWsStream = std::move(wsStream);
            _isSecure = true;

            _sslWsStream->async_accept(_connectionContext->_request, boost::asio::bind_executor(_connectionContext->_strand, acceptHandler));
        }
        else
        {
            // Create websocket stream from underlying plain tcp socket
            auto socketPtr = std::get<std::shared_ptr<boost::asio::ip::tcp::socket>>(_connectionContext->_stream);
            using WsType = boost::beast::websocket::stream<boost::asio::ip::tcp::socket&>;
            auto wsStream = std::make_shared<WsType>(*socketPtr);
            _plainWsStream = std::move(wsStream);
            _isSecure = false;

            _plainWsStream->async_accept(_connectionContext->_request, boost::asio::bind_executor(_connectionContext->_strand, acceptHandler));
        }
    }

    void SendText(const std::string& message)
    {
        auto self = shared_from_this();
        boost::asio::post(_connectionContext->_strand, [this, self, message = message]() mutable
        {
            bool writeInProgress = !_sendQueue.empty();
            _sendQueue.push_back({ message, false });
            if (!writeInProgress)
            {
                DoWrite();
            }
        });
    }

    void SendBinary(const std::string& message)
    {
        auto self = shared_from_this();
        boost::asio::post(_connectionContext->_strand, [this, self, message = message]() mutable
        {
            bool writeInProgress = !_sendQueue.empty();
            _sendQueue.push_back({ message, true });
            if (!writeInProgress)
            {
                DoWrite();
            }
        });
    }

	std::weak_ptr<ConnectionContext> GetConnectionContext()
	{
		return std::weak_ptr<ConnectionContext>(_connectionContext);
	}

	SessionAliveKeeper GetAliveKeeper()
	{
		return SessionAliveKeeper(shared_from_this());
	}

private:
    void DoRead()
    {
        _buffer.consume(_buffer.size());
        auto self = shared_from_this();

        if (_isSecure)
        {
            _sslWsStream->async_read(_buffer, boost::asio::bind_executor(_connectionContext->_strand,
                [this, self](const boost::system::error_code& ec, std::size_t bytes)
                {
                    if (ec)
                    {
                        Log(HttpServerLogLevel::Verbose, "WebSocket read error: " + ec.message());
                        if (_onDisconnect)
                        {
                            _connected = false;
                            _onDisconnect(self);
                        }
                        return;
                    }
                    const auto data = boost::beast::buffers_to_string(_buffer.data());

                    // Check if the stream is in text mode (default is true for websockets)
                    bool isTextFrame = _isSecure ? _sslWsStream->is_message_done() : _plainWsStream->is_message_done();

                    if (_onTextMessage && isTextFrame)
                    {
                        _onTextMessage(self, data);
                    }
                    else if (_onBinaryMessage && !isTextFrame)
                    {
                        _onBinaryMessage(self, data);
                    }
                    DoRead();
                }
            ));
        }
        else
        {
            _plainWsStream->async_read(_buffer, boost::asio::bind_executor(_connectionContext->_strand,
                [this, self](const boost::system::error_code& ec, std::size_t bytes)
                {
                    if (ec)
                    {
                        Log(HttpServerLogLevel::Verbose, "WebSocket read error: " + ec.message());
                        if (_onDisconnect)
                        {
                            _connected = false;
                            _onDisconnect(self);
                        }
                        return;
                    }
                    const auto data = boost::beast::buffers_to_string(_buffer.data());

                    // Check if the stream is in text mode (default is true for websockets)
                    bool isTextFrame = _isSecure ? _sslWsStream->is_message_done() : _plainWsStream->is_message_done();

                    if (_onTextMessage && isTextFrame)
                    {
                        _onTextMessage(self, data);
                    }
                    else if (_onBinaryMessage && !isTextFrame)
                    {
                        _onBinaryMessage(self, data);
                    }
                    DoRead();
                }
            ));
        }
    }

	void DoWrite()
	{
		if (_sendQueue.empty())
		{
			return;
		}
		auto isBinary = _sendQueue.front().second;
		auto self = shared_from_this();

		if (_isSecure)
		{
			_sslWsStream->text(!isBinary);
			_sslWsStream->async_write(boost::asio::buffer(_sendQueue.front().first), boost::asio::bind_executor(_connectionContext->_strand,
				[this, self](const boost::system::error_code& ec, std::size_t)
				{
					if (ec)
					{
						if(!_connected)
						{
							Log(HttpServerLogLevel::Info, "Couldn't write since websocket disconnected: " + ec.message());
							return;
						}
						Log(HttpServerLogLevel::Error, "WebSocket write error: " + ec.message());
						return;
					}
					_sendQueue.pop_front();
					if (!_sendQueue.empty())
					{
						DoWrite();
					}
				}
			));
		}
		else
		{
			_plainWsStream->text(!isBinary);
			_plainWsStream->async_write(boost::asio::buffer(_sendQueue.front().first), boost::asio::bind_executor(_connectionContext->_strand,
				[this, self](const boost::system::error_code& ec, std::size_t)
				{
					if (ec)
					{
						if(!_connected)
						{
							Log(HttpServerLogLevel::Info, "Couldn't write since websocket disconnected: " + ec.message());
							return;
						}
						Log(HttpServerLogLevel::Error, "WebSocket write error: " + ec.message());
						return;
					}
					_sendQueue.pop_front();
					if (!_sendQueue.empty())
					{
						DoWrite();
					}
				}
			));
		}
	}

    void Log(HttpServerLogLevel level, const std::string& message)
    {
        if (_onLog)
        {
            _onLog(level, message);
        }
    }


    std::shared_ptr<ConnectionContext> _connectionContext;
    OnClientConnectedHandler _onClientConnected;
    OnTextMessageHandler _onTextMessage;
    OnBinaryMessageHandler _onBinaryMessage;
    OnDisconnectHandler _onDisconnect;
    boost::beast::flat_buffer _buffer;
    std::deque<std::pair<std::string, bool>> _sendQueue;
    bool _connected = true;
    bool _isSecure = false;
    // WebSocket streams for plain TCP and SSL
    using PlainWs = boost::beast::websocket::stream<boost::asio::ip::tcp::socket&>;
    using SslWs = boost::beast::websocket::stream<ConnectionContext::SslSocket&>;
    std::shared_ptr<PlainWs> _plainWsStream;
    std::shared_ptr<SslWs> _sslWsStream;
    std::function<void(HttpServerLogLevel, std::string)> _onLog;

};

AtlasHttpNamespaceEnd

#endif // ATLASHTTP_WEBSOCKETSESSION_H
