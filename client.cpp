#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

int main() {
    // 创建客户端套接字
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    // 准备服务器地址
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");  // 服务器 IP 地址
    serverAddress.sin_port = htons(8899);  // 服务器端口号

    // 连接服务器
    if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::cerr << "Connect failed" << std::endl;
        return 1;
    }

    // 循环发送消息
    std::string message;
    while (true) {
        std::cout << "Enter 'continue' to send to the server (or 'quit' to exit): ";
        std::cin >> message;

        // 发送消息给服务器
        if (send(clientSocket, message.c_str(), message.size(), 0) < 0) {
            std::cerr << "Send failed" << std::endl;
            break;
        }

        if (message == "quit") {
            break;
        }
    }

    // 关闭客户端套接字
    close(clientSocket);

    return 0;
}
