// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <algorithm>
#include <string>
#include "http_parser.hpp"
#include "sha1.hpp"
#include "base64.hpp"

namespace websocket { namespace details
{
    inline http::Status validateRequest(const http::Request& rq)
    {
        if (rq.method != http::Method::GET)
            return http::Status::MethodNotAllowed;

        if (rq.requestPath != "/")
            return http::Status::NotFound;

        if (rq.httpVersion != http::Version::v1_1)
            return http::Status::HTTPVersionNotSupported;

        if (rq.secWebSocketVersion != 13)
            return http::Status::NotImplemented;

        if (std::find(begin(rq.connection), end(rq.connection), "upgrade") == end(rq.connection))
            return http::Status::BadRequest;

        if (std::find_if(begin(rq.upgrade), end(rq.upgrade), [](const http::Product& p) { return p.name == "websocket"; }) == end(rq.upgrade))
            return http::Status::BadRequest;

        return http::Status::OK;
    }

    inline std::string calcSecKeyHash(const std::string& clientKey)
    {
        auto websocketKeyGUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        SHA1 sha1;
        sha1.update(&clientKey[0], clientKey.size());
        sha1.update(websocketKeyGUID, std::strlen(websocketKeyGUID));
        char sha1Buffer[SHA1::DIGEST_SIZE];
        sha1.digest(sha1Buffer);

        return b64encode(sha1Buffer, SHA1::DIGEST_SIZE);
    }

    inline http::Status processHandshakeRequest(std::istream& stream, http::Request& rq)
    {
        rq.secWebSocketVersion = 0;

        if (!http::parser::parseRequestLine(stream, rq))
            return http::Status::BadRequest;

        if (!http::parser::parseRequestHeaders(stream, rq))
            return http::Status::BadRequest;

        stream.peek();
        if (!stream.eof())
            return http::Status::BadRequest;

        return validateRequest(rq);
    }

    inline http::Status handshake(std::istream& requestStream, std::ostream& replyStream)
    {
        http::Request rq;
        auto status = processHandshakeRequest(requestStream, rq);

        if (status == http::Status::OK)
        {
            replyStream <<
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " << calcSecKeyHash(rq.secWebSocketKey) << "\r\n"
                "\r\n";
        }
        else
        {
            replyStream << "HTTP/1.1 " << (int)status << " :(\r\n\r\n";
        }

        return status;
    }
}}