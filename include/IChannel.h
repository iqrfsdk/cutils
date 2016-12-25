#pragma once

#include <string>
#include <functional>

class IChannel
{
public:
  // receive data handler
  typedef std::function<int(const std::basic_string<unsigned char>&)> ReceiveFromFunc;
  
  //dtor
  virtual ~IChannel() {};
  
  /**
  Sends a request.

  @param [in]	      message	Data to be sent.

  @return	Result of the data send operation. 0 - Data was sent successfully, negative value means some error
  occurred.
  */
  virtual void sendTo(const std::basic_string<unsigned char>& message) = 0;
  
  /**
  Registers the receive data handler, a functional that is called when a message is received.

  @param [in]	receiveFromFunc	The functional.
  */
  virtual void registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc) = 0;

  /**
  Unregisters data handler. The handler remains empty. All icoming data are silently discarded
  */
  virtual void unregisterReceiveFromHandler() = 0;
};
