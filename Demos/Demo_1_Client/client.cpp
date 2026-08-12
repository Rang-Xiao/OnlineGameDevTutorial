#define _WINSOCK_DEPRECATED_NO_WARNINGS   // 允许使用 inet_addr
#include <winsock2.h>
#include <ws2tcpip.h>                     // inet_pton 等
#include <iostream>
#include <string>
#pragma comment(lib, "ws2_32.lib")

int main()
{
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);  // 替代 inet_addr
    server_addr.sin_port = htons(8888);

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        std::cerr << "Connect failed: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }
    std::cout << "Connected to server!" << std::endl;

    std::string user_input;
    char recv_buf[1024];
    int bytes_received;
    while (true)
    {
        std::cout << "Enter message (type 'exit' to quit): ";
        std::getline(std::cin, user_input);
        if (user_input == "exit")
            break;

        send(client_socket, user_input.c_str(), (int)user_input.size() + 1, 0);

        bytes_received = recv(client_socket, recv_buf, sizeof(recv_buf) - 1, 0);
        if (bytes_received > 0)
        {
            recv_buf[bytes_received] = '\0';   // 确保字符串终止
            std::cout << "Server response: " << recv_buf << std::endl;
        }
        else if (bytes_received == 0)
        {
            std::cout << "Server disconnected." << std::endl;
            break;
        }
        else
        {
            std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
            break;
        }
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}