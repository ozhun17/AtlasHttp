
#include <iostream>

#include "Include/HttpServer.h"

#include "Include/Logger.h"


int main()
{
    auto conf =LogManager::Config();
    conf.filePath= "logs.txt";
    conf.toFile = true;
    conf.minLevel = LogLevel::Verbose;
    LogManager::Init(conf);
    Logger(Info) << "Hello and welcome to Atlas Http";
    boost::asio::io_context context;
    auto server = HTTPServer(context);

    return 0;
}