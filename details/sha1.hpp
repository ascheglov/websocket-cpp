// SHA-1
// http://www.ietf.org/rfc/rfc3174.txt

#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <stdexcept>

class SHA1
{
public:
    static const auto DIGEST_SIZE = 20;

    SHA1()
        : m_totalBits(0)
        , m_blockPos(0)
        , m_finalized(false)
    {
        m_hash[0] = 0x67452301;
        m_hash[1] = 0xEFCDAB89;
        m_hash[2] = 0x98BADCFE;
        m_hash[3] = 0x10325476;
        m_hash[4] = 0xC3D2E1F0;
    }

    void update(const void* buffer, std::size_t size)
    {
        if (size == 0)
            return;

        if (m_finalized)
            throw std::logic_error("sha1: update() called after digest()");

        if (!buffer)
            throw std::invalid_argument("sha1: null input buffer");

        const auto message_array = (const std::uint8_t*)buffer;

        for (auto i = 0u; i != size; ++i)
        {
            m_block[m_blockPos++] = message_array[i];

            m_totalBits += 8;
            if (m_totalBits == 0)
                throw std::length_error("sha1: message is too long");

            if (m_blockPos == BLOCK_SIZE)
                processBlock();
        }
    }

    void digest(void* buffer)
    {
        if (!buffer)
            throw std::invalid_argument("sha1: null output buffer");

        if (!m_finalized)
        {
            finalize();

            // clear information about message
            m_block.fill(0);
            m_totalBits = 0;

            m_finalized = true;
        }

        auto Message_Digest = (std::uint8_t*)buffer;
        for (auto i = 0; i != DIGEST_SIZE / 4; ++i)
            writeUInt32BE(Message_Digest + i * 4, m_hash[i]);
    }

private:
    static const auto BLOCK_SIZE = 64;

    void processBlock()
    {
        std::uint32_t W[80];

        for (auto i = 0; i != BLOCK_SIZE / 4; ++i)
            W[i] = readUInt32BE(&m_block[i * 4]);

        for (auto i = 16; i != 80; ++i)
            W[i] = rol32(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);

        auto A = m_hash[0];
        auto B = m_hash[1];
        auto C = m_hash[2];
        auto D = m_hash[3];
        auto E = m_hash[4];

        auto f1 = [&]{ return (B & C) | (~B & D); };
        auto f2 = [&]{ return B ^ C ^ D; };
        auto f3 = [&]{ return (B & C) | (B & D) | (C & D); };
        auto f4 = f2;

        auto calc = [&](int t, std::uint32_t f, std::uint32_t K)
        {
            auto temp = rol32(A, 5) + f + E + W[t] + K;
            E = D;
            D = C;
            C = rol32(B, 30);
            B = A;
            A = temp;
        };

        auto t = 0;
        for (; t != 20; ++t) calc(t, f1(), 0x5A827999u);
        for (; t != 40; ++t) calc(t, f2(), 0x6ED9EBA1u);
        for (; t != 60; ++t) calc(t, f3(), 0x8F1BBCDCu);
        for (; t != 80; ++t) calc(t, f4(), 0xCA62C1D6u);

        m_hash[0] += A;
        m_hash[1] += B;
        m_hash[2] += C;
        m_hash[3] += D;
        m_hash[4] += E;

        m_blockPos = 0;
    }

    void finalize()
    {
        m_block[m_blockPos++] = 0x80;

        auto messageBitsIdx = BLOCK_SIZE - (int)sizeof(m_totalBits);

        if (m_blockPos > messageBitsIdx)
        {
            clearBlockTo(BLOCK_SIZE);
            processBlock();
        }

        clearBlockTo(messageBitsIdx);
        writeUInt64BE(&m_block[messageBitsIdx], m_totalBits);
        processBlock();
    }

    void clearBlockTo(int toIndex)
    {
        while (m_blockPos < toIndex)
            m_block[m_blockPos++] = 0;
    }

    static std::uint32_t readUInt32BE(std::uint8_t* buf)
    {
        return
            buf[0] << 24 |
            buf[1] << 16 |
            buf[2] << 8 |
            buf[3];
    }

    static void writeUInt64BE(std::uint8_t* buf, std::uint64_t num)
    {
        writeUInt32BE(buf, num >> 32);
        writeUInt32BE(buf + 4, std::uint32_t(num));
    }

    static void writeUInt32BE(std::uint8_t* buf, std::uint32_t num)
    {
        buf[0] = std::uint8_t(num >> (8 * 3));
        buf[1] = std::uint8_t(num >> (8 * 2));
        buf[2] = std::uint8_t(num >> (8 * 1));
        buf[3] = std::uint8_t(num);
    }

    static std::uint32_t rol32(std::uint32_t value, int n) { return (value << n) | (value >> (32 - n)); }

    std::array<uint32_t, DIGEST_SIZE / 4> m_hash;
    std::array<std::uint8_t, BLOCK_SIZE> m_block;
    uint64_t m_totalBits;
    int m_blockPos;
    bool m_finalized;
};
