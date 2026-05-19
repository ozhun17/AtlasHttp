add_executable(AtlasHttpExample main_client_example.cpp)

add_subdirectory(external/atlas-logger)
include_directories(external/atlas-logger/include)

target_include_directories(AtlasHttp INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/external/atlas-logger/include
)

target_link_libraries(AtlasHttpExample 
    AtlasLogger
	AtlasHttp 
)
