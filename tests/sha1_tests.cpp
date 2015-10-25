// tests for sha1.hpp
#include "details/sha1.hpp"

#include "third_party/catch/catch.hpp"

static std::string b2a_hex(const void* data, std::size_t size)
{
    std::string a;
    a.resize(size * 2);

    auto bytes = static_cast<const unsigned char*>(data);
    for (auto i = 0u; i != size; ++i)
    {
        const auto table = "0123456789abcdef";
        unsigned b = bytes[i];
        a[2 * i] = table[b >> 4];
        a[2 * i + 1] = table[b & 15];
    }

    return a;
}

static std::string calc_sha1(const std::string& text)
{
    SHA1 sha;
    sha.update(&text[0], text.size());
    uint8_t Message_Digest[20];
    sha.digest(Message_Digest);
    return b2a_hex(Message_Digest, 20);
}

TEST_CASE("sha1 of short messages", "[sha1]")
{
    REQUIRE(calc_sha1("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    REQUIRE(calc_sha1("abcdbcdecdefdefgefghfghighijhi" "jkijkljklmklmnlmnomnopnopq") == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST_CASE("sha1 of a long message", "[sha1]")
{
    SHA1 sha;
    for (auto n = 0; n < 1000000; ++n)
        sha.update("a", 1);

    uint8_t Message_Digest[20];
    sha.digest(Message_Digest);
    REQUIRE(b2a_hex(Message_Digest, 20) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST_CASE("sha1 of a message with length equal to block size", "[sha1]")
{
    SHA1 sha;
    for (auto n = 0; n < 10; ++n)
        sha.update("0123456701234567" "0123456701234567" "0123456701234567" "0123456701234567", 64);

    uint8_t Message_Digest[20];
    sha.digest(Message_Digest);
    REQUIRE(b2a_hex(Message_Digest, 20) == "dea356a2cddd90c7a7ecedc5ebb563934f460452");
}