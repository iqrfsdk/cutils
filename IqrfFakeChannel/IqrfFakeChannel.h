#pragma once

#include "IChannel.h"
#include <iostream>
#include <fstream>
#include <array>

struct TrModel {
    std::array<unsigned int, 32> config;
    unsigned int rfpmg;
    unsigned int rfband;
    std::array<unsigned int, 16> accessPwd;
    std::array<unsigned int, 16> userKey;
    std::array<unsigned int, 0x3FFF> flash;
    std::array<unsigned int, 0x00c0> internalEeprom;
    std::array<unsigned int, 0x7fe0> externalEeprom;
    std::array<unsigned int, 18> special;
};

class IqrfFakeChannel : public IChannel
{
public:
  IqrfFakeChannel(const std::string& fileName);
  virtual ~IqrfFakeChannel();
  virtual void sendTo(const std::basic_string<unsigned char>& message) override;
  virtual void enterProgrammingMode() override;
  virtual void terminateProgrammingMode() override;
  virtual void upload(unsigned char target, const std::basic_string<unsigned char>& message) override;
  virtual void download(unsigned char target, const std::basic_string<unsigned char>& message, std::basic_string<unsigned char>& data) override;
  virtual void* getTRModuleInfo() override;
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) override;
  virtual void unregisterReceiveFromHandler() override;

private:
  IqrfFakeChannel();
  std::ofstream file;
  ReceiveFromFunc m_receiveFromFunc;
  void print(std::string comment, unsigned char target,  const std::basic_string<unsigned char>& message, const std::basic_string<unsigned char>& data);
  void print(std::string comment, unsigned char target, const std::basic_string<unsigned char>& data);
  void print(std::string comment, const std::basic_string<unsigned char>& data);
  void print(std::string comment);
  void getDownloadData(unsigned char target, const std::basic_string<unsigned char>& message,  std::basic_string<unsigned char>& data);
  void putUploadData(unsigned char target, const std::basic_string<unsigned char>& message);
  TrModel model;
  void generateDownloadData(void);
};
