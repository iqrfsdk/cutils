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

#include "IqrfCdcChannel.h"
#include "IqrfLogging.h"
#include <thread>
#include <chrono>

IqrfCdcChannel::IqrfCdcChannel(const std::string& portIqrf)
  : m_cdc(portIqrf.c_str())
{
  if (!m_cdc.test()) {
    THROW_EX(CDCImplException, "CDC Test failed");
  }
}

IqrfCdcChannel::~IqrfCdcChannel()
{
}

void IqrfCdcChannel::sendTo(const std::basic_string<unsigned char>& message)
{
  static int counter = 0;
  DSResponse dsResponse = DSResponse::BUSY;
  int attempt = 0;
  counter++;

  TRC_DBG("Sending to IQRF CDC: " << std::endl << FORM_HEX(message.data(), message.size()));

  while (attempt++ < 4) {
    TRC_DBG("Trying to sent: " << counter << "." << attempt);
    dsResponse = m_cdc.sendData(message);
    if (dsResponse != DSResponse::BUSY)
      break;
    //wait for next attempt
    TRC_DBG("Sleep for a while ... ");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (dsResponse != DSResponse::OK) {
    THROW_EX(CDCImplException, "CDC send failed" << PAR(dsResponse));
  }
}

void IqrfCdcChannel::enterProgrammingMode() {
  PTEResponse peResponse = PTEResponse::ERR1;
  static int counter = 0;
  int attempt = 0;
  counter++;
  
  TRC_DBG("Entering CDC programming mode");

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  while (attempt++ < 5) {
    try {
      TRC_DBG("Trying to enter programming mode: " << counter << "." << attempt);
      peResponse = m_cdc.enterProgrammingMode();
    } catch (std::exception& e) {
        peResponse = PTEResponse::ERR1;
        TRC_DBG("Exception: " << e.what());
    }
    if (peResponse == PTEResponse::OK) {
        break;
    }
    TRC_DBG("Sleep for a while ... ");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (peResponse != PTEResponse::OK) {
    THROW_EX(CDCImplException, "CDC programming mode can not be entered " << PAR(static_cast<int>(peResponse)));
  }
  
  // Wait for a while - TR device can accept commands in programming mode after some time
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void IqrfCdcChannel::terminateProgrammingMode() {
  PTEResponse peResponse = PTEResponse::ERR1;
  
  TRC_DBG("Terminating CDC programming mode");
  
  peResponse = m_cdc.terminateProgrammingMode();

  if (peResponse != PTEResponse::OK) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      peResponse = m_cdc.terminateProgrammingMode();
  }
  
  if (peResponse != PTEResponse::OK) {
    THROW_EX(CDCImplException, "CDC programming mode can not be terminated " << PAR(static_cast<int>(peResponse)));
  }
}

void IqrfCdcChannel::upload(unsigned char target, const std::basic_string<unsigned char>& message) {
  static int counter = 0;
  PMResponse pmResponse = PMResponse::BUSY;
  int attempt = 0;
  counter++;

  TRC_DBG("Uploading data to IQRF CDC target" << PAR_HEX(target) << ": " << std::endl << FORM_HEX(message.data(), message.size()));

  while (attempt++ < 100) {
    TRC_DBG("Trying to upload: " << counter << "." << attempt);
    pmResponse = m_cdc.upload(target, message);
    if (pmResponse != PMResponse::BUSY)
      break;
    //wait for next attempt
    TRC_DBG("Sleep for a while ... ");
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  if (pmResponse != PMResponse::OK) {
    THROW_EX(CDCImplException, "CDC upload failed" << PAR(static_cast<int>(pmResponse)));
  }
}

void IqrfCdcChannel::download(unsigned char target, const std::basic_string<unsigned char>& message, std::basic_string<unsigned char>& data) {
  static int counter = 0;
  PMResponse pmResponse = PMResponse::BUSY;
  int attempt = 0;
  counter++;

  TRC_DBG("Downloading data from IQRF CDC target" << PAR_HEX(target) << ": " << std::endl << FORM_HEX(message.data(), message.size()));

  while (attempt++ < 100) {
    TRC_DBG("Trying to download: " << counter << "." << attempt);
    pmResponse = m_cdc.download(target, message, data);
    if (pmResponse != PMResponse::BUSY)
      break;
    //wait for next attempt
    TRC_DBG("Sleep for a while ... ");
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  if (pmResponse != PMResponse::OK) {
    THROW_EX(CDCImplException, "CDC download failed" << PAR(static_cast<int>(pmResponse)));
  }
}

void* IqrfCdcChannel::getTRModuleInfo() {
    return m_cdc.getTRModuleInfo();
}

void IqrfCdcChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
  m_cdc.registerAsyncMsgListener([&](unsigned char* data, unsigned int length) {
    m_receiveFromFunc(std::basic_string<unsigned char>(data, length)); });
}

void IqrfCdcChannel::unregisterReceiveFromHandler()
{
  m_receiveFromFunc = ReceiveFromFunc();
  m_cdc.unregisterAsyncMsgListener();
}

IChannel::State IqrfCdcChannel::getState()
{
  return State::Ready;
}
