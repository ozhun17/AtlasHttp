//
// Created by mehme on 05/10/2026.
//

#ifndef ATLASHTTP_HTTPCLIENT_H
#define ATLASHTTP_HTTPCLIENT_H

#include <memory>
#include <functional>
#include <string>
#include <regex>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include "Namespace.h"
#include "HttpLogs.h"

AtlasHttpNamespaceBegin

// Forward declaration
struct HttpClientResponse;

struct HttpClient : std::enable_shared_from_this<HttpClient>
{
public:
	explicit HttpClient(
		boost::asio::io_context& ioContext,
		boost::asio::any_io_executor executor,
		std::function<void(HttpClientLogLevel, const std::string&)> onLog = nullptr
	)
		:
		_ioContext(ioContext),
		_executor(executor),
		_onLog(std::move(onLog))
	{
	}

	/**
	 * @brief Make an async HTTP request
	 * @param url The URL to request
	 * @param method HTTP method (GET, POST, PUT, DELETE, etc.)
	 * @param headers Optional headers to include
	 * @param body Optional request body
	 * @param callback Async callback with signature: void(const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
	 */
	void CallAsync(
		const std::string& url,
		boost::beast::http::verb method,
		const std::unordered_map<std::string, std::string>& headers,
		const std::string& body,
		std::function<void(const boost::system::error_code&, std::shared_ptr<HttpClientResponse>)> callback
	)
	{
		Log(HttpClientLogLevel::Verbose, "Making request: " + url + " " + std::string(to_string(method)));

		// Parse URL: scheme://host[:port]/path
		std::string scheme, host, path;
		int port = 0;
		ParseUrl(url, scheme, host, port, path);

		bool useHttps = (scheme == "https");
		if (port == 0)
		{
			port = useHttps ? 443 : 80;
		}

		// Create HTTP request
		
		auto request = std::make_shared<boost::beast::http::request<boost::beast::http::string_body>>();
		request->method(method);
		request->target(path.empty() ? "/" : path);
		request->version(11);

		// Add Host header
		std::string hostHeader = host;
		if ((useHttps && port != 443) || (!useHttps && port != 80))
		{
			hostHeader += ":" + std::to_string(port);
		}
		request->set(boost::beast::http::field::host, hostHeader);

		// Add headers
		for (const auto& [key, value] : headers)
		{
			request->set(key, value);
		}

		// Set body if provided
		if (!body.empty())
		{
			request->body() = body;
			request->prepare_payload();
		}

		// Create socket
		auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_executor);

		// Connect to host
		auto resolveContext = std::make_shared<boost::asio::io_context>();
		auto resolveExecutor = resolveContext->get_executor();
		boost::asio::ip::tcp::resolver resolver(resolveExecutor);
		resolver.async_resolve(
			host,
			std::to_string(port),
			boost::asio::bind_executor(_executor, [resolveExecutor, this, self = shared_from_this(), socket, host, useHttps, port,
				request = std::move(request), callback = std::move(callback)](
					const boost::system::error_code& ec,
					const boost::asio::ip::tcp::resolver::results_type& endpoints
					) mutable
				{
					if (ec)
					{
						Log(HttpClientLogLevel::Error, "Failed to resolve host: " + host + " Error: " + ec.message());
						if (callback)
						{
							callback(ec, nullptr);
						}
						return;
					}

					// Connect to endpoint
					boost::asio::async_connect(
						*socket,
						endpoints.begin(),
						endpoints.end(),
						[this, self = shared_from_this(), socket, request = std::move(request),
						callback = std::move(callback), useHttps, host](
							const boost::system::error_code& connectEc,
							const boost::asio::ip::tcp::resolver::results_type::iterator&
							) mutable
						{
							if (connectEc)
							{
								Log(HttpClientLogLevel::Error, "Failed to connect to host: " + host + " Error: " + connectEc.message());
								if (callback)
								{
									callback(connectEc, nullptr);
								}
								return;
							}

							if (useHttps)
							{
								// For HTTPS, we need SSL stream
								auto sslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tls_client);
								sslContext->set_options(
									boost::asio::ssl::context::default_workarounds |
									boost::asio::ssl::context::no_sslv2 |
									boost::asio::ssl::context::no_sslv3);

								// We need to wrap the socket in SSL stream
								auto conn = std::make_shared<SslConnection>();
								conn->socket = socket;
								conn->sslStream = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(
									std::move(*socket), *sslContext
								);

								// Set SNI hostname for certificate verification
								SSL_set_tlsext_host_name(conn->sslStream->native_handle(), host.c_str());

								// Perform SSL handshake
								conn->sslStream->async_handshake(
									boost::asio::ssl::stream_base::client,
									boost::asio::bind_executor(_executor,
										[this, self = shared_from_this(), conn,
										request = std::move(request), callback = std::move(callback)](
											const boost::system::error_code& handshakeEc
											) mutable
										{
											if (handshakeEc)
											{
												Log(HttpClientLogLevel::Error, "SSL handshake failed: " + handshakeEc.message());
												if (callback)
												{
													callback(handshakeEc, nullptr);
												}
												return;
											}

											// Send request over SSL
											SendRequestAsyncSslConnection(conn, std::move(request), callback);
										}
									)
								);
							}
							else
							{
								// For HTTP, send directly
								SendRequestAsyncPlain(socket, std::move(request), callback);
							}
						}
					);
				}
			)
		);
		std::thread([resolveContext = std::move(resolveContext)]() mutable {
			resolveContext->run();
			}).detach();
	}

private:
	struct SslConnection
	{
		std::shared_ptr<boost::asio::ip::tcp::socket> socket;
		std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> sslStream;
	};

	void ParseUrl(const std::string& url, std::string& scheme, std::string& host, int& port, std::string& path)
	{
		// Parse URL format: scheme://host[:port][/path]
		size_t schemeEnd = url.find("://");
		if (schemeEnd == std::string::npos)
		{
			throw std::runtime_error("Invalid URL: missing scheme");
		}

		scheme = url.substr(0, schemeEnd);
		size_t hostStart = schemeEnd + 3;

		// Find path start
		size_t pathStart = url.find('/', hostStart);
		if (pathStart == std::string::npos)
		{
			pathStart = url.length();
		}

		std::string hostPart = url.substr(hostStart, pathStart - hostStart);

		// Parse host and port
		size_t portColon = hostPart.find(':');
		if (portColon != std::string::npos)
		{
			host = hostPart.substr(0, portColon);
			port = std::stoi(hostPart.substr(portColon + 1));
		}
		else
		{
			host = hostPart;
			port = 0;  // Will be set to default later
		}

		path = url.substr(pathStart);
		if (path.empty())
		{
			path = "/";
		}
	}

	void SendRequestAsyncPlain(
		std::shared_ptr<boost::asio::ip::tcp::socket> socket,
		std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> request,
		std::function<void(const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)> callback
	)
	{
		// Send request
		boost::beast::http::async_write(
			*socket,
			*request,
			boost::asio::bind_executor(_executor,
				[this, self = shared_from_this(), socket, request = std::move(request),
				 callback = std::move(callback)](const boost::system::error_code& ec, std::size_t) mutable
				{
					if (ec)
					{
						Log(HttpClientLogLevel::Error, "Failed to send request: " + ec.message());
						if (callback)
						{
							callback(ec, nullptr);
						}
						return;
					}

					// Read response
					ReadResponseAsyncPlain(socket, callback);
				}
			)
		);
	}

	void SendRequestAsyncSslConnection(
		std::shared_ptr<SslConnection> conn,
		std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> request,
		std::function<void(const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)> callback
	)
	{
		// Send request over SSL
		std::ostringstream oss;
		oss << *request;
		std::string debugString = oss.str();
		Log(HttpClientLogLevel::Debug, "Request String: " + debugString);
		boost::beast::http::async_write(
			*conn->sslStream,
			*request,
			boost::asio::bind_executor(_executor,
				[this, self = shared_from_this(), conn, request=request,
				 callback = std::move(callback)](const boost::system::error_code& ec, std::size_t) mutable
				{
					if (ec)
					{
						Log(HttpClientLogLevel::Error, "Failed to send request: " + ec.message());
						if (callback)
						{
							callback(ec, nullptr);
						}
						return;
					}

					// Read response
					ReadResponseAsyncSslConnection(conn, callback);
				}
			)
		);
	}

	void ReadResponseAsyncPlain(
		std::shared_ptr<boost::asio::ip::tcp::socket> socket,
		std::function<void(const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)> callback
	)
	{
		auto buffer = std::make_shared<boost::beast::flat_buffer>();
		auto response = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>();

		boost::beast::http::async_read(
			*socket,
			*buffer,
			*response,
			boost::asio::bind_executor(_executor,
				[this, self = shared_from_this(), socket, buffer, response,
				 callback = std::move(callback)](const boost::system::error_code& ec, std::size_t)
				{
					if (ec)
					{
						Log(HttpClientLogLevel::Error, "Failed to read response: " + ec.message());
						if (callback)
						{
							callback(ec, nullptr);
						}
						return;
					}

					// Create response wrapper
					auto responsePtr = std::make_shared<HttpClientResponse>(*response);
					if (callback)
					{
						callback(ec, responsePtr);
					}
				}
			)
		);
	}

	void ReadResponseAsyncSslConnection(
		std::shared_ptr<SslConnection> conn,
		std::function<void(const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)> callback
	)
	{
		auto buffer = std::make_shared<boost::beast::flat_buffer>();
		auto response = std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>();

		boost::beast::http::async_read(
			*conn->sslStream,
			*buffer,
			*response,
			boost::asio::bind_executor(_executor,
				[this, self = shared_from_this(), conn, buffer, response,
				 callback = std::move(callback)](const boost::system::error_code& ec, std::size_t)
				{
					if (ec)
					{
						Log(HttpClientLogLevel::Error, "Failed to read response: " + ec.message());
						if (callback)
						{
							callback(ec, nullptr);
						}
						return;
					}

					// Create response wrapper
					auto responsePtr = std::make_shared<HttpClientResponse>(*response);
					if (callback)
					{
						callback(ec, responsePtr);
					}
				}
			)
		);
	}

	void Log(HttpClientLogLevel level, const std::string& message)
	{
		if (_onLog)
		{
			_onLog(level, message);
		}
	}

	//TODO CONNECTION KEEP ALIVE, TIMEOUTS, RETRIES.
	boost::asio::io_context & _ioContext;
	boost::asio::any_io_executor _executor;
	std::function<void(HttpClientLogLevel, const std::string&)> _onLog;
};

// Response wrapper
struct HttpClientResponse
{
	std::optional<boost::beast::http::response<boost::beast::http::string_body>> _response;
	//TODO ADD LATENCY METRICS
	std::optional<std::chrono::milliseconds> _resolveLatency;
	std::optional<std::chrono::milliseconds> _sslHandshakeLatency;
	std::optional<std::chrono::milliseconds> _connectLatency;
	std::optional<std::chrono::milliseconds> _writeLatency;

	explicit HttpClientResponse(const boost::beast::http::response<boost::beast::http::string_body>& response)
		: _response(response)
	{
	}

	explicit HttpClientResponse(boost::beast::http::response<boost::beast::http::string_body>&& response)
		: _response(std::move(response))
	{
	}

	explicit HttpClientResponse() = default;
	boost::beast::http::status status() const 
	{ 
		if(!_response.has_value())
		{
			throw std::runtime_error("Response is not available");
		}
		return _response->result();
	}
	int status_code() const 
	{ 
		if (!_response.has_value())
		{
			throw std::runtime_error("Response is not available");
		}
		return _response->result_int(); 
	}
	std::unordered_map<std::string, std::string> headers() const 
	{ 
		if (!_response.has_value())
		{
			throw std::runtime_error("Response is not available");
		}
		std::unordered_map<std::string, std::string> headersMap;
		for (const auto& field : *_response)
		{
			headersMap.emplace(std::string(field.name_string()), std::string(field.value()));
		}
		return headersMap; 
	}
	const std::string& body() const 
	{ 
		if (!_response.has_value())
		{
			throw std::runtime_error("Response is not available");
		}
		return _response->body(); 
	}
	unsigned int version() const 
	{
		if (!_response.has_value())
		{
			throw std::runtime_error("Response is not available");
		}
		return _response->version(); 
	}
};

AtlasHttpNamespaceEnd

#endif //ATLASHTTP_HTTPCLIENT_H

