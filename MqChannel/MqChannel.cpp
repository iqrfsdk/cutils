#include "MqChannel.h"
#include "IqrfLogging.h"
#include "PlatformDep.h"
#include <climits>
#include <ctime>
#include <ratio>
#include <chrono>

#ifndef WIN
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
const int INVALID_HANDLE_VALUE = -1;
#define QUEUE_PERMISSIONS 0660
#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 256
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10
#endif

#ifdef WIN
MqChannel::MqChannel(const std::string& remoteMqName, const std::string& localMqName, unsigned bufsize)
  :m_runListenThread(true)
  ,m_localMqHandle(INVALID_HANDLE_VALUE)
  ,m_remoteMqHandle(INVALID_HANDLE_VALUE)
  ,m_localMqName(localMqName)
  ,m_remoteMqName(remoteMqName)
  ,m_bufsize(bufsize)
{
  m_connected = false;
  m_rx = ant_new unsigned char[m_bufsize];
  memset(m_rx, 0, m_bufsize);

  m_localMqName = std::string("\\\\.\\pipe\\") + m_localMqName;
  m_remoteMqName = std::string("\\\\.\\pipe\\") + m_remoteMqName;

  // The main loop creates an instance of the named pipe and 
  // then waits for a client to connect to it. When the client 
  // connects, a thread is created to handle communications 
  // with that client, and this loop is free to wait for the
  // next client connect request. It is an infinite loop.

  TRC_DBG("nPipe Server: Main thread awaiting client connection on: " << m_localMqName);
  m_localMqHandle = CreateNamedPipe(
    m_localMqName.c_str(),    // pipe name 
    PIPE_ACCESS_INBOUND,      // read access 
    PIPE_TYPE_MESSAGE |       // message type pipe 
    PIPE_READMODE_MESSAGE |   // message-read mode 
    PIPE_WAIT,                // blocking mode 
    PIPE_UNLIMITED_INSTANCES, // max. instances  
    m_bufsize,                // output buffer size 
    m_bufsize,                // input buffer size 
    0,                        // client time-out 
    NULL);                    // default security attribute 

  if (m_localMqHandle == INVALID_HANDLE_VALUE) {
    THROW_EX(MqChannelException, "CreateNamedPipe failed: " << NAME_PAR(GetLastError, GetLastError()));
  }

  m_listenThread = std::thread(&MqChannel::listen, this);
}

MqChannel::~MqChannel()
{
  // Flush the pipe to allow the client to read the pipe's contents 
  // before disconnecting. Then disconnect the pipe, and close the 
  // handle to this pipe instance. 

  //TODO
  //FlushFileBuffers(hPipe);
  //DisconnectNamedPipe(hPipe);
  //CloseHandle(hPipe);

  TRC_DBG("joining udp listening thread");
  if (m_listenThread.joinable())
    m_listenThread.join();
  TRC_DBG("listening thread joined");

  delete[] m_rx;
}

void MqChannel::listen()
{
  TRC_ENTER("thread starts");

  try {
  // Wait for the client to connect; if it succeeds, 
  // the function returns a nonzero value. If the function
  // returns zero, GetLastError returns ERROR_PIPE_CONNECTED. 
  while (m_runListenThread) {
    
    unsigned long cbBytesRead = 0;
    bool fSuccess(false);

    // Wait to connect form cient
    fSuccess = ConnectNamedPipe(m_localMqHandle, NULL);
    if (!fSuccess) {
      THROW_EX(MqChannelException, "ConnectNamedPipe failed: " << NAME_PAR(GetLastError, GetLastError()));
    }

    // Connected from client open write channel to client
    connect(false);

    // Loop for reading
    while (m_runListenThread) {
      cbBytesRead = 0;
      // Read client requests from the pipe. This simplistic code only allows messages up to BUFSIZE characters in length.
      fSuccess = ReadFile(
        m_localMqHandle, // handle to pipe 
        m_rx,            // buffer to receive data 
        m_bufsize,       // size of buffer 
        &cbBytesRead,    // number of bytes read 
        NULL);           // not overlapped I/O 

      if (!fSuccess || cbBytesRead == 0) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
          THROW_EX(MqChannelException, "Client disconnected: " << NAME_PAR(GetLastError, GetLastError()));
        }
        else {
          THROW_EX(MqChannelException, "ReadFile failed: " << NAME_PAR(GetLastError, GetLastError()));
        }
        break;
      }

      std::basic_string<unsigned char> message(m_rx, cbBytesRead);
      m_receiveFromFunc(message);
    }
  }
  }
  catch (MqChannelException& e) {
    CATCH_EX("listening thread finished", MqChannelException, e);
    m_runListenThread = false;
  }
  //m_isListening = false;
  TRC_LEAVE("thread stopped");
}

void MqChannel::connect(bool force)
{
  std::lock_guard<std::mutex> lck(m_connectMtx);
  if (force) {
    CloseHandle(m_remoteMqHandle);
  }
  if (!m_connected) {
    // Open write channel to client
    m_remoteMqHandle = CreateFile(
      m_remoteMqName.c_str(), // pipe name 
      GENERIC_WRITE,   // write access 
      0,              // no sharing 
      NULL,           // default security attributes
      OPEN_EXISTING,  // opens existing pipe 
      0,              // default attributes 
      NULL);          // no template file 

    if (m_remoteMqHandle == INVALID_HANDLE_VALUE) {
      TRC_WAR("CreateFile failed: " << NAME_PAR(GetLastError, GetLastError()));
      //if (GetLastError() != ERROR_PIPE_BUSY)
    }
    else {
      // The pipe connected; change to message-read mode. 
      //dwMode = PIPE_READMODE_MESSAGE;
      //fSuccess = SetNamedPipeHandleState(
      //  hPipe,    // pipe handle 
      //  &dwMode,  // new pipe mode 
      //  NULL,     // don't set maximum bytes 
      //  NULL);    // don't set maximum time 
      //if (!fSuccess)
      //{
      //  TRC_WAR("SetNamedPipeHandleState failed: " << NAME_PAR(GetLastError, GetLastError()));
      //  return -1;
      //}
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

  // Write the reply to the pipe.
  while (true) {
    bool fSuccess = WriteFile(
      m_remoteMqHandle, // handle to pipe 
      message.data(),   // buffer to write from 
      toWrite,          // number of bytes to write 
      &written,         // number of bytes written 
      NULL);            // not overlapped I/O 

    if (!fSuccess || toWrite != written) {
      //TODO check error if it make sense
      if (!reconnect) {
        connect(true);
        reconnect = true;
      }
      else {
        THROW_EX(MqChannelException, "WriteFile failed: " << NAME_PAR(GetLastError, GetLastError()));
      }
    }
    else
      break;
  }
}

void MqChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
}

#else
///////////////////////////////////
///////////////////////////////////
MqChannel::MqChannel(const std::string& remoteMqName, const std::string& localMqName, unsigned bufsize)
  :m_runListenThread(true)
  ,m_localMqHandle(INVALID_HANDLE_VALUE)
  ,m_remoteMqHandle(INVALID_HANDLE_VALUE)
  ,m_localMqName(localMqName)
  ,m_remoteMqName(remoteMqName)
  ,m_bufsize(bufsize)
{
  m_connected = false;
  m_rx = ant_new unsigned char[m_bufsize];
  memset(m_rx, 0, m_bufsize);

#if WIN
  m_localMqName = std::string("\\\\.\\pipe\\") + m_localMqName;
  m_remoteMqName = std::string("\\\\.\\pipe\\") + m_remoteMqName;
#else
  m_localMqName = std::string("/") + m_localMqName;
  m_remoteMqName = std::string("/") + m_remoteMqName;
#endif

  // The main loop creates an instance of the named pipe and
  // then waits for a client to connect to it. When the client
  // connects, a thread is created to handle communications
  // with that client, and this loop is free to wait for the
  // next client connect request. It is an infinite loop.

  TRC_DBG("nPipe Server: Main thread awaiting client connection on: " << m_localMqName);
#ifndef WIN
  struct mq_attr attr;

  attr.mq_flags = 0;
  attr.mq_maxmsg = MAX_MESSAGES;
  attr.mq_msgsize = MAX_MSG_SIZE;
  attr.mq_curmsgs = 0;

  m_localMqHandle = mq_open(
	m_localMqName.c_str(),    // pipe name
	O_RDONLY | O_CREAT,
	QUEUE_PERMISSIONS,
	&attr);
#else
  m_localMqHandle = CreateNamedPipe(
    m_localMqName.c_str(),    // pipe name
    PIPE_ACCESS_INBOUND,      // read access
    PIPE_TYPE_MESSAGE |       // message type pipe
    PIPE_READMODE_MESSAGE |   // message-read mode
    PIPE_WAIT,                // blocking mode
    PIPE_UNLIMITED_INSTANCES, // max. instances
    m_bufsize,                // output buffer size
    m_bufsize,                // input buffer size
    0,                        // client time-out
    NULL);                    // default security attribute
#endif
  if (m_localMqHandle == INVALID_HANDLE_VALUE) {
    THROW_EX(MqChannelException, "CreateNamedPipe failed: " << NAME_PAR(GetLastError, GetLastError()));
  }

  m_listenThread = std::thread(&MqChannel::listen, this);
}

MqChannel::~MqChannel()
{
  // Flush the pipe to allow the client to read the pipe's contents
  // before disconnecting. Then disconnect the pipe, and close the
  // handle to this pipe instance.

  //TODO
  //FlushFileBuffers(hPipe);
  //DisconnectNamedPipe(hPipe);
  //CloseHandle(hPipe);

  TRC_DBG("joining udp listening thread");
  if (m_listenThread.joinable())
    m_listenThread.join();
  TRC_DBG("listening thread joined");

  delete[] m_rx;
}

void MqChannel::listen()
{
  TRC_ENTER("thread starts");

  try {
  // Wait for the client to connect; if it succeeds,
  // the function returns a nonzero value. If the function
  // returns zero, GetLastError returns ERROR_PIPE_CONNECTED.
  while (m_runListenThread) {

    unsigned long cbBytesRead = 0;
    bool fSuccess(false);

#ifdef WIN
    // Wait to connect form cient
    fSuccess = ConnectNamedPipe(m_localMqHandle, NULL);
    if (!fSuccess) {
      THROW_EX(MqChannelException, "ConnectNamedPipe failed: " << NAME_PAR(GetLastError, GetLastError()));
    }

    // Connected from client open write channel to client
    connect(false);

    // Loop for reading
    while (m_runListenThread) {
      cbBytesRead = 0;
      // Read client requests from the pipe. This simplistic code only allows messages up to BUFSIZE characters in length.
      fSuccess = ReadFile(
        m_localMqHandle, // handle to pipe
        m_rx,            // buffer to receive data
        m_bufsize,       // size of buffer
        &cbBytesRead,    // number of bytes read
        NULL);           // not overlapped I/O

      if (!fSuccess || cbBytesRead == 0) {
        if (GetLastError() == ERROR_BROKEN_PIPE) {
          THROW_EX(MqChannelException, "Client disconnected: " << NAME_PAR(GetLastError, GetLastError()));
        }
        else {
          THROW_EX(MqChannelException, "ReadFile failed: " << NAME_PAR(GetLastError, GetLastError()));
        }
        break;
      }
#else
      // get the oldest message with highest priority
      cbBytesRead = mq_receive(m_localMqHandle, (char*)m_rx, m_bufsize, NULL);
      if (cbBytesRead <= 0) {
        THROW_EX(MqChannelException, "ReadFile failed: " << NAME_PAR(GetLastError, GetLastError()));
      }
#endif
      std::basic_string<unsigned char> message(m_rx, cbBytesRead);
      m_receiveFromFunc(message);
    }
  }

  catch (MqChannelException& e) {
    CATCH_EX("listening thread finished", MqChannelException, e);
    m_runListenThread = false;
  }
  //m_isListening = false;
  TRC_LEAVE("thread stopped");
}

void MqChannel::connect(bool force)
{
  std::lock_guard<std::mutex> lck(m_connectMtx);
  if (force) {
#ifdef WIN
	  CloseHandle(m_remoteMqHandle);
#else
  mq_close(m_remoteMqHandle);
#endif
  }
  if (!m_connected) {
    // Open write channel to client
#ifdef WIN
	m_remoteMqHandle = CreateFile(
		m_remoteMqName.c_str(), // pipe name
		GENERIC_WRITE,   // write access
		0,              // no sharing
		NULL,           // default security attributes
		OPEN_EXISTING,  // opens existing pipe
		0,              // default attributes
		NULL);          // no template file
#else
	// send reply message to client
	m_remoteMqHandle = mq_open(
	  m_remoteMqName.c_str(), // pipe name
	  O_WRONLY);
#endif
    if (m_remoteMqHandle == INVALID_HANDLE_VALUE) {
      TRC_WAR("CreateFile failed: " << NAME_PAR(GetLastError, GetLastError()));
      //if (GetLastError() != ERROR_PIPE_BUSY)
    }
    else {
      // The pipe connected; change to message-read mode.
      //dwMode = PIPE_READMODE_MESSAGE;
      //fSuccess = SetNamedPipeHandleState(
      //  hPipe,    // pipe handle
      //  &dwMode,  // new pipe mode
      //  NULL,     // don't set maximum bytes
      //  NULL);    // don't set maximum time
      //if (!fSuccess)
      //{
      //  TRC_WAR("SetNamedPipeHandleState failed: " << NAME_PAR(GetLastError, GetLastError()));
      //  return -1;
      //}
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

  // Write the reply to the pipe.
  while (true) {
#ifdef WIN
	fSuccess = WriteFile(
      m_remoteMqHandle, // handle to pipe
      message.data(),   // buffer to write from
      toWrite,          // number of bytes to write
      &written,         // number of bytes written
      NULL);            // not overlapped I/O
#else
    written = toWrite;
	int retval = mq_send(
	  m_remoteMqHandle, // handle to pipe
      (const char*)message.data(),   // buffer to write from
      toWrite,          // number of bytes to write
	  0);
    if (retval == -1)
      fSuccess = false;
#endif
    if (!fSuccess || toWrite != written) {
      //TODO check error if it make sense
      if (!reconnect) {
        connect(true);
        reconnect = true;
      }
      else {
        THROW_EX(MqChannelException, "WriteFile failed: " << NAME_PAR(GetLastError, GetLastError()));
      }
    }
    else
      break;
  }
}

void MqChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
}
#endif

#if 0
//https://www.softprayog.in/programming/interprocess-communication-using-posix-message-queues-in-linux
/* * server.c: Server program
* to demonstrate interprocess commnuication
* with POSIX message queues */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#define SERVER_QUEUE_NAME "/sp-example-server"
#define QUEUE_PERMISSIONS 0660
#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 256
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10

int main_server(int argc, char **argv)
{
  mqd_t qd_server, qd_client;  // queue descriptors
  long token_number = 1; // next token to be given to client

  printf("Server: Hello, World!\n");

  struct mq_attr attr;

  attr.mq_flags = 0;
  attr.mq_maxmsg = MAX_MESSAGES;
  attr.mq_msgsize = MAX_MSG_SIZE;
  attr.mq_curmsgs = 0;

  if ((qd_server = mq_open(SERVER_QUEUE_NAME, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
    perror("Server: mq_open (server)");
    exit(1);
  }

  char in_buffer[MSG_BUFFER_SIZE];
  char out_buffer[MSG_BUFFER_SIZE];

  while (1) {
    // get the oldest message with highest priority
    if (mq_receive(qd_server, in_buffer, MSG_BUFFER_SIZE, NULL) == -1) {
      perror("Server: mq_receive");
      exit(1);
    }

    printf("Server: message received.\n");

    // send reply message to client
    if ((qd_client = mq_open(in_buffer, O_WRONLY)) == 1) {
      perror("Server: Not able to open client queue");
      continue;
    }

    sprintf(out_buffer, "%ld", token_number);

    if (mq_send(qd_client, out_buffer, strlen(out_buffer), 0) == -1) {
      perror("Server: Not able to send message to client");
      continue;
    }

    printf("Server: response sent to client.\n");
    token_number++;
  }
}

/* * client.c: Client program
* to demonstrate interprocess commnuication
* with POSIX message queues */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
//#include <cstdio>
#include <unistd.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#define SERVER_QUEUE_NAME "/sp-example-server"
#define QUEUE_PERMISSIONS 0660
#define MAX_MESSAGES 10
#define MAX_MSG_SIZE 256
#define MSG_BUFFER_SIZE MAX_MSG_SIZE + 10

int main_client(int argc, char **argv) {

  char client_queue_name[64];
  mqd_t qd_server, qd_client; // queue descriptors

  // create the client queue for receiving messages from server
  sprintf(client_queue_name, "/sp-example-client-%d", getpid());

  struct mq_attr attr;

  attr.mq_flags = 0;
  attr.mq_maxmsg = MAX_MESSAGES;
  attr.mq_msgsize = MAX_MSG_SIZE;
  attr.mq_curmsgs = 0;

  if ((qd_client = mq_open(client_queue_name, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
    perror("Client: mq_open (client)");
    exit(1);
  }

  if ((qd_server = mq_open(SERVER_QUEUE_NAME, O_WRONLY)) == -1) {
    perror("Client: mq_open (server)");
    exit(1);
  }

  char in_buffer[MSG_BUFFER_SIZE];

  printf("Ask for a token (Press <ENTER>): ");

  char temp_buf[10];

  while (fgets(temp_buf, 2, stdin)) {

    // send message to server
    if (mq_send(qd_server, client_queue_name, strlen(client_queue_name), 0) == -1) {
      perror("Client: Not able to send message to server");
      continue;
    }

    // receive response from server

    if (mq_receive(qd_client, in_buffer, MSG_BUFFER_SIZE, NULL) == -1) {
      perror("Client: mq_receive");
      exit(1);
    }
    // display token received from server
    printf("Client: Token received from server: %s\n\n", in_buffer);

    printf("Ask for a token (Press ): ");
  }


  if (mq_close(qd_client) == -1) {
    perror("Client: mq_close");
    exit(1);
  }

  if (mq_unlink(client_queue_name) == -1) {
    perror("Client: mq_unlink");
    exit(1);
  }
  printf("Client: bye\n");

  exit(0);
}
#endif
