#include "json.hpp"
#include "user.hpp"
#include "group.hpp"
#include "public.hpp"
#include <iostream>
#include <thread>
#include <functional>
#include <chrono>
using namespace std;
using json = nlohmann::json;
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <atomic>


// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;

//控制主菜单页面程序
bool isMainMenuRunning = false;
// 用于读写线程之间的通信
sem_t rwsem;
// 记录登录状态是否成功
atomic_bool g_isLoginSuccess{false};

// 接受线程， 收和发不能相互印象
void readTaskHandler(int clientfd);
// 获取系统时间(聊天信息需要添加时间信息)
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int clientfd);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 聊天客户端实现，main线程用作发送线程，子线程用作接收线程
int main(int agrc, char **argv)
{
    if (agrc < 3)
    {
        cerr << "command invalid example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建clinet端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd == -1)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip + port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client 和 server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd);
        exit(-1);
    }

    // 初始化读写线程用的信号量
    sem_init(&rwsem, 0, 0);


    // 连接服务器成功后，启动接收线程负责接受数据， 该线程应启动一次
    std::thread readTask(readTaskHandler, clientfd); // 底层pthread_create
    readTask.detach();                               // pthread_detach

    // 注销之后，读线程应该关了，要不读线程会 跟主线程抢recv  -》 10月21已解决

    // main线程用于接受用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单，登录，注册，退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice: ";
        int choice = 0;
        cin >> choice; // 整数 后面有个Enter, 缓冲区有Enter， 后面读子串会把enter读走
        cin.get();     // 读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid: ";
            cin >> id;
            cin.get();
            cout << "userpassword: ";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump();
            g_isLoginSuccess = false;

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send login msg error: " << request << endl;
            }
            
            sem_wait(&rwsem);   // 等待信号量，由子线程处理完登录的响应消息后，通知这里

            if (g_isLoginSuccess) 
            {
                isMainMenuRunning = true;
                // 进入聊天主菜单界面
                mainMenu(clientfd);
            }
            
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgid"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();
            cout << "client send json " << request << endl;
            // 发送注册请求
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }
            
            sem_wait(&rwsem);  // 等待信号量，子线程处理完会通知
        }
        break;
        case 3: // quit业务
            close(clientfd);
            sem_destroy(&rwsem);
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }
}

void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "======================friend list======================" << endl;
    if (!g_currentUserFriendList.empty())
    {
        for (User &user : g_currentUserFriendList)
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty())
    {
        for (Group &group : g_currentUserGroupList)
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers())
            {
                cout << user.getId() << " " << user.getName() << " "
                     << user.getState() << " " << user.getRole() << endl;
            }
        }
    }
    cout << "======================================================" << endl;
}

// 处理登录的响应逻辑
void doLoginResponse(json &responsejs) 
{
    if (0 != responsejs["errno"].get<int>()) // 登录失败
    {
        cerr << responsejs["errmsg"] << endl;
        g_isLoginSuccess = false;
    }
    else // 登录成功
    {
        // 记录当前用户的id 和 name
        g_currentUser.setId(responsejs["id"]);
        g_currentUser.setName(responsejs["name"]);

        // 记录当前用户的好友列表信息
        if (responsejs.contains("friends"))
        {
            // 初始化操作
            g_currentUserFriendList.clear();

            vector<string> vec = responsejs["friends"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                User user;
                user.setId(js["id"].get<int>());
                user.setName(js["name"]);
                user.setState(js["state"]);
                g_currentUserFriendList.push_back(user);
            }
        }
        cout << "response js: \n " << responsejs << endl;
        // 记录当前用户的群组列表信息
        if (responsejs.contains("groups")) // 没在server端看到  10月11
        {
            // 初始化
            g_currentUserGroupList.clear();
            vector<string> vec1 = responsejs["groups"];
            for (string &groupStr : vec1)
            {
                json grpjs = json::parse(groupStr);
                Group group;
                group.setId(grpjs["id"].get<int>());
                group.setName(grpjs["groupname"]);
                group.setDesc(grpjs["groupdesc"]);

                vector<string> vec2 = grpjs["users"];
                for (string &userStr : vec2) // 添加每个小组的用户
                {
                    GroupUser user;
                    json js = json::parse(userStr);
                    user.setId(js["id"].get<int>());
                    user.setName(js["name"]);
                    user.setState(js["state"]);
                    user.setRole(js["role"]);

                    group.getUsers().push_back(user);
                }
                g_currentUserGroupList.push_back(group);
            }
        }

        // 显示登录用户的基本信息
        showCurrentUserData();
// ----------------------------------------------------------------------------------
        // 显示当前用户的离线消息 个人聊天信息或者群组消息
        if (responsejs.contains("offlinemsg"))
        {
            vector<string> vec = responsejs["offlinemsg"];
            for (string &str : vec)
            {
                json js = json::parse(str);
                // time + [id] + name + said + xxx

                int msgtype = js["msgid"].get<int>();
                // 一对一聊天
                if (js.contains("msgid") && ONE_CHAT_MSG == msgtype)
                {
                    cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                        << " said: " << js["msg"].get<string>() << endl;
                    continue;
                }
                else if (GROUP_CHAT_MSG == msgtype)
                {
                    cout <<"群消息[" << js["groupid"].get<int>() << "]:" <<  js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                        << " said: " << js["msg"].get<string>() << endl;
                    continue;
                }
            }
        }
        g_isLoginSuccess = true;
    }               
}

// 处理注册的响应逻辑
void doRegResponse(json &responsejs)
{
    if (0 != responsejs["errno"].get<int>()) // 注册失败
    {
        cerr << "name already exist, register error!" << endl;
    }
    else // 注册成功
    {
        cout << " register success, userid is " << responsejs["id"]
                << ", do not forget it!" << endl;
    }
}

// 子线程   接收线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);  // 阻塞， 当前用户loginout， 这个位置也是阻塞的
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(1);
        }
        // 接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgid"].get<int>();
        // 一对一聊天
        if (js.contains("msgid") && ONE_CHAT_MSG == msgtype)
        {
            cout << js["time"] << " [" << js["id"] << "]" << js["name"]
                 << " said: " << js["msg"] << endl;
            continue;
        }

        if (GROUP_CHAT_MSG == msgtype)
        {
            cout <<"群消息[" << js["groupid"] << "]:" <<  js["time"] << " [" << js["id"] << "]" << js["name"]
                 << " said: " << js["msg"] << endl;
            continue;
        }

        if (LOGIN_MSG_ACK == msgtype)
        {
            doLoginResponse(js);  // 处理登录响应的业务逻辑、
            sem_post(&rwsem);  // 告知主线程登录成功失败
            continue;
        }

        if (REG_MSG_ACK == msgtype)
        {
            doRegResponse(js);
            sem_post(&rwsem);
            continue;
        }
    }
}
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
        ptm->tm_year + 1900, (int)ptm->tm_mon + 1, ptm->tm_mday, 
        ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    return string(date);
}
// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令，格式help"},
    {"chat", "一对一聊天，格式chat:friendid:message"},
    {"addfriend", "添加好友，格式addfriend:friendid"},
    {"creategroup", "创建群组，格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组，格式addgroup:groupid"},
    {"groupchat", "群聊，格式groupchat:groupid:message"},
    {"loginout", "注销，格式loginout"}};
// 注册系统支持的客户端命令处理
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},
    {"chat", chat},
    {"addfriend", addfriend},
    {"creategroup", creategroup},
    {"addgroup", addgroup},
    {"groupchat", groupchat},
    {"loginout", loginout}};
void mainMenu(int clientfd)
{
    help();

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024);
        string commandbuf(buffer);
        string command;    // 存储指令
        int idx = commandbuf.find(":");
        if (idx == -1) 
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command);
        if (it == commandHandlerMap.end())
        {
            cerr << "invaild input command!" << endl;
            continue;
        }

        // 调用相应命令的事件处理回调，mainMenu对修改封闭，添加功能不需要修改函数
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // 调用命令处理方法
    }
}

// "help" command handler
void help(int fd, string str) 
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap)
    {
        cout << p.first << " : " << p.second << endl; 
    }
    cout << endl;
}

void addfriend(int clientfd, string str) 
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

void chat(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx)
    {
        cerr << "chat command invalid!" << endl;
        return ;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();
    js["name"] = g_currentUser.getName();
    string buffer = js.dump();
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    
    if (-1 == len)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

// "creategroup" command handler
void creategroup(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "creategroup command invalid" << endl;
        return;
    }

    string groupname = str.substr(0, idx);
    string groupdesc = str.substr(idx + 1, str.size() + 1);

    json js;
    js["msgid"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len) 
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}
// "addgroup" command handler
void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgid"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len) 
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}
// "groupchat" command handler   groupid:message
void groupchat(int clientfd, string str)
{
    int idx = str.find(":");
    if (idx == -1)
    {
        cerr << "groupchat command invalid" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgid"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["groupid"] = groupid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len) 
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}
void loginout(int clientfd, string str)
{
    json js;
    js["msgid"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len) 
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    } 
    else
    {
        isMainMenuRunning = false;
    }
}
