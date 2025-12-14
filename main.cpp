
#include <csignal>
#include <iostream>

#include "Include/HttpServer/HttpServer.h"

#include "Include/Logger.h"
#include "Include/Namespace.h"

UsingAtlasNamespace

static std::atomic<bool> globalStop{ false };

static void signal_handler(int /*signum*/)
{
    globalStop.store(true);
}

int main()
{
    auto conf =LogManager::Config();
    conf.filePath= "logs.txt";
    conf.toFile = true;
    conf.minLevel = LogLevel::Verbose;
    LogManager::Init(conf);
    Logger(Info) << "Hello and welcome to Atlas Http";
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    boost::asio::io_context context;
    auto server = HTTPServer(context);
    server.Start("0.0.0.0", "1411");
    constexpr auto threadCount = 4;
    const auto threadPool = std::make_shared<boost::asio::thread_pool>(threadCount);
    boost::asio::post(*threadPool , [&context]()
        {
            try
            {
                context.run();
            }
            catch(const std::exception& e)
            {
                Logger(Error) << "IO Context run error: " << e.what() << std::endl;
            }
        });
    while (!globalStop.load())
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
