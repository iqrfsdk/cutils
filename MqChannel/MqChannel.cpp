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

#include "MqChannel.h"
#include "IqrfLogging.h"

#ifndef WIN
#include <string.h>
const int INVALID_HANDLE_VALUE = -1;
#define QUEUE_PERMISSIONS 0644
#define MAX_MESSAGES 32

const std::string MQ_PREFIX("/");

inline MQDESCR openMqRead(const std::string name, unsigned bufsize)
{
  TRC_ENTER(PAR(name) << PAR(bufsize))
  mqd_t desc;

  struct mq_attr req_attr;

  req_attr.mq_flags = 0;
  req_attr.mq_maxmsg = MAX_MESSAGES;
  req_attr.mq_msgsize = bufsize / MAX_MESSAGES;
  req_attr.mq_curmsgs = 0;

  TRC_DBG("required attributes" << PAR(req_attr.mq_maxmsg) << PAR(req_attr.mq_msgsize))
  desc = mq_open(name.c_str(), O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &req_attr);

  if (desc > 0) {

    struct mq_attr act_attr;
    int res = mq_getattr(desc, &act_attr);
    if (res == 0) {
      TRC_DBG("actual attributes: " << PAR(res) << PAR(act_attr.mq_maxmsg) << PAR(act_attr.mq_msgsize))

      if (act_attr.mq_maxmsg != req_attr.mq_maxmsg || act_attr.mq_msgsize != req_attr.mq_msgsize) {
      res = mq_unlink(name.c_str());
      if (res == 0 || errno == ENOENT) {
        desc = mq_open(name.c_str(), O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &req_attr);
        if (desc < 0) {
          TRC_WAR("mq_open() after mq_unlink() failed:" << PAR(name) << PAR(desc))
        }
      }
      else {
        TRC_WAR("mq_unlink() failed:" << PAR(name) << PAR(desc))
      }
    }
    else {
      TRC_WAR("mq_getattr() failed:" << PAR(name) << PAR(desc))
    }

    }
  }
  else {
    TRC_WAR("mq_open() failed:" << PAR(name) << PAR(desc))
  }

  TRC_LEAVE(PAR(desc));
  return desc;
}

inline MQDESCR openMqWrite(const std::string name, unsigned bufsize)
{
  TRC_ENTER(PAR(name))

  struct mq_attr attr;

  attr.mq_flags = 0;
  attr.mq_maxmsg = MAX_MESSAGES;
  attr.mq_msgsize = bufsize / MAX_MESSAGES;
  attr.mq_curmsgs = 0;

  TRC_DBG("explicit attributes" << PAR(attr.mq_maxmsg) << PAR(attr.mq_msgsize))
  mqd_t retval = mq_open(name.c_str(), O_WRONLY);

  if (retval > 0) {
    struct mq_attr nwattr;
    int nwretval = mq_getattr(retval, &nwattr);
    TRC_DBG("set attributes" << PAR(nwretval) << PAR(nwattr.mq_maxmsg) << PAR(nwattr.mq_msgsize))
  }

  TRC_LEAVE(PAR(retval));
  return retval;
}

inline void closeMq(MQDESCR mqDescr)
{
  mq_close(mqDescr);
}

inline bool readMq(MQDESCR mqDescr, unsigned char* rx, unsigned long bufSize, unsigned long& numOfBytes)
{
  bool ret = true;

  ssize_t numBytes = mq_receive(mqDescr, (char*)rx, bufSize, NULL);

  if (numBytes <= 0) {
    ret = false;
    numOfBytes = 0;
  }
  else
	numOfBytes = numBytes;
  return ret;
}

inline bool writeMq(MQDESCR mqDescr, const unsigned char* tx, unsigned long toWrite, unsigned long& written)
{
  TRC_ENTER(PAR(toWrite))

  written = toWrite;
  int res = mq_send(mqDescr, (const char*)tx, toWrite, 0);
  bool retval = res == 0;

  TRC_LEAVE(PAR(retval))
  return retval;
}

#else

const std::string MQ_PREFIX("\\\\.\\pipe\\");

inline MQDESCR openMqRead(const std::string name, unsigned bufsize)
{
  return CreateNamedPipe(name.c_str(), PIPE_ACCESS_INBOUND,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
    PIPE_UNLIMITED_INSTANCES, bufsize, bufsize, 0, NULL);
}

inline MQDESCR openMqWrite(const std::string name)
{
  return CreateFile(name.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

inline void closeMq(MQDESCR mqDescr)
{
  CloseHandle(mqDescr);
}

inline bool readMq(MQDESCR mqDescr, unsigned char* rx, unsigned long bufSize, unsigned long& numOfBytes)
{
  return ReadFile(mqDescr, rx, bufSize, &numOfBytes, NULL);
}

inline bool writeMq(MQDESCR mqDescr, const unsigned char* tx, unsigned long toWrite, unsigned long& written)
{
  return WriteFile(mqDescr, tx, toWrite, &written, NULL);
}

#endif

MqChannel::MqChannel(const std::string& remoteMqName, const std::string& localMqName, unsigned bufsize, bool server)
  :m_runListenThread(true)
  , m_localMqHandle(INVALID_HANDLE_VALUE)
  , m_remoteMqHandle(INVALID_HANDLE_VALUE)
  , m_localMqName(localMqName)
  , m_remoteMqName(remoteMqName)
  , m_bufsize(bufsize)
  , m_server(server)
{
  TRC_ENTER(PAR(remoteMqName) << PAR(localMqName) << PAR(bufsize));

  m_connected = false;
  m_rx = ant_new unsigned char[m_bufsize];
  memset(m_rx, 0, m_bufsize);

  m_localMqName = MQ_PREFIX + m_localMqName;
  m_remoteMqName = MQ_PREFIX + m_remoteMqName;

  TRC_DBG(PAR(m_localMqName) << PAR(m_remoteMqName));

  m_listenThread = std::thread(&MqChannel::listen, this);
  TRC_LEAVE("");
}

MqChannel::~MqChannel()
{
  TRC_DBG("joining Mq listening thread");
  m_runListenThread = false;
#ifndef WIN
  //seem the only way to stop the thread here
  pthread_cancel(m_listenThread.native_handle());
  closeMq(m_remoteMqHandle);
  closeMq(m_localMqHandle);
#else
  // Open write channel to client just to unblock ConnectNamedPipe() if listener waits there
  MQDESCR mqHandle = openMqWrite(m_localMqName);
  closeMq(m_remoteMqHandle);
  closeMq(m_localMqHandle);
#endif

  if (m_listenThread.joinable())
    m_listenThread.join();
  TRC_DBG("listening thread joined");

  delete[] m_rx;
}

void MqChannel::listen()
{
  TRC_ENTER("thread starts");

  try {
    while (m_runListenThread) {

      unsigned long cbBytesRead = 0;
      bool fSuccess(false);

      m_localMqHandle = openMqRead(m_localMqName, m_bufsize);
      if (m_localMqHandle == INVALID_HANDLE_VALUE) {
        THROW_EX(MqChannelException, "openMqRead() failed: " << NAME_PAR(GetLastError, GetLastError()));
      }
      TRC_DBG("openMqRead() opened: " << PAR(m_localMqName));

#ifdef WIN
      // Wait to connect from cient
      fSuccess = ConnectNamedPipe(m_localMqHandle, NULL);
      if (!fSuccess) {
        THROW_EX(MqChannelException, "ConnectNamedPipe() failed: " << NAME_PAR(GetLastError, GetLastError()));
      }
      TRC_DBG("ConnectNamedPipe() connected: " << PAR(m_localMqName));
#endif

      // Loop for reading
      while (m_runListenThread) {
    	m_state = State::Ready;
        cbBytesRead = 0;
        fSuccess = readMq(m_localMqHandle, m_rx, m_bufsize, cbBytesRead);
        if (!fSuccess || cbBytesRead == 0) {
          if (m_server) { // listen again
            closeMq(m_localMqHandle);
            m_connected = false; // connect again
            TRC_INF("readMq() failed: " << NAME_PAR(GetLastError, GetLastError()));
            break;
          }
          else {
            std::string brokenMsg("Remote broken");
            sendTo(ustring((const unsigned char*)brokenMsg.data(), brokenMsg.size()));
            THROW_EX(MqChannelException, "readMq() failed: " << NAME_PAR(GetLastError, GetLastError()));
          }
        }

        if (m_receiveFromFunc) {
          std::basic_string<unsigned char> message(m_rx, cbBytesRead);
          m_receiveFromFunc(message);
        }
        else {
          TRC_WAR("Unregistered receiveFrom() handler");
        }
      }
    }
  }
  catch (MqChannelException& e) {
    CATCH_EX("listening thread finished", MqChannelException, e);
    m_runListenThread = false;
  }
  catch (std::exception& e) {
    CATCH_EX("listening thread finished", std::exception, e);
    m_runListenThread = false;
  }
  m_state = State::NotReady;
  TRC_LEAVE("thread stopped");
}

void MqChannel::connect()
{
  if (!m_connected) {

    std::lock_guard<std::mutex> lck(m_connectMtx);

    closeMq(m_remoteMqHandle);

    // Open write channel to client
    m_remoteMqHandle = openMqWrite(m_remoteMqName, m_bufsize);
    if (m_remoteMqHandle == INVALID_HANDLE_VALUE) {
      TRC_WAR("openMqWrite() failed: " << NAME_PAR(GetLastError, GetLastError()));
      //if (GetLastError() != ERROR_PIPE_BUSY)
    }
    else {
      TRC_DBG("openMqWrite() opened: " << PAR(m_remoteMqName));
      m_connected = true;
    }
  }
}

void MqChannel::sendTo(const std::basic_string<unsigned char>& message)
{
  TRC_DBG("Send to MQ: " << std::endl << FORM_HEX(message.data(), message.size()));

  unsigned long toWrite = message.size();
  unsigned long written = 0;
  bool reconnect = false;
  bool fSuccess;

  connect(); //open write channel if not connected yet

  fSuccess = writeMq(m_remoteMqHandle, message.data(), toWrite, written);
  if (!fSuccess || toWrite != written) {
    TRC_WAR("writeMq() failed: " << NAME_PAR(GetLastError, GetLastError()));
    m_connected = false;
  }
}

void MqChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
}

void MqChannel::unregisterReceiveFromHandler()
{
  m_receiveFromFunc = ReceiveFromFunc();
}

IChannel::State MqChannel::getState()
{
  return m_state;
}
