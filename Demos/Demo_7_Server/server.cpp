#define MSGPACK_NO_BOOST

#include "script.h"

#include <msgpack.hpp>
#include <enet/enet.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <sstream>

class Server
{
private:
    ENetHost* server = nullptr;
    std::atomic<bool> running{ false };
    ENetPeer* connectedClient = nullptr;

public:
    bool Initialize(enet_uint16 port)
    {
        if (enet_initialize() != 0)
        {
            std::cerr << "Failed to initialize ENet" << std::endl;
            return false;
        }

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = port;

        server = enet_host_create(&address, 32, 2, 0, 0);
        if (!server)
        {
            std::cerr << "Failed to create ENet server" << std::endl;
            return false;
        }

        std::cout << "Server initialized on port " << port << std::endl;
        return true;
    }

    void Run()
    {
        running = true;
        std::thread updateThread(&Server::SendUpdates, this);

        while (running)
        {
            ENetEvent event;
            while (enet_host_service(server, &event, 1000) > 0)
            {
                switch (event.type)
                {
                case ENET_EVENT_TYPE_CONNECT:
                    std::cout << "Client connected" << std::endl;
                    connectedClient = event.peer;
                    break;

                case ENET_EVENT_TYPE_DISCONNECT:
                    std::cout << "Client disconnected" << std::endl;
                    connectedClient = nullptr;
                    break;

                case ENET_EVENT_TYPE_RECEIVE:
                    enet_packet_destroy(event.packet);
                    break;
                }
            }
        }

        updateThread.join();
    }

private:
    void SendUpdates()
    {
        int version = 1;
        const char* colorComponents[] = { "R", "G", "B" };

        while (running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(3));

            if (!connectedClient) continue;

            std::stringstream luaScript;
            std::string color = colorComponents[(version - 1) % 3];

            luaScript << "local size = 100 + 50 * (" << version << " % 3)\n"
                << "local r, g, b = 0, 0, 0\n";

            if (color == "R")
                luaScript << "r = 255\n";
            else if (color == "G")
                luaScript << "g = 255\n";
            else
                luaScript << "b = 255\n";

            luaScript << "print(\"Script v" << version 
                << " - Rectangle size: \" .. size .. \", Color: (\" .. r .. \",\" .. g .. \",\" .. b .. \")\")\n"
                << "return {size = size, r = r, g = g, b = b}";

            Script script;
            script.version = version;
            script.data = luaScript.str();

            std::cout << "Sending script v" << version << " with " << color << "=255" << std::endl;

            msgpack::sbuffer buffer;
            msgpack::pack(buffer, script);

            ENetPacket* packet = enet_packet_create(
                buffer.data(),
                buffer.size(),
                ENET_PACKET_FLAG_RELIABLE
            );

            enet_peer_send(connectedClient, 0, packet);
            version++;
        }
    }

public:
    ~Server()
    {
        running = false;
        if (server)
            enet_host_destroy(server);
        enet_deinitialize();
    }
};

int main()
{
    Server server;
    if (server.Initialize(25565))
    {
        server.Run();
    }
    return 0;
}