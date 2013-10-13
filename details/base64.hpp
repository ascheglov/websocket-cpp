// Base64 encoding
// http://www.ietf.org/rfc/rfc3548.txt

#pragma once

#include <string>

inline std::string b64encode(const void* data, std::size_t size)
{
    std::string encoded;
    encoded.resize(((size + 2) / 3) * 4);

    auto&& table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789" "+/";

    static_assert(sizeof(table) == 64 + 1, "");

    auto fullBlocksCount = size / 3;
    auto bytes = static_cast<const unsigned char*>(data);
    for (auto i = 0u; i != fullBlocksCount; ++i)
    {
        unsigned b0 = bytes[i * 3 + 0];
        unsigned b1 = bytes[i * 3 + 1];
        unsigned b2 = bytes[i * 3 + 2];
        auto idx0 = b0 >> 2;
        auto idx1 = (b0 & 0x03) << 4 | b1 >> 4;
        auto idx2 = (b1 & 0x0F) << 2 | b2 >> 6;
        auto idx3 = b2 & 0x3F;
        encoded[i * 4 + 0] = table[idx0];
        encoded[i * 4 + 1] = table[idx1];
        encoded[i * 4 + 2] = table[idx2];
        encoded[i * 4 + 3] = table[idx3];
    }

    auto tailSize = size - fullBlocksCount * 3;
    auto i = fullBlocksCount;
    if (tailSize == 1)
    {
        unsigned b0 = bytes[i * 3 + 0];
        unsigned b1 = 0;
        //unsigned b2 = 0;
        auto idx0 = b0 >> 2;
        auto idx1 = (b0 & 0x03) << 4 | b1 >> 4;
        encoded[i * 4 + 0] = table[idx0];
        encoded[i * 4 + 1] = table[idx1];
        encoded[i * 4 + 2] = '=';
        encoded[i * 4 + 3] = '=';
    }
    else if (tailSize == 2)
    {
        unsigned b0 = bytes[i * 3 + 0];
        unsigned b1 = bytes[i * 3 + 1];
        unsigned b2 = 0;
        auto idx0 = b0 >> 2;
        auto idx1 = (b0 & 0x03) << 4 | b1 >> 4;
        auto idx2 = (b1 & 0x0F) << 2 | b2 >> 6;
        encoded[i * 4 + 0] = table[idx0];
        encoded[i * 4 + 1] = table[idx1];
        encoded[i * 4 + 2] = table[idx2];
        encoded[i * 4 + 3] = '=';
    }

    return encoded;
}

inline std::string b64encode(const std::string& data)
{
    return b64encode(&data[0], data.size());
}