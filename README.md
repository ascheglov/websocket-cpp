# WebSockets for C++

*Version 0.1 alpha*

Quick and dirty implementation of WebSocket server.

For testing purposes only.

## Example

    websocket::Server server;

    // listen on 0.0.0.0:8888
    // log errors to stderr
    server.start("0.0.0.0", 8888, std::cerr); 

    ...

    // check for a new event
    websocket::Event event;
    websocket::ConnectionId connId;
    std::string message;
    if (server.poll(event, connId, message) {
        switch (event) {
        case websocket::Event::NewConnection:
            std::cout << '#' << connId << " connected\n";
            break;
        case websocket::Event::Message:
            std::cout << '#' << connId << " says " << message << '\n';
            // send reply
            server.send(connId, message);
            break;
        case websocket::Event::Disconnect:
            std::cout << '#' << connId << " disconnected\n";
            break;
        }
    }

    ...

    // stop server
    server.stop();
    // destructor also can call stop(), but it's better to do it explicitly

## Features and limitations

* Fragmented messages are not supported
* Client can't send a message longer than 125 bytes
* Server can't send a message longer than UINT32_MAX bytes
* Server doesn't validate client text frames.

## Overview of the WebSocket protocol

### Handshake

Client sends HTTP GET request

    GET /some/path/with/optional?query HTTP/1.1
    Upgrade: websocket
    Connection: Upgrade
    Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
    Sec-WebSocket-Version: 13
    ... other fields ...

Server replies with 

    HTTP/1.1 101 Switching Protocols
    Upgrade: websocket
    Connection: Upgrade
    Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=

and then they send *data frames* for each other

### Client data frames

Data frames from client looks like this:

    XX YY [LL LL | LL LL LL LL LL LL LL LL] KK KK KK KK [data]

where

* `XX` - byte with opcode and the "final fragment" flag, 0x81 for non-fragmented text frame.
* `YY` - byte with data length, high bit is set to 1, i.e. 0x85 for 5-byte data.  
If length is 126, next two bytes are real 16-bit length.
If length is 127, next eight bytes are real 64-bit length.
* `KK` - four bytes of the *masking key*
* `data` - data bytes XORed with the masking key, `data[i] ^ key[i % 4]`.

Example:

    81 85 F0 F0 F0 F0 C1 C2 C3 C4 C5 -- text "12345"

### Server data frames

Almost like client frames, but without masking.

    XX LL [LL LL | LL LL LL LL LL LL LL LL] [data]

* `XX` - opcode and "final fragment" flag.
* `LL` - data length (0 - 125), 126 and 127 for extended length.
* `data` - data bytes.

Example:

    81 05 31 32 33 34 35 -- text "12345"

## License

**websocket-cpp** is placed in the public domain.

## External links

* [RFC 6455](http://tools.ietf.org/html/rfc6455) - The WebSocket Protocol
