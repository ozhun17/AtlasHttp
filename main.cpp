
#include <iostream>

#include "Include/HttpServer.h"

#include "Include/Logger.h"


int main()
{
    auto conf =LogManager::Config();
    LogManager::Init(conf);
    const auto lang = "C++";
    std::cout << "Hello and welcome to " << lang << "!\n";
    Logger(Info) << "Hello";
    for (int i = 1; i <= 5; i++)
    {
        std::cout << "i = " << i << std::endl;
    }

    return 0;
}