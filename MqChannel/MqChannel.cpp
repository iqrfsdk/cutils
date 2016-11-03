#include "MqChannel.h"
#include "IqrfLogging.h"

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

const std::string MQ_PREFIX("/");

inline MQDESCR openMqRead(const std::string name, unsigned bufsize)
{
  struct mq_attr attr;

  attr.mq_flags = 0;
  attr.mq_maxmsg = MAX_MESSAGES;
  attr.mq_msgsize = MAX_MSG_SIZE;
  attr.mq_curmsgs = 0;

  return mq_open(name.c_str(), O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr);
}

inline MQDESCR openMqWrite(const std::string name)
{
  m_remoteMqHandle = mq_open(name.c_str(), O_WRONLY);
}

inline void closeMq(MQDESCR mqDescr)
{
  mq_close(m_remoteMqHandle);
}

inline bool readMq(MQDESCR mqDescr, unsigned char* rx, unsigned long bufSize, unsigned long& numOfBytes)
{
  bool retval = true;
  numOfBytes = mq_receive(mqDescr, (char*)rx, bufsize, NULL);
  if (numOfBytes <= 0)
    retval = false;
  return retval;
}

inline bool writeMq(MQDESCR mqDescr, const unsigned char* tx, unsigned long toWrite, unsigned long& written)
{
  written = toWrite;
  int retval = mq_send(mqDescr, (const char*)tx, toWrite, 0);
  if (retval < 0)
    return false;
  return true;
}

#else

const std::string MQ_PREFIX("\\\\.\\pipe\\");

inline MQDESCR openMqRead(const std::string name, unsigned bufsize)
{
  return CreateNamedPipe(name.c_str(), PIPE_ACCESS_INBOUND,
    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
    PIPE_UNLIMITED_INSTANCES, bufsize, bufsize, 0, NULL);
}

inline MQDESCR openMqWrite(const std::string name)
{
  return CreateFile(name.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

inline void closeMq(MQDESCR mqDescr)
{
  CloseHandle(mqDescr);
}

inline bool readMq(MQDESCR mqDescr, unsigned char* rx, unsigned long bufSize, unsigned long& numOfBytes)
{
  return ReadFile(mqDescr, rx, bufSize, &numOfBytes, NULL);
}

inline bool writeMq(MQDESCR mqDescr, const unsigned char* tx, unsigned long toWrite, unsigned long& written)
{
  return WriteFile(mqDescr, tx, toWrite, &written, NULL);
}

#endif

MqChannel::MqChannel(const std::string& remoteMqName, const std::string& localMqName, unsigned bufsize)
  :m_runListenThread(true)
  , m_localMqHandle(INVALID_HANDLE_VALUE)
  , m_remoteMqHandle(INVALID_HANDLE_VALUE)
  , m_localMqName(localMqName)
  , m_remoteMqName(remoteMqName)
  , m_bufsize(bufsize)
{
  m_connected = false;
  m_rx = ant_new unsigned char[m_bufsize];
  memset(m_rx, 0, m_bufsize);

  m_localMqName = MQ_PREFIX + m_localMqName;
  m_remoteMqName = MQ_PREFIX + m_remoteMqName;

  TRC_DBG("nPipe Server: Main thread awaiting client connection on: " << m_localMqName);

  m_localMqHandle = openMqRead(m_localMqName, m_bufsize);
  if (m_localMqHandle == INVALID_HANDLE_VALUE) {
    THROW_EX(MqChannelException, "CreateNamedPipe failed: " << NAME_PAR(GetLastError, GetLastError()));
  }

  m_listenThread = std::thread(&MqChannel::listen, this);
}

MqChannel::~MqChannel()
{
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
    while (m_runListenThread) {

      unsigned long cbBytesRead = 0;
      bool fSuccess(false);

#ifdef WIN
      // Wait to connect from cient
      fSuccess = ConnectNamedPipe(m_localMqHandle, NULL);
      if (!fSuccess) {
        THROW_EX(MqChannelException, "ConnectNamedPipe failed: " << NAME_PAR(GetLastError, GetLastError()));
      }
#endif

      // Loop for reading
      while (m_runListenThread) {
        cbBytesRead = 0;
        fSuccess = readMq(m_localMqHandle, m_rx, m_bufsize, cbBytesRead);
        if (!fSuccess || cbBytesRead <= 0) {
          //if (GetLastError() == ERROR_BROKEN_PIPE) {
          //  THROW_EX(MqChannelException, "Client disconnected: " << NAME_PAR(GetLastError, GetLastError()));
          //}
          THROW_EX(MqChannelException, "ReadFile failed: " << NAME_PAR(GetLastError, GetLastError()));
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

void MqChannel::connect()
{
  if (!m_connected) {

    std::lock_guard<std::mutex> lck(m_connectMtx);
  
    closeMq(m_remoteMqHandle);
    
    // Open write channel to client
    m_remoteMqHandle = openMqWrite(m_remoteMqName);
    if (m_remoteMqHandle == INVALID_HANDLE_VALUE) {
      TRC_WAR("CreateFile failed: " << NAME_PAR(GetLastError, GetLastError()));
      //if (GetLastError() != ERROR_PIPE_BUSY)
    }
    else {
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

  connect(); //open write channel if not connected yet

  fSuccess = writeMq(m_remoteMqHandle, message.data(), toWrite, written);
  if (!fSuccess || toWrite != written) {
    TRC_WAR("WriteFile failed: " << NAME_PAR(GetLastError, GetLastError()));
    m_connected = false;
  }
}

void MqChannel::registerReceiveFromHandler(ReceiveFromFunc receiveFromFunc)
{
  m_receiveFromFunc = receiveFromFunc;
}

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
