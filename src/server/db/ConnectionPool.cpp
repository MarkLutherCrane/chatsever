#include "ConnectionPool.hpp"
#include "dbLog.hpp"
#include <iostream>
// 线程安全的懒汉单例函数接口
ConnectionPool* ConnectionPool::getInstance()
{
	static ConnectionPool pool;
	return &pool;
}
// 连接池的构造
ConnectionPool::ConnectionPool()
{
	// 加载配置项
	if (!loadConfigFile())
	{
		return;
	}
	// 创建初始数量的连接
	for (int i = 0; i < _initSize; ++i)
	{
		Connection* p = new Connection();
		p->connect(_ip, _port, _username, _password, _dbname);
		p->refreshAliveTime();  // 刷新一下开始空闲时间的起始时间
		_connectionQue.push(p);
		_connectionCnt++;
	}
	// 启动一个新的线程，作为连接的生产者 linux thread => pthread_create
	thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
	produce.detach();
	// 启动一个新的定时线程，扫描多余的空闲连接，超过maxIdleTime, 进行多余的连接回收
	thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
	scanner.detach();
}
// 从配置文件中加载配置项
bool ConnectionPool::loadConfigFile()
{
	FILE* pf = fopen("/home/wh/mysql.cnf", "r");
	if (pf == nullptr)
	{
		LOG("mysql.cnf file is not exist!");
		return false;
	}
	while (!feof(pf))
	{
		char line[1024] = {0};
		fgets(line, 1024, pf);
		string str = line;
		int idx = str.find("=", 0);
		if (idx == -1)  // 无效的配置项
		{
			continue;
		}
		//
		int endidx = str.find("\n", idx);
		string key = str.substr(0, idx);
		string value = str.substr(idx + 1, endidx - idx - 1);
		cout << key << ":" << value << endl;
		if (key == "ip")
		{
			_ip = value;
		}
		else if (key == "port")
		{ 
			_port = atoi(value.c_str());
		}
		else if (key == "username")
		{
			_username = value;
		}
		else if (key == "password")
		{
			_password = value;
		}
		else if (key == "dbname")
		{
			_dbname = value;
		}
		else if (key == "initSize")
		{
			_initSize = atoi(value.c_str());
		}
		else if (key == "maxSize")
		{
			_maxSize = atoi(value.c_str());
		}
		else if (key == "maxIdleTime")
		{
			_maxIdleTime = atoi(value.c_str());
		}
		else if (key == "connectionTimeout")
		{
			_connectionTimeout = atoi(value.c_str());
		}
	}
	return true;
}
// 运行在独立的线程中，专门负责生产新连接
void ConnectionPool::produceConnectionTask()
{
	for (;;)
	{
		unique_lock<mutex> lock(_queueMutex);
		while (!_connectionQue.empty())
		{
			cv.wait(lock);  // 队列不空，此处生产线程进入等待状态
		}
		// 连接数量没有达到上限，继续创建新的连接
		if (_connectionCnt < _maxSize)
		{
			Connection* p = new Connection();
			p->connect(_ip, _port, _username, _password, _dbname);
			p->refreshAliveTime();
			_connectionQue.push(p);
			_connectionCnt++;
		}
		// 通知消费者线程，可以消费连接
		cv.notify_all();
	}
}
// 给外部提供接口，从连接池中获取一个可用的空闲连接
shared_ptr<Connection> ConnectionPool::getConnection()
{
	unique_lock<mutex> lock(_queueMutex);
	while (_connectionQue.empty())
	{
		// 等待归还连接，或者生产者生产连接
		if (cv_status::timeout == cv.wait_for(lock, std::chrono::milliseconds(_connectionTimeout)))
		{
			// 超过这个时间, 如果还是空的
			if (_connectionQue.empty())
			{
				LOG("获取空闲连接超时了...获取连接失败");
				return nullptr;
			}
		}
	}
	/*
	shared_ptr 析构时，会把connection资源直接delete掉，
	相当于调用connection析构函数，就会被close
	这里需要自定义shared_ptr的释放资源方式，把connection直接归还到Que当中
	*/
	shared_ptr<Connection> sp(_connectionQue.front(), 
		[&](Connection* pcon) {
			// 这里是在服务器应用线程中调用的，所以一定要考虑队列的线程安全
			unique_lock<mutex> lock(_queueMutex);
			pcon->refreshAliveTime();
			_connectionQue.push(pcon);
		});
	_connectionQue.pop();
	if (_connectionQue.empty())
	{
		cv.notify_all(); // 谁消费了队列中最后一个connection，谁负责通知生产者生产连接
	}

	return sp;
}
void ConnectionPool::scannerConnectionTask()
{
	for (;;)
	{
		// 通过sleep模拟定时效果
		this_thread::sleep_for(std::chrono::seconds(_maxIdleTime));
		
		// 扫描整个队列，释放多余的连接
		unique_lock<mutex> lock(_queueMutex);
		while (_connectionCnt > _initSize)
		{
			Connection* p = _connectionQue.front();
			if (p->getAliceTime() >= (_maxIdleTime * 1000))
			{
				_connectionQue.pop();
				_connectionCnt--;
				delete p;  // 调用~Connection 释放连接
			}
			else  // 队头的连接没有超过_maxIdleTime, 其他连接肯定没有
			{
				break;
			}
		}
	}
}