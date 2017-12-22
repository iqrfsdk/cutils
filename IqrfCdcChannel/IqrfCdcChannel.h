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

#include "IChannel.h"
#include "CdcInterface.h"
#include "CDCImpl.h"

class IqrfCdcChannel : public IChannel
{
public:
  IqrfCdcChannel(const std::string& portIqrf);
  virtual ~IqrfCdcChannel();
  virtual void sendTo(const std::basic_string<unsigned char>& message) override;
  virtual void enterProgrammingMode() override;
  virtual void terminateProgrammingMode() override;
  virtual void upload(unsigned char target, const std::basic_string<unsigned char>& message) override;
  virtual void download(unsigned char target, const std::basic_string<unsigned char>& message, std::basic_string<unsigned char>& data) override;
  virtual void* getTRModuleInfo() override;
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) override;
  virtual void unregisterReceiveFromHandler() override;
  State getState() override;

private:
  IqrfCdcChannel();
  CDCImpl m_cdc;
  ReceiveFromFunc m_receiveFromFunc;

};
