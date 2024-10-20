#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
using namespace std;
// 处理服务器 ctrl+c结束后，重置user的状态信息
void resetHandler(int) 
{
    ChatService::instance()->reset();
    exit(0);
}
int main(int argc, char **argv) {
    if (argc < 3)
    {
        cerr << "command invalid! example : ./ChatServer 127.0.0.1 6000" << endl;
        exit(-1);
    }
    // 解析 ip + port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);
    signal(SIGINT, resetHandler);
    EventLoop loop;
    InetAddress addr(ip, port);
    // InetAddress addr("172.31.140.135", 6000);
    ChatServer server(&loop, addr, "ChatServer");

    server.start();
    loop.loop();
    return 0;
}

