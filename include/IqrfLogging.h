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

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <mutex>

static const long TRC_DEFAULT_FILE_MAXSIZE(10 * 1024 * 1024);

// Initialize tracing. It has to be placed once in a binary
// It has to be started by TRC_START to provide traces
#define TRC_INIT() \
namespace iqrf { \
  Tracer& Tracer::getTracer() { static Tracer tracer; return tracer; }\
  static Tracer& iqrfTracer(Tracer::getTracer()); }

// Starts tracing. The file is reset after reaching filesize [Byte]
// If the filename is empty string, it provides traces to stdout
#define TRC_START(filename, level, filesize) \
iqrf::Tracer::getTracer().start(filename, level, filesize);

#define TRC_STOP() \
iqrf::Tracer::getTracer().stop();

//convenient trace macros
#ifdef _DEBUG
#define TRC_ERR(trc)  TRC(iqrf::Level::err, trc)
#define TRC_WAR(trc)  TRC(iqrf::Level::war, trc)
#define TRC_INF(trc)  TRC(iqrf::Level::inf, trc)
#define TRC_DBG(trc)  TRC(iqrf::Level::dbg, trc)
#define TRC_ENTER(trc) TRC(iqrf::Level::dbg, "{ENTER} " << trc)
#define TRC_LEAVE(trc) TRC(iqrf::Level::dbg, "{LEAVE} " << trc)
#else
#define TRC_ERR(trc)  TRC(iqrf::Level::err, trc)
#define TRC_WAR(trc)  TRC(iqrf::Level::war, trc)
#define TRC_INF(trc)  TRC(iqrf::Level::inf, trc)
#define TRC_DBG(trc)
#define TRC_ENTER(trc)
#define TRC_LEAVE(trc)
#endif

#define PAR(par)                #par "=\"" << par << "\" "
#define PAR_HEX(par)            #par "=\"0x" << std::hex << par << std::dec << "\" "
#define NAME_PAR(name, par)     #name "=\"" << par << "\" "
#define NAME_PAR_HEX(name,par)  #name "=\"0x" << std::hex << par << std::dec << "\" "

// Inserts binary data to trace in the form:
// 16:  34 35 36 36 35 34 36 35 34 36 35 35 34 36 35 34    4566546546554654
// 32:  36 35 34 36 35 34 36 35 34 36 35 34 36 35 34 36    6546546546546546
// 48:  35 34 36 35 34 36 35 34 36 35 34 36 35 34 32 37    5465465465465427 <I/>
// It is developer's responsibility to passed proper len of "owned" data. Uprintable characters are replaced by '.'
#define FORM_HEX(ptr, len)      iqrf::TracerHexString((unsigned char*)ptr, len)

//exceptions tracing
#define THROW_EX(extype, exmsg) { \
  std::ostringstream ostrex; ostrex << exmsg; \
  TRC_WAR("Throwing " << #extype << ": " << ostrex.str()); \
  extype ex(ostrex.str().c_str()); throw ex; }

#define CATCH_EX(msg, extype, ex) { \
  TRC_WAR("Caught " << msg << ": " << #extype << ": " << ex.what()); }

//auxiliary macro
#ifdef _DEBUG
#define FLF iqrf::TracerNiceFileName(__FILE__) << " ln:" << \
__LINE__ << iqrf::TracerNiceFuncName(__FUNCTION__)
#else
#define FLF iqrf::TracerNiceFuncName(__FUNCTION__)
#endif

#define TRC(level, msg) { \
  if(iqrf::Tracer::getTracer().isOn(level)) \
  { std::ostringstream ostr; \
  ostr << FLF << std::endl << msg << std::endl; \
  iqrf::Tracer::getTracer().write(level, ostr.str()); } }

namespace iqrf {

  enum Level {
    err,
    war,
    inf,
    dbg
  };

  class TracerNiceFileName {
  public:
    TracerNiceFileName(const std::string& fname)
      :file_name(fname)
    {}

    friend std::ostream& operator << (std::ostream& o, const TracerNiceFileName& fn)
    {
      std::string niceName = fn.file_name;
      size_t found = niceName.find_last_of("/\\");
      if (std::string::npos != found)
        niceName = niceName.substr(found + 1);

      o << " \"" << niceName << "\"";
      return o;
    }
  private:
    std::string file_name;
  };

  //TODO check conversion of lambdas
  class TracerNiceFuncName {
  public:
    TracerNiceFuncName(const std::string& fname)
      :func_name(fname)
    {}

    friend std::ostream& operator << (std::ostream& o, const TracerNiceFuncName& fn)
    {
      std::string niceName = fn.func_name;
      size_t end = niceName.find('(');
      if (std::string::npos != end)
    	  niceName = niceName.substr(0, end);

      size_t beg = niceName.rfind(' ');

      if (std::string::npos != beg) {
        beg++;
        niceName = niceName.substr(beg, niceName.size() - 1);
      }

      o << " " << niceName << "()";
      return o;
    }
  private:
    std::string func_name;
  };

  class Tracer {
  public:
    static Tracer& getTracer();

    bool isOn(Level level)
    {
      return level <= m_level;
    }

    void write(Level level, const std::string& msg)
    {
      std::lock_guard<std::mutex> lck(m_mtx);

      time_t now = time(NULL);
      char   buff[32];
      strftime(buff, sizeof(buff), "%d-%m-%Y %H:%M:%S", localtime(&now));

      if (m_started) {
        if (m_cout)
          std::cout << buff << " " << levelToChar(level) << msg;
        else {
          if (m_ofstream.is_open()) {
            m_ofstream << buff << " " << levelToChar(level) << msg;
            if (m_ofstream.tellp() > m_maxSize)
            {
              resetFile();
            }
          }
        }
      }
    }

    void start(const std::string& fname, Level level = Level::dbg, long maxSize = TRC_DEFAULT_FILE_MAXSIZE)
    {
      std::lock_guard<std::mutex> lck(m_mtx);
      closeFile();

      m_fname = fname;
      m_cout = fname.empty();
      m_maxSize = maxSize;
      m_level = level;

      if (!m_fname.empty())
        openFile();

      m_started = true;
    }

    void stop()
    {
      std::lock_guard<std::mutex> lck(m_mtx);
      closeFile();
      m_started = false;
    }

  private:
    Tracer()
      : m_cout(false)
      , m_maxSize(-1)
      , m_started(false)
      , m_level(Level::dbg)
    {
    }

    void openFile()
    {
      static unsigned count = 0;
      if (!m_ofstream.is_open())
      {
        m_ofstream.open(m_fname, std::ofstream::out | std::ofstream::trunc);
        if (!m_ofstream.is_open())
          std::cerr << std::endl << "Cannot open: " << PAR(m_fname) << std::endl;
        m_ofstream << "file: " << count++ << " opened/reset" << std::endl;
      }
    }

    void closeFile()
    {
      if (m_ofstream.is_open()) {
        m_ofstream.flush();
        m_ofstream.close();
      }
    }

    void resetFile()
    {
      closeFile();
      openFile();
    }

    const char* levelToChar(Level level)
    {
      switch (level) {
      case err:
        return "{ERR}";
      case war:
        return "{WAR}";
      case inf:
        return "{INF}";
      case dbg:
        return "{DBG}";
      default:
        return "{???}";
      }
    }

    Tracer & operator = (const Tracer& t);
    Tracer(const Tracer& t);

    //std::recursive_mutex m_mtx;
    std::mutex m_mtx;
    std::string m_fname;
    std::ofstream m_ofstream;
    bool m_cout;
    bool m_started;
    long m_maxSize;
    Level m_level;

  };

  class TracerHexString
  {
  public:
    TracerHexString(const void* buf, const long len, bool plain = false) {
      os.setf(std::ios::hex, std::ios::basefield); os.fill('0');

      int i = 1;
      for (; i < len + 1; i++) {
        unsigned char c = 0xff & ((const unsigned char*)buf)[i - 1];
        //bin output
        os << std::setw(2) << (int)c << ' ';
        oschar.put(isgraph(c) ? (char)c : '.');
        if (!(i % 16)) {
          if (!plain) {
            //char output
            os << oschar.str();
            oschar.seekp(0);
            os << std::endl;
          }
          //os << std::endl;
        }
      }
      if (!plain) {
        //align last line
        i--;
        if (i % 16) {
          for (; i % 16; i++) {
            os << "   ";
            oschar << ' ';
          }
          os << oschar.str();
          os << std::endl;
        }
      }
    }

    virtual ~TracerHexString() {}

  private:
    TracerHexString();

    std::ostringstream os;
    std::ostringstream oschar;

    friend std::ostream& operator<<(std::ostream& out, const TracerHexString& hex) {
      out << hex.os.str();
      return out;
    }
  };

} //namespace iqrf
