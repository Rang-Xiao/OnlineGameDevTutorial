#define MSGPACK_NO_BOOST

#include <msgpack.hpp>

struct Script
{
    int version = 0;
    std::string data;

    MSGPACK_DEFINE(version, data);
};
