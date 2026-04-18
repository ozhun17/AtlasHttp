add_executable(AtlasHttpExample main.cpp)

target_link_libraries(AtlasHttpExample AtlasHttp)

# Explicitly link Boost libraries for MinGW linker
find_package(Boost 1.90 REQUIRED COMPONENTS log log_setup)
target_link_libraries(AtlasHttpExample Boost::log Boost::log_setup)