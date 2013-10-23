#include "details/handshake.hpp"

#include "catch_wrap.hpp"

TEST_CASE("Validate request", "[websocket]")
{
    http::Request rq;
    rq.method = http::Method::GET;
    rq.requestPath = "/";
    rq.httpVersion = http::Version::v1_1;
    rq.secWebSocketVersion = 13;
    rq.secWebSocketKey = "AA==";

    rq.upgrade.push_back({"websocket", ""});

    rq.connection.push_back("keep-alive");
    rq.connection.push_back("upgrade");

    rq.secWebSocketVersion = 13;

    SECTION("ok")
    {
        REQUIRE(websocket::validateRequest(rq) == http::Status::OK);
    }

    SECTION("not GET")
    {
        rq.method = http::Method::POST;
        REQUIRE(websocket::validateRequest(rq) == http::Status::MethodNotAllowed);
    }

    SECTION("404")
    {
        rq.requestPath = "/foo";
        REQUIRE(websocket::validateRequest(rq) == http::Status::NotFound);
    }

    SECTION("not HTTP/1.1")
    {
        rq.httpVersion = http::Version::v1_0;
        REQUIRE(websocket::validateRequest(rq) == http::Status::HTTPVersionNotSupported);
    }

    SECTION("another websocket version")
    {
        rq.secWebSocketVersion = 1;
        REQUIRE(websocket::validateRequest(rq) == http::Status::NotImplemented);
    }

    SECTION("no `websocket' in upgrade field")
    {
        rq.upgrade.clear();
        rq.upgrade.push_back({"foo", ""});
        REQUIRE(websocket::validateRequest(rq) == http::Status::BadRequest);
    }

    SECTION("no `upgrade' in connection field")
    {
        rq.connection.clear();
        rq.connection.push_back("keep-alive");
        REQUIRE(websocket::validateRequest(rq) == http::Status::BadRequest);
    }
}

TEST_CASE("calc Sec-WebSocket-Accept", "[websocket]")
{
    REQUIRE(websocket::calcSecKeyHash("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("handshake", "[websocket]")
{
    std::istringstream request(
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n");

    std::ostringstream reply;
    REQUIRE(websocket::handshake(request, reply) == http::Status::OK);
    REQUIRE(reply.str() ==
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
        "\r\n");
}