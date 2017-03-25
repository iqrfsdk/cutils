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

#include "UdpChannel.h"
#include "IqrfLogging.h"

#include "PlatformDep.h"
#include <stdlib.h>     //srand, rand
#include <time.h>       //time
#include <string.h>

#ifndef WIN
#define WSAGetLastError() errno
#define SOCKET_ERROR -1
int closesocket (int filedes) { close(filedes); }
typedef int opttype;
#else
#define SHUT_RD SD_RECEIVE
typedef char opttype;
#endif

UdpChannel::UdpChannel(unsigned short remotePort, unsigned short localPort, unsigned bufsize)
  :m_runListenThread(true),
  m_remotePort(remotePort),
  m_localPort(localPort),
  m_bufsize(bufsize)
{
  m_isListening = false;
  //m_rx = ant_new unsigned char[m_bufsize];
  //memset(m_rx, 0, m_bufsize);

#ifdef WIN
  // Initialize Winsock
  WSADATA wsaData = { 0 };
  int iResult = 0;

  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    THROW_EX(UdpChannelException, "WSAStartup failed: " << GetLastError());
  }
#endif

  //try to get local IP by trm and rec to itself
  getMyIpAddress();

  //iqrfUdpSocket = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  m_iqrfUdpSocket = socket(AF_INET, SOCK_DGRAM, 0);
  if (m_iqrfUdpSocket == -1)
    THROW_EX(UdpChannelException, "socket failed: " << GetLastError());

  opttype broadcastEnable = 1;                                // Enable sending broadcast packets
  if (0 != setsockopt(m_iqrfUdpSocket, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)))
  {
    closesocket(m_iqrfUdpSocket);
    THROW_EX(UdpChannelException, "setsockopt failed: " << GetLastError());
  }

  // Remote server, packets are send as a broadcast until the first packet is received
  memset(&m_iqrfUdpTalker, 0, sizeof(m_iqrfUdpTalker));
  m_iqrfUdpTalker.sin_family = AF_INET;
  m_iqrfUdpTalker.sin_port = htons(m_remotePort);
  m_iqrfUdpTalker.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  // Local server, packets are received from any IP
  memset(&m_iqrfUdpListener, 0, sizeof(m_iqrfUdpListener));
  m_iqrfUdpListener.sin_family = AF_INET;
  m_iqrfUdpListener.sin_port = htons(m_localPort);
  m_iqrfUdpListener.sin_addr.s_addr = htonl(INADDR_ANY);

  if (SOCKET_ERROR == bind(m_iqrfUdpSocket, (struct sockaddr *)&m_iqrfUdpListener, sizeof(m_iqrfUdpListener)))
  {
    closesocket(m_iqrfUdpSocket);
    THROW_EX(UdpChannelException, "bind failed: " << GetLastError());
  }

  m_rx = ant_new unsigned char[m_bufsize];
  memset(m_rx, 0, m_bufsize);

  m_listenThread = std::thread(&UdpChannel::listen, this);
}

UdpChannel::~UdpChannel()
{
  shutdown(m_iqrfUdpSocket, SHUT_RD);
  closesocket(m_iqrfUdpSocket);

  TRC_DBG("joining udp listening thread");
  if (m_listenThread.joinable())
    m_listenThread.join();
  TRC_DBG("listening thread joined");

#ifdef WIN
  WSACleanup();
#endif

  delete[] m_rx;
}

void UdpChannel::listen()
{
  TRC_ENTER("thread starts");

  int recn = -1;
  socklen_t iqrfUdpListenerLength = sizeof(m_iqrfUdpListener);

  try {
    m_isListening = true;
    while (m_runListenThread)
    {
      recn = recvfrom(m_iqrfUdpSocket, (char*)m_rx, m_bufsize, 0, (struct sockaddr *)&m_iqrfUdpListener, &iqrfUdpListenerLength);

      if (recn == SOCKET_ERROR) {
        THROW_EX(UdpChannelException, "recvfrom returned: " << WSAGetLastError());
      }

      if (recn > 0 ) {
        if (m_receiveFromFunc) {
          std::basic_string<unsigned char> message(m_rx, recn);
          if (0 == m_receiveFromFunc(message)) {
            m_iqrfUdpTalker.sin_addr.s_addr = m_iqrfUdpListener.sin_addr.s_addr;    // Change the destination to the address of the last received packet
          }
        }
        else {
          TRC_WAR("Unregistered receiveFrom() handler");
        }
      }
    }
  }
  catch (UdpChannelException& e) {
    CATCH_EX("listening thread finished", UdpChannelException, e);
    m_runListenThread = false;
  }
  m_isListening = false;
  TRC_LEAVE("thread stopped");
}

void UdpChannel::sendTo(const std::basic_string<unsigned char>& message)
{
  //TRC_DBG("Send to UDP: " << std::endl << FORM_HEX(message.data(), message.size()));

  int trmn = sendto(m_iqrfUdpSocket, (const char*)message.data(), message.size(), 0, (struct sockaddr *)&m_iqrfUdpTalker, sizeof(m_iqrfUdpTalker));

  if (trmn < 0) {
    THROW_EX(UdpChannelException, "sendto failed: " << WSAGetLastError());
  }

}

void UdpChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
}

void UdpChannel::unregisterReceiveFromHandler()
{
  m_receiveFromFunc = ReceiveFromFunc();
}

void UdpChannel::getMyIpAddress()
{
  TRC_ENTER("");
  const int ATTEMPTS_CNT = 3;
  int attempts;

  //initialize random seed
  srand(time(NULL));

  //generate secret number
  int short secret = (int short)rand();

  std::basic_string<unsigned char> msgTrm = { 0x66, 0x64, 0, 0 };

  msgTrm[2] = (unsigned char)((secret >> 8) & 0xFF);
  msgTrm[3] = (unsigned char)(secret & 0xFF);

  sockaddr_in iqrfUdpMyself;
  socklen_t iqrfUdpMyselfLength = sizeof(iqrfUdpMyself);
  socklen_t iqrfUdpListenerLength = sizeof(m_iqrfUdpListener);

  memset(&iqrfUdpMyself, 0, sizeof(iqrfUdpMyself));
  iqrfUdpMyself.sin_family = AF_INET;
  iqrfUdpMyself.sin_port = htons(m_localPort);
  iqrfUdpMyself.sin_addr.s_addr = htonl(INADDR_BROADCAST);

  // Local server, packets are received from any IP
  sockaddr_in iqrfUdpListener;
  memset(&iqrfUdpListener, 0, sizeof(iqrfUdpListener));
  iqrfUdpListener.sin_family = AF_INET;
  iqrfUdpListener.sin_port = htons(m_localPort);
  iqrfUdpListener.sin_addr.s_addr = htonl(INADDR_ANY);

  SOCKET soc;
  soc = socket(AF_INET, SOCK_DGRAM, 0);
  if (soc == -1)
    THROW_EX(UdpChannelException, "socket failed: " << GetLastError());

  opttype broadcastEnable = 1;                                // Enable sending broadcast packets
  if (0 != setsockopt(soc, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)))
  {
    closesocket(soc);
    THROW_EX(UdpChannelException, "setsockopt failed: " << GetLastError());
  }

  if (SOCKET_ERROR == bind(soc, (struct sockaddr *)&iqrfUdpListener, sizeof(iqrfUdpListener)))
  {
    closesocket(soc);
    THROW_EX(UdpChannelException, "bind failed: " << GetLastError());
  }

  for (attempts = ATTEMPTS_CNT; attempts > 0; attempts--) {
    int recn = -1;

    TRC_DBG("Send to UDP to myself: " << PAR(attempts) << std::endl << FORM_HEX(msgTrm.data(), msgTrm.size()));
    int trmn = sendto(soc, (const char*)msgTrm.data(), msgTrm.size(), 0, (struct sockaddr *)&iqrfUdpMyself, sizeof(iqrfUdpMyself));
    if (trmn < 0) {
      THROW_EX(UdpChannelException, "sendto failed: " << WSAGetLastError());
    }

    unsigned char rx[16];
    memset(rx, 0, 16);

    recn = recvfrom(soc, (char*)rx, 16, 0, (struct sockaddr *)&iqrfUdpListener, &iqrfUdpListenerLength);
    if (recn == SOCKET_ERROR) {
      THROW_EX(UdpChannelException, "recvfrom failed: " << WSAGetLastError());
    }

    if (recn > 0) {
      TRC_DBG("Received from UDP: " << std::endl << FORM_HEX(rx, recn));
      std::basic_string<unsigned char> msgRec(rx, recn);
      if (msgTrm == msgRec) {
        m_myIpAdress = inet_ntoa(iqrfUdpListener.sin_addr);    // my address is address of the last received packet
        break;
      }
    }
  }

  if (attempts <= 0) {
    THROW_EX(UdpChannelException, "Failed listen myself - cannot specify my IP address.");
  }

  shutdown(soc, SHUT_RD);
  closesocket(soc);

  TRC_LEAVE("");
}
