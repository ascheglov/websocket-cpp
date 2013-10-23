// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <cstdint>

namespace websocket
{
    using ConnectionId = std::uint32_t;
    // 1.36 years at 100 new connections per second

    enum class Event { NewConnection, Message, Disconnect };
}
