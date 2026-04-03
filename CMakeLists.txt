cmake_minimum_required(VERSION 4.0)
project(AtlasHttp LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#   -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
#   -DVCPKG_TARGET_TRIPLET=x64-windows or x64-windows-static

# Boost.Log on Windows requires dynamic linking unless built static.



find_package(Boost 1.90 REQUIRED
        COMPONENTS log log_setup program_options beast regex filesystem thread
)
find_package(nlohmann_json CONFIG REQUIRED)

message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libs: ${Boost_LIBRARIES}")

include_directories(${Boost_INCLUDE_DIRS})

add_executable(AtlasHttp
        main.cpp
        Include/HttpServer/HttpServer.h
        Include/Logger.h
        Include/MetricManager.h
        Include/Namespace.h
        Include/HttpServer/AsyncReaderWriter.h
)

target_link_libraries(AtlasHttp
        Boost::log
        Boost::log_setup
        Boost::program_options
        Boost::regex
        Boost::filesystem
        Boost::thread
        ws2_32
        wsock32
)

target_compile_definitions(AtlasHttp PRIVATE
        _WIN32_WINNT=0x0A00
        BOOST_LOG_DYN_LINK
)
