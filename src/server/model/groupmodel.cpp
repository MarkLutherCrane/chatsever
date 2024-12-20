#include "groupmodel.hpp"
#include "Connection.hpp"
#include "ConnectionPool.hpp"
#include <iostream>
using namespace std;
// 创建群组
bool GroupModel::createGroup(Group &group)
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
            group.getName().c_str(), group.getDesc().c_str());
    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp->update(sql))
    {
        // 获取以下插入成功的数据的用户的主键id
        group.setId(mysql_insert_id(sp->getConnection()));
        return true;
    } // creategroup:python:learning the chat project
    return false;
}
// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into groupuser values(%d, %d, '%s')",
            groupid, userid, role.c_str());
    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    cout << "TEST::\n";
    cout << groupid << ": " << userid << ": " << role << endl;
    sp->update(sql);
}
// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    /*
    1. 先根据userid在groupuser表中查询出该用户所属的群组信息
    2. 在根据群组信息，查询属于该群组的所有用户的userid， 并且和user表进行多表联合查询，查出用户的详细信息
    */
    char sql[1024] = {0};
    sprintf(sql, "select a.id, a.groupname, a.groupdesc from allgroup a inner join groupuser b on b.userid = %d and b.groupid = a.id ", userid);
    vector<Group> groupVec;
    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
   

    MYSQL_RES *res = sp->query(sql);
    if (res != nullptr) {
        MYSQL_ROW row;
        while ((row  = mysql_fetch_row(res)) != nullptr) 
        {
            Group group;
            group.setId(atoi(row[0]));
            group.setName(row[1]);
            group.setDesc(row[2]);
            groupVec.push_back(group);
        }
        // 释放资源
        mysql_free_result(res); 
    }
    
    
    // 查询群组用户信息 
    for (Group &group : groupVec) 
    {
        sprintf(sql, "select a.id, a.name, a.state, b.grouprole from user a \
        inner join groupuser b on b.userid = a.id where b.groupid = %d", group.getId());
           
        MYSQL_RES *res = sp->query(sql);
        if (res != nullptr) {
            MYSQL_ROW row;
            while ((row  = mysql_fetch_row(res)) != nullptr) 
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setRole(row[3]);
                group.getUsers().push_back(user);
            }
            // 释放资源
            mysql_free_result(res); 
        }
    }
    return groupVec;
}
// 根据指定的groupid查询群组用户id列表，除了userid,主要用户群聊业务给群组其他成员发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d ", groupid, userid);
    vector<int> idVec;
    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
  
    MYSQL_RES *res = sp->query(sql);
    if (res != nullptr) {
        MYSQL_ROW row;
        while ((row  = mysql_fetch_row(res)) != nullptr) 
        {
            idVec.push_back(atoi(row[0]));
        }
        // 释放资源
        mysql_free_result(res); 
    }
    return idVec;
}