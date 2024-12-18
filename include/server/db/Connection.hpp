#pragma once
#include <string>
#include <ctime>
#include <muduo/base/Logging.h>
#include <mysql/mysql.h>
using namespace std;
/*
实现Mysql数据库的操作
*/ 
class Connection
{
public:
	// 初始化数据库连接
	Connection();
	// 释放数据库连接资源
	~Connection();
	// 连接数据库
	bool connect(string ip, 
		unsigned short port, 
		string user, 
		string password,
		string dbname);
	// 更新操作 insert、delete、update
	bool update(string sql);
	// 查询操作 select
	MYSQL_RES* query(string sql);
	// 刷新一下连接的起始的空闲时间点
	void refreshAliveTime() { _alivetime = clock(); }
	// 返回存活的时间
	clock_t getAliceTime() const { return clock() - _alivetime; } 
    // 获取当前连接
    MYSQL* getConnection() {return _conn;} 
private:
	MYSQL * _conn;
	clock_t _alivetime; // 进入空闲状态后的存活时间
};