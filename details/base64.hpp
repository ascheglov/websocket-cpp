// Base64 encoding
// http://www.ietf.org/rfc/rfc3548.txt

#pragma once

#include <string>
#if defined _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4127) // conditional expression is constant
#endif
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#if defined _MSC_VER
#pragma warning(pop)
#endif

inline std::string b64encode(const void* data, std::size_t size)
{
    using namespace boost::archive::iterators;
    using iter_t = base64_from_binary<transform_width<const char*, 6, 8>>;
    auto bytes = static_cast<const char*>(data);
    std::string encoded(iter_t{bytes}, iter_t{bytes + size});
    encoded.append("\0\2\1"[size % 3], '='); // TBD: looks too hacky
    return encoded;
}

inline std::string b64encode(const std::string& data)
{
    return b64encode(&data[0], data.size());
}