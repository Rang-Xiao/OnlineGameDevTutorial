#include <enet/enet.h>

#include <iostream>

int main(int argc, char** argv)
{
    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet.\n";
        return EXIT_FAILURE;
    }
    atexit(enet_deinitialize);

    ENetAddress address;
    ENetHost* server;

    address.host = ENET_HOST_ANY;
    address.port = 25565;

    server = enet_host_create(&address, 32, 2, 0, 0);
    if (server == NULL) {
        std::cerr << "An error occurred while trying to create an ENet server host.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Server started...\n";

    while (true)
    {
        ENetEvent event;
        while (enet_host_service(server, &event, 0) > 0)
        {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                std::cout << "A new client connected from " << event.peer->address.host << ":" << event.peer->address.port << ".\n";
                event.peer->data = (void*)"Client information";
                break;
            case ENET_EVENT_TYPE_RECEIVE:
            {
                std::cout << "Received: " << (char*)event.packet->data << std::endl;
                enet_packet_destroy(event.packet);

                std::string response = "Message received by server";
                ENetPacket* packet = enet_packet_create(response.data(), response.size(), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(event.peer, 0, packet);
            }
            break;
            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << (char*)event.peer->data << " disconnected.\n";
                event.peer->data = NULL;
                break;
            }
        }
    }

    enet_host_destroy(server);
    return EXIT_SUCCESS;
}
