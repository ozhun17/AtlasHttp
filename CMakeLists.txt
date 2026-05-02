cmake_minimum_required(VERSION 4.0)
project(AtlasHttp LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable debug symbols
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
endif()

if(MSVC)
    add_compile_options(/bigobj)
endif()




#   -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
#   -DVCPKG_TARGET_TRIPLET=x64-windows or x64-windows-static

# Boost.Log on Windows requires dynamic linking unless built static.
option(BUILD_EXAMPLES "Build examples" OFF)


find_package(Boost 1.90 REQUIRED
        COMPONENTS log log_setup program_options beast regex filesystem thread system
)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)

message(STATUS "Boost include dir: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libs: ${Boost_LIBRARIES}")
message(STATUS "OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "OpenSSL libs: ${OPENSSL_LIBRARIES}")

include_directories(${Boost_INCLUDE_DIRS})

add_library(AtlasHttp INTERFACE)

target_include_directories(AtlasHttp INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

target_link_libraries(AtlasHttp INTERFACE
    Boost::log
    Boost::log_setup
    Boost::program_options
    Boost::beast
    Boost::regex
    Boost::filesystem
    Boost::thread
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:wsock32>
)

target_compile_definitions(AtlasHttp INTERFACE
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
    $<$<PLATFORM_ID:Windows>:BOOST_LOG_DYN_LINK>
)

if(BUILD_EXAMPLES)
    add_subdirectory(example)
endif()