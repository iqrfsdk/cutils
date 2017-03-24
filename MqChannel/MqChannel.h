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
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
typedef mqd_t MQDESCR;
#endif

typedef std::basic_string<unsigned char> ustring;

class MqChannel: public IChannel
{
public:
  MqChannel(const std::string& remoteMqName, const std::string& localMqName, unsigned bufsize, bool server = false);
  virtual ~MqChannel();

  virtual void sendTo(const std::basic_string<unsigned char>& message) override;
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) override;
  virtual void unregisterReceiveFromHandler() override;

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
  bool m_server;
};

class MqChannelException : public std::exception {
public:
  MqChannelException(const std::string& cause)
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

  virtual ~MqChannelException()
  {}

protected:
  std::string m_cause;
};
