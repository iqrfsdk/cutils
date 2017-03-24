/**
 * Copyright 2016-2017 MICRORISC s.r.o.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "PlatformDep.h"

#ifdef WIN

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int clientlen_t;
#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
typedef int SOCKET;
typedef void * SOCKADDR_STORAGE;
typedef size_t clientlen_t;

#endif

#include "IChannel.h"
#include <stdint.h>
#include <exception>
#include <thread>
#include <vector>
#include <atomic>

class UdpChannel : public IChannel
{
public:
  UdpChannel(unsigned short remotePort, unsigned short localPort, unsigned bufsize);
  virtual ~UdpChannel();
  virtual void sendTo(const std::basic_string<unsigned char>& message) override;
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) override;
  virtual void unregisterReceiveFromHandler() override;

  std::string getListeningIpAdress() { return m_myIpAdress; }
  unsigned short getListeningIpPort() { return m_localPort; }
  bool isListening() { return m_isListening; }

private:
  UdpChannel();
  ReceiveFromFunc m_receiveFromFunc;

  std::atomic_bool m_isListening;
  bool m_runListenThread;
  std::thread m_listenThread;
  void listen();
  void getMyIpAddress();

  SOCKET m_iqrfUdpSocket;
  sockaddr_in m_iqrfUdpListener;
  sockaddr_in m_iqrfUdpTalker;

  unsigned short m_remotePort;
  unsigned short m_localPort;

  unsigned char* m_rx;
  unsigned m_bufsize;

  std::string m_myIpAdress;
};

class UdpChannelException : public std::exception {
public:
  UdpChannelException(const std::string& cause)
    :m_cause(cause)
  {}

//TODO ?
#ifndef WIN
  virtual const char* what() const noexcept(true)
#else
  virtual const char* what() const
#endif
  {
    return m_cause.c_str();
  }

  virtual ~UdpChannelException()
  {}

protected:
  std::string m_cause;
};
