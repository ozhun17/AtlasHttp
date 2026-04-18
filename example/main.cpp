
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
    auto server = HTTPServer(context);
    EndpointPopulator{}(server);
    server.Start("0.0.0.0", "1411");
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
