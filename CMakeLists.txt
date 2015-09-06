cmake_minimum_required(VERSION 2.8)

project(websocket-cpp)

if(MSVC)
    add_definitions(/W4 /WX -D_WIN32_WINNT=0x0601)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
else()
    add_definitions(-std=c++14 -Wall -pedantic)
endif()

set(Boost_USE_STATIC_LIBS ${WIN32})
find_package(Boost 1.55 COMPONENTS coroutine context date_time regex system REQUIRED)

include_directories(. third_party ${Boost_INCLUDE_DIR})

add_executable(tests
    websocket-cpp.cpp
    Server.hpp
    server_fwd.hpp
    server_src.hpp
    details/Acceptor.hpp
    details/base64.hpp
    details/Connection.hpp
    details/frames.hpp
    details/handshake.hpp
    details/http.hpp
    details/http_parser.hpp
    details/ServerLogic.hpp
    details/sha1.hpp
    tests/base64_tests.cpp
    tests/frames_tests.cpp
    tests/handshake_tests.cpp
    tests/http_parser_tests.cpp
    tests/main.cpp
    tests/regression_tests.cpp
    tests/sha1_tests.cpp
    tests/catch_wrap.hpp
)

target_link_libraries(tests ${Boost_LIBRARIES})

if(WIN32)
    target_link_libraries(tests ws2_32 mswsock)
else()
    target_link_libraries(tests -lpthread)
endif()
