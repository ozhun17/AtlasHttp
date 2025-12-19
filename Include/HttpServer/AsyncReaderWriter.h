//
// Created by mehme on 12/14/2025.
//

#ifndef ATLASHTTP_ASYNCREADERWRITER_H
#define ATLASHTTP_ASYNCREADERWRITER_H
#include "../Namespace.h"
#include "../Logger.h"
#include "../MetricManager.h"
AtlasNamespaceBegin

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
    ~AsyncReaderWriter() {
        Logger(Verbose) << "Closing Async ReaderWriter";
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
            _response->set(boost::beast::http::field::keep_alive, "false");
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
                        Logger(Verbose) << "Http Server read some from connection: " << self->_socket->remote_endpoint().address();
                        self->_version = self->_request.version();
                        MetricManager::The()._httpRequests++;
                        self->Process();
                    }
                    else {
                        Logger(Verbose) << "Http server couldn't read next for connection: " << self->_socket->remote_endpoint().address();
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
                [this, self = shared_from_this()](const boost::system::error_code& ec, std::size_t)
                {
                    if (ec)
                    {
                        MetricManager::The()._httpResponses++;
                        boost::system::error_code ec2;
                        self->_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec2); // NOLINT(*-unused-return-value)
                        if (ec2)
                        {
                            Logger(Error) << "Connection Teardown!" << ec.message();
                        }
                        return;
                    }
                    Logger(Info) << "KeepAlive:" << std::to_string(self->_response->keep_alive());
                    Logger(Verbose) << "Http Server wrote async for connection: " << self->_socket->remote_endpoint().address();

                    if (!(self->_response->keep_alive()))
                    {
                        MetricManager::The()._httpResponses++;
                        boost::system::error_code ec2;
                        self->_socket->shutdown(boost::asio::ip::tcp::socket::shutdown_send, ec2); // NOLINT(*-unused-return-value)
                        if (ec2)
                        {
                            Logger(Error) << "Connection Teardown!" << ec.message();
                        }
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


AtlasNamespaceEnd
#endif //ATLASHTTP_ASYNCREADERWRITER_H