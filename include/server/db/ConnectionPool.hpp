#pragma once
#include <string>
#include <queue>
#include "Connection.hpp"
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <memory>
#include <atomic>
using namespace std;
/*
实现连接池功能模块
*/
class ConnectionPool
{
public:
	static ConnectionPool* getInstance();
	// 给外部提供接口，从连接池中获取一个可用的空闲连接
	shared_ptr<Connection> getConnection(); 
private:
	string _ip;  // mysql的ip地址
	unsigned short _port;  // mysql的端口号3306
	string _username;  // mysql登录用户名
	string _password;  // mysql登录密码
	string _dbname; // 连接数据库名
	int _initSize;  // 连接池初始连接量
	int _maxSize;// 连接池最大连接量
	int _maxIdleTime;  // 连接池最大空闲时间
	int _connectionTimeout; // 连接池获取连接的超时时间
	queue<Connection*> _connectionQue;  // 存储mysql连接的队列
	mutex _queueMutex; // 维护连接队列的线程安全互斥锁
	atomic_int _connectionCnt;  // 记录所创建的connection连接的总数量 <= maxSize;
	condition_variable cv;  // 设置条件变量，用于连接生产线程和连接消费线程的通信

	ConnectionPool();  // 单例 #1  
	ConnectionPool(const ConnectionPool&) = delete;
	ConnectionPool& operator=(const ConnectionPool&) = delete;
	bool loadConfigFile();  // 从配置文件中加载配置项
	// 运行在独立的线程中，专门负责生产新连接
	void produceConnectionTask();
	// 扫描多余的空闲连接，超过maxIdleTime, 进行多余的连接回收
	void scannerConnectionTask();
};