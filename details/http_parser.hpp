#pragma once

#include <istream>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/case_conv.hpp>

#include "http.hpp"

namespace http { namespace parser
{
    // see RFC2616 5.1 Request-Line (http://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html#sec5.1)
    inline bool parseRequestLine(std::istream& stream, Request& request)
    {
        std::string method;
        stream >> method;
        if (stream.eof()) return false;

        if (stream.get() != ' ' || stream.eof()) return false;

        if (method == "GET") request.method = Method::GET;
        else if (method == "POST") request.method = Method::POST;
        else request.method = Method::Unsupported;

        stream >> request.requestPath;
        if (stream.eof()) return false;

        if (stream.get() != ' ' || stream.eof()) return false;

        std::string version;
        std::getline(stream, version);
        if (stream.eof()) return false;

        if (version.empty()) return false;
        version.pop_back(); // remove CR
        if (version == "HTTP/1.1") request.httpVersion = Version::v1_1;
        else if (version == "HTTP/1.0") request.httpVersion = Version::v1_0;
        else request.httpVersion = Version::Unsupported;

        return true;
    }

    /// eat *( ' ' | '\t' )
    inline void eatWhitespace(const char*& iter)
    {
        while (*iter == ' ' || *iter == '\t')
            ++iter;
    }

    inline bool isSeparator(char c)
    {
        return std::strchr("()<>@,;:\\\"/[]?={} \t", c) != nullptr;
    }

    inline bool isControl(char c)
    {
        return c <= 31 || c >= 127;
    }

    inline bool parseToken(const char*& iter, std::string& token)
    {
        auto start = iter;
        while (!isControl(*iter) && !isSeparator(*iter))
            ++iter;

        token.assign(start, iter);
        boost::to_lower(token);
        return !token.empty();
    }

    inline bool parseProduct(const char*& iter, Product& product)
    {
        if (!parseToken(iter, product.name))
            return false;

        product.version.clear();
        if (*iter == '/')
        {
            ++iter;
            if (!parseToken(iter, product.version))
                return false;
        }
            
        return true;
    }

    // parse 1#element
    template<class Container, typename ElementParser>
    inline bool parseList(const char*& iter, Container& container, ElementParser&& parseElem)
    {
        // do not clear container here

        for (;;)
        {
            eatWhitespace(iter);

            typename Container::value_type elem{};
            if (!parseElem(iter, elem))
                return false;

            container.push_back(std::move(elem));

            eatWhitespace(iter);

            if (*iter != ',')
                return true;

            ++iter;
        }
    }

    inline bool parseBase64Raw(const char*& iter, std::string& base64Raw)
    {
        auto isBase64Char = [](char c)
        {
            return
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '+' || c == '/';
        };

        auto start = iter;
        while (isBase64Char(*iter))
            ++iter;

        base64Raw.assign(start, iter);
        while (*iter == '=') { ++iter; base64Raw.push_back('='); }
        return !base64Raw.empty();
    }

    // see RFC2616 4.2 Message Headers (http://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2)
    inline bool parseRequestHeaders(std::istream& stream, Request& request)
    {
        request.upgrade.clear();
        request.connection.clear();

        std::string headerLine;
        while (std::getline(stream, headerLine))
        {
            if (headerLine.empty() || headerLine.back() != '\r')
                return false;

            headerLine.pop_back();

            if (headerLine.empty())
                return true; // end of headers

            if (headerLine[0] == ' ' || headerLine[0] == '\t')
                continue; // a header field extends to next line

            auto iter = headerLine.c_str();

            auto fieldName = [&](const char* prefix)
            {
                if (!boost::istarts_with(headerLine, prefix))
                    return false;

                iter += strlen(prefix);
                return true;
            };
            
            if (fieldName("upgrade:")) // 1#product
            {
                if (!parseList(iter, request.upgrade, parseProduct))
                    return false;
            }
            else if (fieldName("connection:")) // 1#token
            {
                if (!parseList(iter, request.connection, parseToken))
                    return false;
            }
            else if (fieldName("sec-websocket-version:")) // 0-255
            {
                eatWhitespace(iter);
                request.secWebSocketVersion = strtol(iter, (char**)&iter, 10);
            }
            else if (fieldName("sec-websocket-key:")) // 1#token
            {
                eatWhitespace(iter);
                if (!parseBase64Raw(iter, request.secWebSocketKey))
                    return false;
            }
            else
            {
                iter = "";
            }

            eatWhitespace(iter);
            if (*iter)
                return false;
        }
        return false;
    }
}}