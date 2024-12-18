#include "usermodel.hpp"
#include "Connection.hpp"
#include "ConnectionPool.hpp"
#include <iostream>
using namespace std;
bool UserModel::insert(User &user)
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    // MySQL mysql;
    // if (mysql.connect())
    // {
    //     if (mysql.update(sql))
    //     {
    //         // 获取以下插入成功的数据的用户的主键id
    //         user.setId(mysql_insert_id(mysql.getConnection()));
    //         return true;
    //     }
    // }
    // return false;

    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();

    if (sp->update(sql))
    {
        // 获取以下插入成功的数据的用户的主键id
        user.setId(mysql_insert_id(sp->getConnection()));
        return true;
    }
    return false;
}

// 根据用户id查询用户信息
User UserModel::query(int id)
{
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    // MySQL mysql;
    // if (mysql.connect())
    // {
    //     MYSQL_RES *res = mysql.query(sql);
    //     if (res != nullptr) {
    //         MYSQL_ROW row = mysql_fetch_row(res);
    //         if (row != nullptr)
    //         {
    //             User user;
    //             user.setId(atoi(row[0]));
    //             user.setName(row[1]);
    //             user.setPwd(row[2]);
    //             user.setState(row[3]);

    //             // 释放资源
    //             mysql_free_result(res);
    //             return user;
    //         }
    //     }
    // }
    // return User();

    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    MYSQL_RES *res = sp->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row != nullptr)
        {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setPwd(row[2]);
            user.setState(row[3]);

            // 释放资源
            mysql_free_result(res);
            return user;
        }
    }
    return User();
}


bool UserModel::updateState(User user)
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    // MySQL mysql;
    // if (mysql.connect())
    // {
    //     if (mysql.update(sql))
    //     {
    //         return true;
    //     }
    // }
    // return false;

    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    if (sp->update(sql))
    {
        return true;
    }
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = 'offline' where state = 'online'");

    ConnectionPool *cp = ConnectionPool::getInstance();
    shared_ptr<Connection> sp = cp->getConnection();
    sp->update(sql);
}

// {"msgid":2,"name":"zhang san","password":"123456"}

//  {"msgid":1,"id":23,"password":"123456"}