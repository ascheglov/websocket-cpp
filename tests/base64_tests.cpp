// tests for base64.hpp
#include "details/base64.hpp"

#include "catch_wrap.hpp"

template<std::size_t N>
static std::string str(const char(&s)[N])
{
    return {s, s + N - 1};
}

TEST_CASE("Base64 encoding", "[base64]")
{
    REQUIRE(b64encode("") == "");

    // 0b111111 pattern
    REQUIRE(b64encode(str("\xfc\x00\x00")) == "/AAA");
    REQUIRE(b64encode(str("\x03\xf0\x00")) == "A/AA");
    REQUIRE(b64encode(str("\x00\x0f\xc0")) == "AA/A");
    REQUIRE(b64encode(str("\x00\x00\x3f")) == "AAA/");

    // 0b100011 pattern
    REQUIRE(b64encode(str("\x8c\x00\x00")) == "jAAA");
    REQUIRE(b64encode(str("\x02\x30\x00")) == "AjAA");
    REQUIRE(b64encode(str("\x00\x08\xc0")) == "AAjA");
    REQUIRE(b64encode(str("\x00\x00\x23")) == "AAAj");

    REQUIRE(b64encode("abcdef") == "YWJjZGVm");

    REQUIRE(b64encode("a") == "YQ==");
    REQUIRE(b64encode("ab") == "YWI=");
}