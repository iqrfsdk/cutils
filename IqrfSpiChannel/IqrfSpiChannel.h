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
#include "spi_iqrf.h"
#include "sysfs_gpio.h"
#include "machines_def.h"

class IqrfSpiChannel : public IChannel
{
public:
  static const spi_iqrf_config_struct SPI_IQRF_CFG_DEFAULT;
  IqrfSpiChannel() = delete;
  IqrfSpiChannel(const spi_iqrf_config_struct& cfg);
  virtual ~IqrfSpiChannel();
  void sendTo(const std::basic_string<unsigned char>& message) override;
  void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) override;
  void unregisterReceiveFromHandler() override;
  State getState() override;

  void setCommunicationMode(_spi_iqrf_CommunicationMode mode) const;
  _spi_iqrf_CommunicationMode getCommunicationMode() const;

private:
  class Imp;
  Imp *m_imp;
};

class SpiChannelException : public std::exception {
public:
  SpiChannelException(const std::string& cause)
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

  virtual ~SpiChannelException()
  {}

protected:
  std::string m_cause;
};
