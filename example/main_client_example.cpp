#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include "external/atlas-logger/include/Logger.h"
#include <boost/asio.hpp>
#include "../include/HttpServer.h"
#include "../include/HttpClient.h"
#include "../include/Namespace.h"

UsingAtlasNamespace
UsingAtlasHttpNamespace

int main()
{
    auto conf = Atlas::LogManager::Config();
    conf.filePath = "logs.txt";
    conf.toFile = true;
    conf.colorConsole = true;
    conf.toConsole = true;
    conf.minLevel = LogLevel::Verbose;
    conf.immediateFlush = true;
    Atlas::LogManager::Init(conf);
    Logger(Info) << "Hello and welcome to Atlas Http with HttpClient";

    // Create IO context
    boost::asio::io_context context;
	auto strand = boost::asio::make_strand(context);
    // Create HTTP client with the same IO context and executor
    auto onLog = [](HttpClientLogLevel level, const std::string& message)
    {
        switch (level)
        {
        case HttpClientLogLevel::Verbose:
            Logger(Verbose) << "[HttpClient] " << message;
            break;
        case HttpClientLogLevel::Debug:
            Logger(Debug) << "[HttpClient] " << message;
            break;
        case HttpClientLogLevel::Info:
            Logger(Info) << "[HttpClient] " << message;
            break;
        case HttpClientLogLevel::Warning:
            Logger(Warning) << "[HttpClient] " << message;
            break;
        case HttpClientLogLevel::Error:
            Logger(Error) << "[HttpClient] " << message;
            break;
        case HttpClientLogLevel::Critical:
            Logger(Fatal) << "[HttpClient] " << message;
            break;
        default:
            Logger(Info) << "[HttpClient] " << message;
            break;
        }
    };
    auto httpClient1 = std::make_shared<HttpClient>(context, strand, onLog);

    // Example 1: Simple GET request
    Logger(Info) << "\n=== Example 1: Simple GET request ===";
    httpClient1->CallAsync(
        "https://httpbin.org/get",
        boost::beast::http::verb::get,
        {},  // headers
        "",  // body
        [httpClient1](const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
        {
            if (ec)
            {
                Logger(Error) << "GET request failed: " << ec.message();
                return;
            }

            Logger(Info) << "Status: " << static_cast<int>(response->status()) << " (" << response->status_code() << ")";
            Logger(Info) << "Body length: " << response->body().size();
            // Print first 200 chars of body
            auto body = response->body();
            if (body.size() > 200)
            {
                body.resize(200);
                body += "...";
            }
            Logger(Info) << "Response: " << body;
            httpClient1->CallAsync(
                "https://httpbin.org/get",
                boost::beast::http::verb::get,
                {},  // headers
                "",  // body
                [httpClient1](const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
                {
                    if (ec)
                    {
                        Logger(Error) << "GET request failed: " << ec.message();
                        return;
                    }

                    Logger(Info) << "Status: " << static_cast<int>(response->status()) << " (" << response->status_code() << ")";
                    Logger(Info) << "Body length: " << response->body().size();
                    // Print first 200 chars of body
                    auto body = response->body();
                    if (body.size() > 200)
                    {
                        body.resize(200);
                        body += "...";
                    }
                    Logger(Info) << "Response: " << body;
                }
            );

        }
    );

    // Example 2: POST request with headers and body
    Logger(Info) << "\n=== Example 2: POST request with headers and body ===";
    std::unordered_map<std::string, std::string> headers;
    headers["Content-Type"] = "application/json";
    headers["Accept"] = "application/json";

    std::string requestBody = R"({
        "name": "test",
        "value": 123
    })";


    auto httpClient2 = std::make_shared<HttpClient>(context,strand, onLog);

    httpClient2->CallAsync(
        "https://httpbin.org/post",
        boost::beast::http::verb::post,
        headers,
        requestBody,
        [httpClient2](const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
        {
            if (ec)
            {
                Logger(Error) << "POST request failed: " << ec.message();
                return;
            }

            Logger(Info) << "Status: " << static_cast<int>(response->status()) << " (" << response->status_code() << ")";
            auto body = response->body();
            if (body.size() > 200)
            {
                body.resize(200);
                body += "...";
            }
            Logger(Info) << "Response: " << body;
        }
    );

    // Example 3: PUT request
    Logger(Info) << "\n=== Example 3: PUT request ===";
    std::string putBody = R"({
        "key": "value"
    })";
    auto httpClient3 = std::make_shared<HttpClient>(context, strand, onLog);

    httpClient3->CallAsync(
        "https://httpbin.org/put",
        boost::beast::http::verb::put,
        headers,
        putBody,
        [httpClient3](const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
        {
            if (ec)
            {
                Logger(Error) << "PUT request failed: " << ec.message();
                return;
            }

            Logger(Info) << "Status: " << static_cast<int>(response->status()) << " (" << response->status_code() << ")";
            auto body = response->body();
            if (body.size() > 200)
            {
                body.resize(200);
                body += "...";
            }
            Logger(Info) << "Response: " << body;
        }
    );
    auto httpClient4 = std::make_shared<HttpClient>(context, strand, onLog);

    // Example 4: DELETE request
    Logger(Info) << "\n=== Example 4: DELETE request ===";
    httpClient4->CallAsync(
        "https://httpbin.org/delete",
        boost::beast::http::verb::delete_,
        {},
        "",
        [httpClient4](const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
        {
            if (ec)
            {
                Logger(Error) << "DELETE request failed: " << ec.message();
                return;
            }

            Logger(Info) << "Status: " << static_cast<int>(response->status()) << " (" << response->status_code() << ")";
            auto body = response->body();
            if (body.size() > 200)
            {
                body.resize(200);
                body += "...";
            }
            Logger(Info) << "Response: " << body;
        }
    );

    // Example 5: Request with custom headers
    Logger(Info) << "\n=== Example 5: Request with custom headers ===";
    std::unordered_map<std::string, std::string> customHeaders;
    customHeaders["Content-Type"] = "application/json";
    customHeaders["Authorization"] = "Bearer your_token_here";
    customHeaders["User-Agent"] = "AtlasHttp/1.0";
    auto httpClient5 = std::make_shared<HttpClient>(context, strand, onLog);

    httpClient5->CallAsync(
        "https://httpbin.org/headers",
        boost::beast::http::verb::get,
        customHeaders,
        "",
        [httpClient5](const boost::system::error_code& ec, std::shared_ptr<HttpClientResponse> response)
        {
            if (ec)
            {
                Logger(Error) << "Request with custom headers failed: " << ec.message();
                return;
            }

            Logger(Info) << "Status: " << static_cast<int>(response->status()) << " (" << response->status_code() << ")";
            auto body = response->body();
            if (body.size() > 200)
            {
                body.resize(200);
                body += "...";
            }
            Logger(Info) << "Response: " << body;
        }
    );

    // Run the IO context to process async operations
    constexpr auto threadCount = 4;
    const auto threadPool = std::make_shared<boost::asio::thread_pool>(threadCount);
    boost::asio::post(*threadPool, [&context, strand]()
    {
        while (true)
        {
            try
            {
                context.run();
            }
            catch (const std::exception& e)
            {
                Logger(Error) << "IO Context run error: " << e.what() << std::endl;
            }
        }
    });
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    if (threadPool)
    {
        threadPool->stop();
        threadPool->join();
    }
    Logger(Fatal) << "Shutting down...";

    return 0;
}
