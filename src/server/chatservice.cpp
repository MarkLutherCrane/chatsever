#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include <vector>
#include <map>
#include <iostream>

using namespace std;
using namespace muduo;
ChatService *ChatService::instance()
{
    static ChatService service; // 单例对象，线程安全
    return &service;
}

// 注册消息以及对应的回调操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});

    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFrined, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});

    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {   
        // 设置上报消息的回调  handleRedisSubscribeMessage
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}
// 服务器异常，业务重置方法
void ChatService::reset()
{
    // 把所有online的用户设置成 offline
    _userModel.resetState();
}
// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志， msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {
        // 返回一个默认的处理器，空操作
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgid: " << msgid << " can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理登录业务 ORM 框架 对象关系映射 => 业务层操作操作的都是对象  数据层DAO
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    // id pwd
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账号已经登录，请重新输入新账号";
            conn->send(response.dump());
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id 用户登录成功后，向redis订阅channel(id)
            cout << "redis subscribe id = " << id << endl;
            _redis.subscribe(id);
            // 登录成功， 更新用户状态信息  state offline => online
            user.setState("online");
            _userModel.updateState(user);

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty()) {
                response["offlinemsg"] = vec;
                // // 客户端那里读了发送消息的name, 所以这边要查询以下
                // json tmjs;
                // tmjs = json::parse(string(response["offlinemsg"]));
                // User tmuser = _userModel.query(tmjs["id"].get<int>());
                // response["name"] = tmuser.getName();
                // 读取该用户的离线消息后，把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }

            // 查询好友的登录信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty()) 
            {
                vector<string> vec2;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            // 查询该用户群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group : [{groupid:[xxx, xxx]}, {}]
                vector<string> groupVec;
                for (Group &group : groupuserVec)
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();
                    
                    vector<string> userV;
                    for (GroupUser &user : group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    } 
                    grpjson["users"] = userV;
                    groupVec.push_back(grpjson.dump());
                }

                response["groups"] = groupVec;  
            }

            
            conn->send(response.dump());

        }
    }
    else
    {
        // 该用户不存在 或密码错误 登录失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或者密码错误";
        conn->send(response.dump());
    }

    LOG_INFO << "do login service!!!";
}
// 处理注册业务  name password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    std::cout << "ChatService::reg json" << js.dump() << endl; 
    string name = js["name"];
    string pwd = js["password"];
    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
        // 注册失败
    }
    LOG_INFO << "do reg service!!!";
}


// 初始注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);


    // 更新用户的状态信息
    {
        User user(userid, "", "", "offline");
        _userModel.updateState(user);
    }
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); it++) 
        {
            if (it->second == conn) 
            {
                // 从map表删除用户的连接信息
                user.setId(it->first);
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线， 在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    if (user.getId() != -1) 
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}
// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp) 
{
    int toid = js["toid"].get<int>();
   
    // 标识用户是否在线
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线，转发消息  服务器主动推送消息给toid用户
            it->second->send(js.dump());
            // 走这个说明在同一个主机上
            return;
        }
    }   
    
    cout << "redis one chat toid = " << toid << endl;
    User user = _userModel.query(toid);
    if (user.getState() == "online") 
    {
        cout << "toid online \n";
        _redis.publish(toid, js.dump());
        return ;
    }
    // toid不在线， 存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
    
}

// 添加好友业务 msgid id friendid 
void ChatService::addFrined(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();
    
   
    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    // 存储创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group)) 
    {
        // 存储创建人的信息
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}
// 加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid, groupid, "normal");
}
// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid);

    lock_guard<mutex> lock(_connMutex);
    for (int id : useridVec) 
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end())
        {
            // 转发群消息
            it->second->send(js.dump());
        } 
        else 
        {
            User user = _userModel.query(id);
            if (user.getState() == "online")
            {
                _redis.publish(id, js.dump());
                continue;
            }
            // 存储离线消息
            _offlineMsgModel.insert(id, js.dump());
        }
    }
}


void ChatService::handleRedisSubscribeMessage(int userid, string message)
{
    lock_guard<mutex> lock(_connMutex);
    // 有可能拿到消息后，下线了
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(message);
        return;
    }
    
    // 存储该用户的离线信息 ？？？ 10月19：如果在线发redis, 那你还存jm离线？？/ 懂了
    _offlineMsgModel.insert(userid, message);
}