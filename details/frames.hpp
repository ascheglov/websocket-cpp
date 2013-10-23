// Websocket Server implementation
// Belongs to the public domain

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace websocket
{
    enum class Opcode
    {
        Continuation = 0,
        Text = 1,
        Binary = 2,
        Reserved3, Reserved4, Reserved5, Reserved6, Reserved7,
        Close = 8,
        Ping = 9,
        Pong = 10,
        ReservedB, ReservedC, ReservedD, ReservedE, ReservedF,
    };

    struct ServerFrame
    {
        ServerFrame(Opcode opcode, std::string data)
            : m_data(std::move(data))
        {
            writeOpcode(opcode);
            writeLen();
        }

        std::uint8_t m_header[1 + 1 + 8];
        std::uint8_t m_headerLen;
        std::string m_data;

    private:
        void writeOpcode(Opcode op)
        {
            const auto FinalFragmentFlag = 0x80;
            m_header[0] = FinalFragmentFlag | static_cast<std::uint8_t>(op);
        }

        void writeLen()
        {
            auto n = m_data.size();
            if (n <= 125)
            {
                m_headerLen = 1 + 1;
                m_header[1] = static_cast<std::uint8_t>(n);
            }
            else if (n <= 0xFFFF)
            {
                m_headerLen = 1 + 1 + 2;
                m_header[1] = 126;

                m_header[2] = (n >> 8) & 0xFF;
                m_header[3] = n & 0xFF;
            }
            else if (n <= 0xFFffFFff)
            {
                m_headerLen = 1 + 1 + 8;
                m_header[1] = 127;

                m_header[2] = 0;
                m_header[3] = 0;
                m_header[4] = 0;
                m_header[5] = 0;
                m_header[6] = (n >> 8 * 3) & 0xFF;
                m_header[7] = (n >> 8 * 2) & 0xFF;
                m_header[8] = (n >> 8 * 1) & 0xFF;
                m_header[9] = n & 0xFF;
            }
            else
            {
                throw std::length_error("websocket message is too long");
            }
        }
    };

    class FrameReceiver
    {
    public:
        static const auto MinHeaderLen = 1 + 1 + 4;
        static const auto MaxPayloadLen = 125;
        static const auto BufferSize = MinHeaderLen + MaxPayloadLen;

        FrameReceiver() {}

        void* getBufferTail() { return m_buffer; }
        std::size_t getBufferTailSize() { return BufferSize; }

        std::size_t needReceiveMore(std::size_t bytesWritten) const
        {
            auto available = m_dataLen + bytesWritten;
            if (!isValidFrame(available))
                return 0;

            if (available < 2)
                return 1;

            auto needBytes = frameLen() - available;
            return needBytes > 0 ? std::size_t(needBytes) : 0;
        }

        void addBytes(std::size_t n)
        {
            m_dataLen += n;
        }

        bool isValidFrame() const
        {
            return isValidFrame(m_dataLen);
        }

        bool isValidFrame(std::size_t bytesAvailable) const
        {
            if (bytesAvailable == 0)
                return true;
            
            if (!isFinalFragment())
                return false;

            if (bytesAvailable == 1)
                return true;

            if (!isMasked())
                return false;

            if (payloadLen() > MaxPayloadLen)
                return false;

            return true;
        }

        bool isFinalFragment() const { return (m_buffer[0] & 0x80) != 0; }
        Opcode opcode() const { return static_cast<Opcode>(m_buffer[0] & 0x0F); }
        bool isMasked() const { return (m_buffer[1] & 0x80) != 0; }
        int payloadLen() const { return m_buffer[1] & 0x7f; }
        int payloadStart() const { return MinHeaderLen; }
        int frameLen() const { return payloadStart() + payloadLen(); }
        std::string message() const { return{m_buffer + payloadStart(), payloadLen()}; }

        void unmask()
        {
            auto data = m_buffer + payloadStart();
            auto key = data - 4;

            for (auto i = 0; i != payloadLen(); ++i)
                data[i] ^= key[i % 4];
        }

        void shiftBuffer()
        {
            auto currentFrameLen = frameLen();
            m_dataLen -= currentFrameLen;
            std::memmove(m_buffer, m_buffer + currentFrameLen, m_dataLen);
        }

    private:
        char m_buffer[BufferSize];
        std::size_t m_dataLen{0};
    };
}