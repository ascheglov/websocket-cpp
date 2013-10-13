// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <iosfwd>

namespace websocket
{
    using ConnectionId = std::uint32_t;
    // more than one year at 100 new connections per second

    enum class Event { NewConnection, Message, Disconnect };

    class Server
    {
    public:
        Server();
        ~Server();

        void start(const std::string& ip, unsigned short port, std::ostream& log);
        void stop();

        void sendText(ConnectionId connId, std::string message);
        void sendBinary(ConnectionId connId, std::string message);
        
        bool poll(Event& event, ConnectionId& connId, std::string& message);

        void drop(ConnectionId connId);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}