// http parser tests
#include "details/http_parser.hpp"

#include "catch_wrap.hpp"

#include <sstream>

TEST_CASE("request line", "[http][parser]")
{
    http::Request rq;
    // init with some garbage values
    rq.method = http::Method::Unsupported;
    rq.requestPath = "...";
    rq.httpVersion = http::Version::Unsupported;

    std::stringstream stream("GET / HTTP/1.1\r\n~");
    REQUIRE(http::parser::parseRequestLine(stream, rq));
    REQUIRE(rq.method == http::Method::GET);
    REQUIRE(rq.requestPath == "/");
    REQUIRE(rq.httpVersion == http::Version::v1_1);
    REQUIRE(stream.get() == '~');
}

TEST_CASE("headers", "[http][parser]")
{
    http::Request rq;

    std::stringstream stream(
        "Connection: keep-alive, Upgrade\r\n"
        "Upgrade: websocket\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "some-name: some-value\r\n"
        "\r\n");

    REQUIRE(http::parser::parseRequestHeaders(stream, rq));

    REQUIRE(rq.upgrade.size() == 1);
    REQUIRE(rq.upgrade[0].name == "websocket");
    REQUIRE(rq.upgrade[0].version == "");

    REQUIRE(rq.connection.size() == 2);
    REQUIRE(rq.connection[0] == "keep-alive");
    REQUIRE(rq.connection[1] == "upgrade");

    REQUIRE(rq.secWebSocketVersion == 13);
    REQUIRE(rq.secWebSocketKey == "dGhlIHNhbXBsZSBub25jZQ==");

    stream.peek();
    REQUIRE(stream.eof());
}