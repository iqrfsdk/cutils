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
#include "TaskQueue.h"
#include <string.h>
#include <thread>
#include <chrono>

const unsigned SPI_REC_BUFFER_SIZE = 1024;

const spi_iqrf_config_struct IqrfSpiChannel::SPI_IQRF_CFG_DEFAULT = {
  SPI_IQRF_DEFAULT_SPI_DEVICE,
  ENABLE_GPIO,
  CE0_GPIO,
  MISO_GPIO,
  MOSI_GPIO,
  SCLK_GPIO
};

class IqrfSpiChannel::Imp
{
public:
  static const spi_iqrf_config_struct SPI_IQRF_CFG_DEFAULT;
  
  Imp() = delete;
  
  Imp(const spi_iqrf_config_struct& cfg)
    :m_port(cfg.spiDev),
    m_bufsize(SPI_REC_BUFFER_SIZE)
  {
    m_rx = ant_new unsigned char[m_bufsize];
    memset(m_rx, 0, m_bufsize);

    int retval = spi_iqrf_initAdvanced(&cfg);
    if (BASE_TYPES_OPER_OK != retval) {
      delete[] m_rx;
      THROW_EX(SpiChannelException, "Communication interface has not been open.");
    }

    m_receiveMessageQueue = new TaskQueue<std::basic_string<unsigned char>>([&](std::basic_string<unsigned char> msg) {
      // unlocked - possible to write in receiveFromFunc
      if (m_receiveFromFunc) {
        m_receiveFromFunc(msg);
      }
      else {
        TRC_WAR("Unregistered receiveFrom() handler");
      }
    });

    m_runListenThread = true;
    m_listenThread = std::thread(&Imp::listen, this);
  }

  ~Imp()
  {
    m_runListenThread = false;

    TRC_DBG("joining udp listening thread");
    if (m_listenThread.joinable())
      m_listenThread.join();
    TRC_DBG("listening thread joined");

    spi_iqrf_destroy();

    delete[] m_rx;
  }

  void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
  {
    m_receiveFromFunc = receiveFromFunc;
  }

  void unregisterReceiveFromHandler()
  {
    m_receiveFromFunc = ReceiveFromFunc();
  }

  void setCommunicationMode(_spi_iqrf_CommunicationMode mode) const
  {
    spi_iqrf_setCommunicationMode(mode);
    if (mode != spi_iqrf_getCommunicationMode()) {
      THROW_EX(SpiChannelException, "CommunicationMode was not changed.");
    }
  }

  _spi_iqrf_CommunicationMode getCommunicationMode() const
  {
    return spi_iqrf_getCommunicationMode();
  }

  IChannel::State getState()
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

  void sendTo(const std::basic_string<unsigned char>& message)
  {
    const int ATTEMPTS = 8;
    static int counter = 0;
    int attempt = 0;
    counter++;

    TRC_INF("Sending to IQRF SPI: " << std::endl << FORM_HEX(message.data(), message.size()));

    while (attempt++ < ATTEMPTS) {
      TRC_DBG("Trying to sent: " << counter << "." << attempt);
      spi_iqrf_SPIStatus status;

      std::unique_lock<std::mutex> lck(m_commMutex);

      // get status
      int retval = spi_iqrf_getSPIStatus(&status);
      if (BASE_TYPES_OPER_OK == retval) {
        if (status.dataNotReadyStatus == SPI_IQRF_SPI_READY_COMM) {
          int retval = spi_iqrf_write((void*)message.data(), message.size());
          if (BASE_TYPES_OPER_OK == retval) {
            TRC_DBG("Success write: " << NAME_PAR(wrData, message.size()))
            break;
          }
          else {
            TRC_WAR("spi_iqrf_write() failed: " << PAR(retval));
          }
        }
      }
      else {
        TRC_WAR("spi_iqrf_getSPIStatus() failed: " << PAR(retval));
      }

      // conflict with incoming data
      if (status.isDataReady) {
        TRC_WAR("Data ready postpone write: " << PAR_HEX(status.isDataReady) << PAR_HEX(status.dataReady) << PAR(m_runListenThread));
        
        // notify listen() to read immediately
        m_commCondition.notify_one();
         
        // wait for finished read and try write again
        m_commCondition.wait_for(lck, std::chrono::milliseconds(100));
      }

    }
    if (attempt > ATTEMPTS) {
      TRC_WAR("Cannot send to SPI: message is dropped");
    }
  }

private:
  void listen()
  {
    TRC_ENTER("thread starts");

    try {
      TRC_DBG("SPI is ready");

      while (m_runListenThread)
      {
        int recData = 0;
        bool timeout = false;

        { // locked scope
          std::unique_lock<std::mutex> lck(m_commMutex);
          m_commCondition.wait_for(lck, std::chrono::milliseconds(10));
          // locked here when out of wait, doesn't matter if notify or timeout

          spi_iqrf_SPIStatus status;
          int retval = spi_iqrf_getSPIStatus(&status);
          if (BASE_TYPES_OPER_OK == retval) {
            if (status.isDataReady) {
              TRC_DBG("Data is ready: " << NAME_PAR(dataReady, status.dataReady));
              if (status.dataReady <= m_bufsize) {
                retval = spi_iqrf_read(m_rx, status.dataReady);
                if (BASE_TYPES_OPER_OK == retval) {
                  // reading success
                  recData = status.dataReady;
                }
                else {
                  TRC_WAR("spi_iqrf_read() failed: " << PAR(retval));
                }
              }
              else {
                TRC_WAR("Received data too long: " << NAME_PAR(dataReady, status.dataReady) << PAR(m_bufsize));
              }
            }
          }
          else {
            TRC_WAR("spi_iqrf_getSPIStatus() failed: " << PAR(retval));
          }
        }

        // unblock pending write if any
        m_commCondition.notify_one();

        // push received message if any
        if (recData) {
          TRC_DBG("Success read: " << PAR(recData));
          m_receiveMessageQueue->pushToQueue(std::basic_string<unsigned char>(m_rx, recData));
        }

      }
    }
    catch (SpiChannelException& e) {
      CATCH_EX("listening thread error", SpiChannelException, e);
      m_runListenThread = false;
    }
    TRC_WAR("thread stopped");
  }

  ReceiveFromFunc m_receiveFromFunc;

  std::atomic_bool m_runListenThread;
  std::thread m_listenThread;

  std::string m_port;

  unsigned char* m_rx;
  unsigned m_bufsize;

  std::mutex m_commMutex;
  std::condition_variable m_commCondition;

  TaskQueue<std::basic_string<unsigned char>>* m_receiveMessageQueue = nullptr;

};

//////////////////////////////////////
IqrfSpiChannel::IqrfSpiChannel(const spi_iqrf_config_struct& cfg)
{
  m_imp = ant_new Imp(cfg);
}

IqrfSpiChannel::~IqrfSpiChannel()
{
  delete m_imp;
}

void IqrfSpiChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_imp->registerReceiveFromHandler(receiveFromFunc);
}

void IqrfSpiChannel::unregisterReceiveFromHandler()
{
  m_imp->unregisterReceiveFromHandler();
}

void IqrfSpiChannel::setCommunicationMode(_spi_iqrf_CommunicationMode mode) const
{
  m_imp->setCommunicationMode(mode);
}

_spi_iqrf_CommunicationMode IqrfSpiChannel::getCommunicationMode() const
{
  return m_imp->getCommunicationMode();
}

void IqrfSpiChannel::sendTo(const std::basic_string<unsigned char>& message)
{
  m_imp->sendTo(message);
}

IChannel::State IqrfSpiChannel::getState()
{
  return m_imp->getState();
}
