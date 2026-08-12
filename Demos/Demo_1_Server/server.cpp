#include <iostream>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

int main()
{
    // 初始化Winsock
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    // 创建服务器套接字
    
    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 返回一个套接字句柄，使用稳定流的TCP协议
    if (server_socket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // 设置服务器地址信息
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;   // 监听所有网络接口
    // IP地址被设置为INADDR_ANY，这是一个常量，通常定义为0。它表示监听所有网络接口上的连接。
    server_addr.sin_port = htons(8888);         // 监听端口8888

    // 绑定套接字到本地地址
    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    // 开始监听连接请求
    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on port 8888..." << std::endl;

    // 接受客户端连接
    sockaddr_in client_addr;
    int client_addr_size = sizeof(client_addr);
    SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_size);
    // 从监听队列中取出一个连接请求
    // 为这个连接创建一个全新的服务端socket
    if (client_socket == INVALID_SOCKET)
    {
        std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Client connected!" << std::endl;

    // 通信循环
    char recv_buf[1024];
    int bytes_received;
    while (true)
    {
        // 接收客户端数据
        bytes_received = recv(client_socket, recv_buf, sizeof(recv_buf), 0);
        if (bytes_received > 0)
        {
            std::cout << "Received: " << recv_buf << std::endl;

            // 发送响应消息
            const char* response = "Message received by server";
            send(client_socket, response, (int)strlen(response) + 1, 0);
        }
        else if (bytes_received == 0)
        {
            std::cout << "Client disconnected." << std::endl;
            break;
        }
        else
        {
            std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
            break;
        }
    }

    // 清理资源
    closesocket(client_socket);
    closesocket(server_socket);
    WSACleanup();
    return 0;
}