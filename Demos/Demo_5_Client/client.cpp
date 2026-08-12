#define MSGPACK_NO_BOOST

#include <enet/enet.h>

#include <string>
#include <iostream>

int main(int argc, char** argv) 
{
    if (enet_initialize() != 0) {
        std::cerr << "An error occurred while initializing ENet.\n";
        return EXIT_FAILURE;
    }
    atexit(enet_deinitialize);

    ENetHost* client;
    client = enet_host_create(NULL, 1, 2, 0, 0);
    if (client == NULL) {
        std::cerr << "An error occurred while trying to create an ENet client host.\n";
        return EXIT_FAILURE;
    }

    ENetAddress address;
    ENetPeer* peer;
    ENetEvent event;

    enet_address_set_host(&address, "localhost");
    address.port = 25565;

    peer = enet_host_connect(client, &address, 2, 0);
    if (peer == NULL) 
    {
        std::cerr << "No available peers for initiating an ENet connection.\n";
        return EXIT_FAILURE;
    }

    if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) 
    {
        std::cout << "Connection to localhost:25565 succeeded.\n";
    }
    else 
    {
        enet_peer_reset(peer);
        std::cerr << "Connection to localhost:25565 failed.\n";
        return EXIT_FAILURE;
    }

    std::string user_input;

    while (true)
    {
        std::cout << "Enter message (type 'exit' to quit): ";
        std::getline(std::cin, user_input);

        if (user_input == "exit")
            break;

        ENetPacket* packet = enet_packet_create(user_input.data(), user_input.size(), ENET_PACKET_FLAG_RELIABLE);
        enet_peer_send(peer, 0, packet);

        while (enet_host_service(client, &event, 0) > 0)
        {
            switch (event.type) {
            case ENET_EVENT_TYPE_RECEIVE:
            {
                std::string response = std::string((char*)event.packet->data, event.packet->dataLength);
                std::cout << "Server response: " << response << std::endl;
                enet_packet_destroy(event.packet);
            }
            break;
            case ENET_EVENT_TYPE_DISCONNECT:
                std::cout << "Disconnected from server.\n";
                return EXIT_SUCCESS;
            }
        }
    }

    enet_peer_disconnect(peer, 0);
    enet_host_destroy(client);
    return EXIT_SUCCESS;
}
