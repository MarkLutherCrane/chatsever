#include "friendmodel.hpp"
#include "Connection.hpp"
#include "ConnectionPool.hpp"
// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into friend values('%d', '%d')", userid, friendid);
    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    sp->update(sql);
    
}

// 返回用户好友列表  friendid => user表里的信息
vector<User> FriendModel::query(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid = %d", userid);
    vector<User> vec;
    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    MYSQL_RES *res = sp->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        // 把userid用户的所有离线消息放入vec中返回
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setState(row[2]);
            vec.push_back(user);
        }
        mysql_free_result(res);
    }
    return vec;
}