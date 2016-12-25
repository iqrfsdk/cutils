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
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) override;
  virtual void unregisterReceiveFromHandler() override;

private:
  IqrfCdcChannel();
  CDCImpl m_cdc;
  ReceiveFromFunc m_receiveFromFunc;

};
