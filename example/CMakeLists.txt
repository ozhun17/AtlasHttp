add_executable(AtlasHttpExample main.cpp)

add_subdirectory(external/atlas-logger)
include_directories(external/atlas-logger/include)

target_include_directories(AtlasHttp INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/external/atlas-logger/include
)

target_link_libraries(AtlasHttpExample 
    AtlasLogger
	AtlasHttp 
)

# Explicitly link Boost libraries for MinGW linker
find_package(Boost 1.90 REQUIRED COMPONENTS log log_setup)
target_link_libraries(AtlasHttpExample Boost::log Boost::log_setup)