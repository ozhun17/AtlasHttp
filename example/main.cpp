
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>
#include <Logger.h>
#include <boost/asio.hpp>
#include "../include/HttpServer.h"
#include "../include/Namespace.h"
#include "EndpointPopulator.h"

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
    Logger(Info) << "Hello and welcome to Atlas Http";
    boost::asio::io_context context;
    auto onLog = [](HttpServerLogLevel level, const std::string& message)
    {
        switch (level)
        {
        case HttpServerLogLevel::Verbose:
            Logger(Verbose) << message;
            break;
        case HttpServerLogLevel::Debug:
            Logger(Debug) << message;
            break;
        case HttpServerLogLevel::Info:
            Logger(Info) << message;
            break;
        case HttpServerLogLevel::Warning:
            Logger(Warning) << message;
            break;
        case HttpServerLogLevel::Error:
            Logger(Error) << message;
            break;
        case HttpServerLogLevel::Critical:
            Logger(Fatal) << message;
            break;
        default:
            Logger(Info) << message;
            break;
        }
		};
    auto server = HTTPServer(context, onLog);
    EndpointPopulator{}(server);
    constexpr bool useHttps = false;
    const auto certificateFile = "certfile.pem";
    const auto privateKeyFile = "keyfile.pem";
    server.Start("0.0.0.0", "1411", useHttps, certificateFile, privateKeyFile);
    constexpr auto threadCount = 4;
    const auto threadPool = std::make_shared<boost::asio::thread_pool>(threadCount);
    boost::asio::post(*threadPool, [&context]()
    {
        try
        {
            context.run();
        }
        catch (const std::exception& e)
        {
            Logger(Error) << "IO Context run error: " << e.what() << std::endl;
        }
    });

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    Logger(Fatal) << "Shutting down servers...";
    server.Stop();
    if (threadPool)
    {
        threadPool->stop();
        threadPool->join();
    }

    return 0;
}
