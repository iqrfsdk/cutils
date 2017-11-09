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

#include "IqrfSpiChannel.h"
#include "IqrfLogging.h"
#include "PlatformDep.h"
#include <string.h>
#include <thread>
#include <chrono>

const unsigned SPI_REC_BUFFER_SIZE = 1024;

IqrfSpiChannel::IqrfSpiChannel(const std::string& portIqrf)
  :m_port(portIqrf),
  m_bufsize(SPI_REC_BUFFER_SIZE)
{
  m_rx = ant_new unsigned char[m_bufsize];
  memset(m_rx, 0, m_bufsize);

  int retval = spi_iqrf_init(portIqrf.c_str());
  if (BASE_TYPES_OPER_OK != retval) {
    delete[] m_rx;
    THROW_EX(SpiChannelException, "Communication interface has not been open.");
  }

  m_runListenThread = true;
  m_listenThread = std::thread(&IqrfSpiChannel::listen, this);
}

IqrfSpiChannel::~IqrfSpiChannel()
{
  m_runListenThread = false;

  TRC_DBG("joining udp listening thread");
  if (m_listenThread.joinable())
    m_listenThread.join();
  TRC_DBG("listening thread joined");

  spi_iqrf_destroy();

  delete[] m_rx;
}

void IqrfSpiChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
}

void IqrfSpiChannel::unregisterReceiveFromHandler()
{
  m_receiveFromFunc = ReceiveFromFunc();
}

void IqrfSpiChannel::setCommunicationMode(_spi_iqrf_CommunicationMode mode) const
{
  spi_iqrf_setCommunicationMode(mode);
  if ( mode != spi_iqrf_getCommunicationMode()) {
    THROW_EX(SpiChannelException, "CommunicationMode was not changed.");
  }
}

_spi_iqrf_CommunicationMode IqrfSpiChannel::getCommunicationMode() const
{
  return spi_iqrf_getCommunicationMode();
}

void IqrfSpiChannel::listen()
{
  TRC_ENTER("thread starts");

  try {
    TRC_DBG("SPI is ready");

    while (m_runListenThread)
    {
      int recData = 0;

      // lock scope
      {
    	  std::lock_guard<std::mutex> lck(m_commMutex);

        // get status
        spi_iqrf_SPIStatus status;
        int retval = spi_iqrf_getSPIStatus(&status);
        if (BASE_TYPES_OPER_OK != retval) {
          THROW_EX(SpiChannelException, "spi_iqrf_getSPIStatus() failed: " << PAR(retval));
        }

        if (status.isDataReady) {

          if (status.dataReady > m_bufsize) {
            THROW_EX(SpiChannelException, "Received data too long: " << NAME_PAR(len, status.dataReady) << PAR(m_bufsize));
          }

          // reading
          int retval = spi_iqrf_read(m_rx, status.dataReady);
          if (BASE_TYPES_OPER_OK != retval) {
            THROW_EX(SpiChannelException, "spi_iqrf_read() failed: " << PAR(retval));
          }
          recData = status.dataReady;
        }
      }

      // unlocked - possible to write in receiveFromFunc
      if (recData) {
        if (m_receiveFromFunc) {
          std::basic_string<unsigned char> message(m_rx, recData);
          m_receiveFromFunc(message);
        }
        else {
          TRC_WAR("Unregistered receiveFrom() handler");
        }
      }

      // checking every 10ms
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  catch (SpiChannelException& e) {
    CATCH_EX("listening thread error", SpiChannelException, e);
    m_runListenThread = false;
  }
  TRC_WAR("thread stopped");
}

void IqrfSpiChannel::sendTo(const std::basic_string<unsigned char>& message)
{
  static int counter = 0;
  int attempt = 0;
  counter++;

  TRC_INF("Sending to IQRF SPI: " << std::endl << FORM_HEX(message.data(), message.size()));

  while (attempt++ < 4) {
    TRC_INF("Trying to sent: " << counter << "." << attempt);
    
    // lock scope
    {
      std::lock_guard<std::mutex> lck(m_commMutex);

      // get status
      spi_iqrf_SPIStatus status;
      int retval = spi_iqrf_getSPIStatus(&status);
      if (BASE_TYPES_OPER_OK != retval) {
        THROW_EX(SpiChannelException, "spi_iqrf_getSPIStatus() failed: " << PAR(retval));
      }

      if (status.dataNotReadyStatus == SPI_IQRF_SPI_READY_COMM) {
        int retval = spi_iqrf_write((void*)message.data(), message.size());
        if (BASE_TYPES_OPER_OK != retval) {
          THROW_EX(SpiChannelException, "spi_iqrf_write()() failed: " << PAR(retval));
        }
        break;
      }
      else {
   	    TRC_INF(PAR_HEX(status.isDataReady) << PAR_HEX(status.dataNotReadyStatus));
      }
    }
    //wait for next attempt
    TRC_DBG("Sleep for a while ... ");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

IChannel::State IqrfSpiChannel::getState()
{
  IChannel::State state = State::NotReady;
  spi_iqrf_SPIStatus spiStatus1, spiStatus2;
  int ret = 1;

  {
    std::lock_guard<std::mutex> lck(m_commMutex);

    ret = spi_iqrf_getSPIStatus(&spiStatus1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ret = spi_iqrf_getSPIStatus(&spiStatus2);
  }

  switch (ret) {
  case BASE_TYPES_OPER_OK:
    if (spiStatus1.dataNotReadyStatus == SPI_IQRF_SPI_READY_COMM && spiStatus2.dataNotReadyStatus == SPI_IQRF_SPI_READY_COMM) {
      state = State::Ready;
    }
    else {
      TRC_INF("SPI status1: " << PAR(spiStatus1.dataNotReadyStatus));
      TRC_INF("SPI status2: " << PAR(spiStatus2.dataNotReadyStatus));
      state = State::NotReady;
    }
    break;

  default:
    state = State::NotReady;
    break;
  }

  return state;
}
