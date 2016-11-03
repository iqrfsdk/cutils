#pragma once

#include "PlatformDep.h"

#include "IChannel.h"
#include <string>
#include <exception>
#include <thread>
#include <mutex>
#include <atomic>

#ifdef WIN
#include <windows.h>
typedef HANDLE MQDESCR;
#else
//#include <fcntl.h>
//#include <sys/stat.h>
#include <mqueue.h>
typedef mqd_t MQDESCR;
#endif

typedef std::basic_string<unsigned char> ustring;

class MqChannel: public IChannel
{
public:
  MqChannel(const std::string& remoteMqName, const std::string& localMqName, unsigned bufsize);
  virtual ~MqChannel();

  virtual void sendTo(const std::basic_string<unsigned char>& message);
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc);

private:
  MqChannel();
  ReceiveFromFunc m_receiveFromFunc;

  std::atomic_bool m_connected;
  bool m_runListenThread;
  std::thread m_listenThread;
  void listen();
  void connect();
  std::mutex m_connectMtx;

  MQDESCR m_localMqHandle;
  MQDESCR m_remoteMqHandle;
  std::string m_localMqName;
  std::string m_remoteMqName;

  unsigned char* m_rx;
  unsigned m_bufsize;
};

class MqChannelException : public std::exception {
public:
  MqChannelException(const std::string& cause)
    :m_cause(cause)
  {}

  //TODO ?
#ifndef WIN32
  virtual const char* what() const noexcept(true)
#else
  virtual const char* what() const
#endif
  {
    return m_cause.c_str();
  }

  virtual ~MqChannelException()
  {}

protected:
  std::string m_cause;
};
