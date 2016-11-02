#pragma once

#include "IChannel.h"
#include <string>
#include <exception>
#include <thread>
#include <mutex>
#include <atomic>

/////////////////
#include <windows.h> 
/////////////////

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
  void connect(bool reconnect);
  std::mutex m_connectMtx;

  HANDLE m_localMqHandle;
  HANDLE m_remoteMqHandle;
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
