/*
AirSane Imaging Daemon
Copyright (C) 2018-2021 Simul Piscator

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "httpserver.h"

#include <atomic>
#include <cstring>
#include <ctime>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#ifdef __FreeBSD__
#include <netinet/in.h>
#endif
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <unistd.h>

#include "basic/fdbuf.h"
#include "errorpage.h"

const char* HttpServer::HTTP_GET = "GET";
const char* HttpServer::HTTP_POST = "POST";
const char* HttpServer::HTTP_DELETE = "DELETE";
// header field names are compared in lower case
const char* HttpServer::HTTP_HEADER_CONTENT_TYPE = "content-type";
const char* HttpServer::HTTP_HEADER_CONTENT_LENGTH = "content-length";
const char* HttpServer::HTTP_HEADER_LOCATION = "location";
const char* HttpServer::HTTP_HEADER_ACCEPT = "accept";
const char* HttpServer::HTTP_HEADER_USER_AGENT = "user-agent";
const char* HttpServer::HTTP_HEADER_REFERER = "referer";
const char* HttpServer::HTTP_HEADER_CONNECTION = "connection";
const char* HttpServer::HTTP_HEADER_TRANSFER_ENCODING = "transfer-encoding";
const char* HttpServer::HTTP_HEADER_CONTENT_DISPOSITION = "content-disposition";
const char* HttpServer::HTTP_HEADER_REFRESH = "refresh";

const char* HttpServer::MIME_TYPE_JPEG = "image/jpeg";
const char* HttpServer::MIME_TYPE_PDF = "application/pdf";
const char* HttpServer::MIME_TYPE_PNG = "image/png";

namespace {

const std::locale clocale = std::locale("C");
std::string
ctolower(const std::string& s)
{
  std::string r = s;
  for (auto& c : r)
    c = std::tolower(c, clocale);
  return r;
}

std::string
ctrim(const std::string& s)
{
  std::string r;
  for (const auto& c : s)
    if (!std::isspace(c, clocale))
      r += c;
  return r;
}

long
hexdecode(const std::string& s)
{
  return ::strtol(s.c_str(), nullptr, 16);
}

std::string
urldecode(const std::string& s)
{
  std::string r;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%') {
      if (i + 1 < s.size() && s[i + 1] == '%') {
        r += '%';
        i += 1;
      } else if (i + 2 < s.size()) {
        r += hexdecode(s.substr(i + 1, 2));
        i += 2;
      }
    } else if (s[i] == '+') {
      r += ' ';
    } else {
      r += s[i];
    }
  }
  return r;
}

typedef union
{
  sockaddr sa;
  sockaddr_in in;
  sockaddr_in6 in6;
  sockaddr_un un;
} Sockaddr;

std::string
ipString(Sockaddr address)
{
  char buf[128] = "n/a";
  switch (address.sa.sa_family) {
    case AF_INET:
      ::inet_ntop(AF_INET, &address.in.sin_addr, buf, sizeof(buf));
      break;
    case AF_INET6:
      ::strcpy(buf, "[");
      ::inet_ntop(AF_INET6, &address.in6.sin6_addr, buf + 1, sizeof(buf) - 2);
      ::strcat(buf, "]");
      break;
    case AF_UNIX:
      ::strcpy(buf, "unix");
      break;
  }
  return buf;
}

uint16_t
portNumber(Sockaddr address)
{
  switch (address.sa.sa_family) {
    case AF_INET:
      return ntohs(address.in.sin_port);
    case AF_INET6:
      return ntohs(address.in6.sin6_port);
  }
  return 0;
}

std::string
describeAddress(const Sockaddr& address)
{
  std::ostringstream oss;
  switch (address.sa.sa_family) {
    case AF_INET:
    case AF_INET6:
      oss << ipString(address) << ":" << portNumber(address);
      break;
    case AF_UNIX:
      oss << address.un.sun_path;
      break;
  }
  return oss.str();
}

std::vector<Sockaddr>
interfaceAddresses(const char* if_name)
{
  std::vector<Sockaddr> r;
  struct ifaddrs* pAddr;
  if (::getifaddrs(&pAddr)) {
    std::cerr << ::strerror(errno) << std::endl;
    return r;
  }
  for (const ifaddrs* p = pAddr; p != nullptr; p = p->ifa_next) {
    if (p->ifa_addr && (!if_name || !::strcmp(if_name, p->ifa_name))) {
      Sockaddr addr;
      switch (p->ifa_addr->sa_family) {
        case AF_INET:
          ::memcpy(&addr.sa, p->ifa_addr, sizeof(sockaddr_in));
          r.push_back(addr);
          break;
        case AF_INET6:
          ::memcpy(&addr.sa, p->ifa_addr, sizeof(sockaddr_in6));
          r.push_back(addr);
          break;
      }
    }
  }
  ::freeifaddrs(pAddr);
  return r;
}

} // namespace

struct HttpServer::Private
{

  HttpServer* mInstance;
  std::atomic<int> mTerminationStatus, mLastError;

  uint16_t mPort;
  std::string mInterfaceName, mUnixSocket;
  int mInterfaceIndex, mBacklog;

  std::atomic<bool> mRunning;
  std::atomic<int> mPipeWriteFd;

  Private(HttpServer* instance)
    : mInstance(instance)
    , mTerminationStatus(0)
    , mLastError(0)
    , mPort(0)
    , mInterfaceIndex(invalidInterface)
    , mBacklog(SOMAXCONN)
    , mRunning(false)
    , mPipeWriteFd(-1)
  {}
  
  int determineAddresses(std::vector<Sockaddr>& addresses)
  {
    int err = 0;
    if (!mUnixSocket.empty()) {
      Sockaddr addr = { 0 };
      addr.sa.sa_family = AF_UNIX;
      if (mUnixSocket.length() >= sizeof(addr.un.sun_path))
        err = ENAMETOOLONG;
      else {
        ::strcpy(addr.un.sun_path, mUnixSocket.c_str());
        addresses.push_back(addr);
      }
    }
    else {
      const char* if_name = nullptr;
      if (mInterfaceIndex == invalidInterface)
        err = ENXIO;
      else if (mInterfaceIndex != anyInterface)
        if_name = mInterfaceName.c_str();
      if (!err)
        addresses = interfaceAddresses(if_name);
    }
    if (!err && addresses.empty())
      err = EINVAL;
    return err;
  }

  int createListeningSocket(Sockaddr& addr)
  {
    size_t socklen = 0;
    switch (addr.sa.sa_family) {
      case AF_INET:
        addr.in.sin_port = htons(mPort);
        socklen = sizeof(sockaddr_in);
        break;
      case AF_INET6:
        addr.in6.sin6_port = htons(mPort);
        socklen = sizeof(sockaddr_in6);
        break;
      case AF_UNIX:
        socklen = sizeof(sockaddr_un);
        break;
    }
    int sockfd = ::socket(addr.sa.sa_family, SOCK_STREAM, 0);
    if (sockfd < 0)
      return -1;
    int err = 0;
    if (addr.sa.sa_family == AF_UNIX) {
      ::unlink(addr.un.sun_path);
    }
    else {
      int reuseaddr = 1;
      err = ::setsockopt(
        sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
    }
    if (!err)
      err = ::bind(sockfd, &addr.sa, socklen);
    if (!err && addr.sa.sa_family == AF_UNIX)
      ::chmod(addr.un.sun_path, 0660);
    if (!err)
      err = ::listen(sockfd, mBacklog);
    if (err) {
      ::close(sockfd);
      sockfd = -1;
    }
    return sockfd;
  }

  bool run()
  {
    bool wasRunning = false;
    mRunning.compare_exchange_strong(wasRunning, true);
    if (wasRunning) {
      std::cerr << "server already running" << std::endl;
      mTerminationStatus = -1;
      return false;
    }
    int pipe[] = { -1, -1 };
    if (::pipe(pipe) < 0) {
      mLastError = errno;
      mTerminationStatus = -1;
      return false;
    }
    int pipeReadFd = pipe[0];
    mPipeWriteFd = pipe[1];
    mTerminationStatus = 0;

    std::vector<Sockaddr> addresses;
    int err = determineAddresses(addresses);
    if (!err) {
      std::vector<struct pollfd> pfds(1);
      pfds[0].fd = pipeReadFd;
      pfds[0].events = POLLIN;
      for (auto& address : addresses) {
        int sockfd = createListeningSocket(address);
        if (sockfd < 0)
          err = errno;
        else {
          struct pollfd pfd = { sockfd, POLLIN, 0 };
          pfds.push_back(pfd);
          std::clog << "listening on " << describeAddress(address)
                    << std::endl;
        }
      }
      bool done = (err != 0);
      while (!done) {
        int r = ::poll(pfds.data(), pfds.size(), -1);
        if (r > 0 && pfds[0].revents) {
          done = true;
          int value;
          if (::read(pipeReadFd, &value, sizeof(value)) != sizeof(value)) {
            err = errno ? errno : EBADMSG;
            std::cerr << "error reading from internal pipe" << std::endl;
          } else {
            mTerminationStatus = value;
          }
        } else if (r > 0) {
          for (size_t i = 1; i < pfds.size(); ++i) {
            if (pfds[i].revents) {
              Sockaddr addr;
              socklen_t len = sizeof(addr);
              int fd = ::accept(pfds[i].fd, &addr.sa, &len);
              if (fd >= 0)
                std::thread([this, fd, addr]() {
                  handleRequest(fd, addr);
                }).detach();
            }
          }
        } else if (r < 0 && errno != EINTR) {
          done = true;
          err = errno;
          std::cerr << ::strerror(err) << std::endl;
        }
      }
      for (size_t i = 1; i < pfds.size(); ++i)
        ::close(pfds[i].fd);
    }
    mLastError = err;
    if (err && !mTerminationStatus)
      mTerminationStatus = -1;
    ::close(pipeReadFd);
    ::close(mPipeWriteFd);
    mPipeWriteFd = -1;
    mRunning = false;
    return err == 0;
  }

  bool terminate(int status)
  {
    if (!mRunning) {
      mTerminationStatus = status;
      return true;
    }
    return ::write(mPipeWriteFd, &status, sizeof(mTerminationStatus)) ==
           sizeof(mTerminationStatus);
  }

  void handleRequest(int fd, Sockaddr address)
  {
    fdbuf buf(fd);
    std::istream is(&buf);
    std::ostream os(&buf);
    Request request(is);
    Response response(os);
    if (!request.isValid()) {
      response.setStatus(HTTP_BAD_REQUEST);
      ErrorPage(HTTP_BAD_REQUEST).render(request, response);
    } else {
      mInstance->onRequest(request, response);
      if (!response.sent()) {
        response.setStatus(HTTP_NOT_FOUND);
        ErrorPage(HTTP_NOT_FOUND).render(request, response);
        std::cerr << "Warning: Error 404 when requesting "
                  << "\"" << request.uri() << "\"" << std::endl;
      }
    }
    os.flush();

    if (std::cout.rdbuf()) {
      char time[80] = "n/a";
      time_t now = ::time(nullptr);
      struct tm tm_;
      ::strftime(
        time, sizeof(time), "%d/%b/%Y:%T %z", ::localtime_r(&now, &tm_));

      std::cout // apache combined log format, custom loginfo added
        << ipString(address) << " - - [" << time << "] "
        << "\"" << request.method() << " " << request.uri() << "\" "
        << response.status() << " " << os.tellp() - response.contentBegin()
        << " \"" << request.header(HTTP_HEADER_REFERER) << "\""
        << " \"" << request.header(HTTP_HEADER_USER_AGENT) << "\""
        << (request.logInfo().empty() ? "" : " \"" + request.logInfo() + "\"")
        << std::endl;
    }
  }
};

std::string
HttpServer::statusReason(int status)
{
  switch (status) {
    case HttpServer::HTTP_OK:
      return "OK";
    case HttpServer::HTTP_CREATED:
      return "Created";
    case HttpServer::HTTP_BAD_REQUEST:
      return "Bad Request";
    case HttpServer::HTTP_NOT_FOUND:
      return "Not Found";
    case HttpServer::HTTP_METHOD_NOT_ALLOWED:
      return "Method Not Allowed";
    case HttpServer::HTTP_SERVICE_UNAVAILABLE:
      return "Service Unavailable";
  }
  return "Unknown Reason";
}

std::string
HttpServer::fileExtension(const std::string& mimeType)
{
  if (mimeType == HttpServer::MIME_TYPE_JPEG)
    return ".jpg";
  if (mimeType == HttpServer::MIME_TYPE_PDF)
    return ".pdf";
  if (mimeType == HttpServer::MIME_TYPE_PNG)
    return ".png";
  return "";
}

HttpServer::HttpServer()
  : p(new Private(this))
{
  setInterfaceIndex(anyInterface).setPort(8080);
}

HttpServer::~HttpServer()
{
  delete p;
}

HttpServer&
HttpServer::setInterfaceName(const std::string& s)
{
  if (s == "*") {
    p->mInterfaceName = "*";
    p->mInterfaceIndex = anyInterface;
  } else {
    unsigned int i = ::if_nametoindex(s.c_str());
    if (i == 0) {
      p->mInterfaceName = "<invalid>";
      p->mInterfaceIndex = invalidInterface;
    } else {
      p->mInterfaceName = s;
      p->mInterfaceIndex = i;
    }
  }
  return *this;
}

HttpServer&
HttpServer::setInterfaceIndex(int i)
{
  if (i == anyInterface) {
    p->mInterfaceName = "*";
    p->mInterfaceIndex = anyInterface;
  } else if (i == invalidInterface) {
    p->mInterfaceName = "<invalid>";
    p->mInterfaceIndex = invalidInterface;
  } else {
    unsigned int idx = i;
    char buf[IF_NAMESIZE] = { 0 };
    if (::if_indextoname(idx, buf)) {
      p->mInterfaceName = buf;
      p->mInterfaceIndex = i;
    } else {
      p->mInterfaceName = "<invalid>";
      p->mInterfaceIndex = invalidInterface;
    }
  }
  return *this;
}

int
HttpServer::interfaceIndex() const
{
  return p->mInterfaceIndex;
}

HttpServer&
HttpServer::setPort(uint16_t port)
{
  p->mPort = port;
  return *this;
}

uint16_t
HttpServer::port() const
{
  return p->mPort;
}

HttpServer&
HttpServer::setUnixSocket(const std::string &path)
{
  p->mUnixSocket = path;
  return *this;
}

const std::string&
HttpServer::unixSocket() const
{
  return p->mUnixSocket;
}

HttpServer&
HttpServer::setBacklog(int backlog)
{
  p->mBacklog = backlog;
  return *this;
}

int
HttpServer::backlog() const
{
  return p->mBacklog;
}

bool
HttpServer::run()
{
  return p->run();
}

bool
HttpServer::terminate(int status)
{
  return p->terminate(status);
}

int
HttpServer::terminationStatus() const
{
  return p->mTerminationStatus;
}

int
HttpServer::lastError() const
{
  return p->mLastError;
}

void
HttpServer::onRequest(const HttpServer::Request&,
                      HttpServer::Response&)
{
}

struct HttpServer::Response::Chunkstream : std::ostream
{
  explicit Chunkstream(std::ostream& os)
    : std::ostream(&mBuf)
    , mBuf(os)
  {}
  struct chunkbuf : std::stringbuf
  {
    std::ostream& mStream;
    std::streampos mTotalWritten;
    explicit chunkbuf(std::ostream& os)
      : mStream(os)
    {}
    ~chunkbuf()
    {
      pubsync();
      mStream << "0\r\n\r\n" << std::flush;
    }
    int sync() override
    {
      std::stringbuf::sync();
      std::string s = str();
      if (!s.empty()) {
        mTotalWritten += s.length();
        str("");
        mStream << std::noshowbase << std::hex << s.size() << "\r\n";
        mStream.write(s.data(), s.size());
        mStream << "\r\n" << std::flush;
      }
      return !!mStream ? 0 : -1;
    }
    std::streampos seekoff(off_type offset,
                           std::ios_base::seekdir dir,
                           std::ios_base::openmode mode) override
    {
      if (offset == 0 && dir == std::ios_base::cur &&
          mode == std::ios_base::out)
        return mTotalWritten + std::stringbuf::seekoff(offset, dir, mode);
      return -1;
    }
  } mBuf;
};

HttpServer::Response::Response(std::ostream& os)
  : mStream(os)
  , mSent(false)
  , mContentBegin(0)
  , mStatus(HTTP_OK)
  , mpChunkstream(nullptr)
{}

HttpServer::Response::~Response()
{
  delete mpChunkstream;
}

HttpServer::Response&
HttpServer::Response::setHeader(const std::string& key,
                                const std::string& value)
{
  auto nkey = ctolower(ctrim(key));
  auto nvalue = ctrim(value);
  if (nvalue.empty())
    mHeaders.eraseKey(key);
  else
    mHeaders[nkey] = nvalue;
  return *this;
}

HttpServer::Response&
HttpServer::Response::setHeader(const std::string& key, int value)
{
  std::ostringstream oss;
  oss << value;
  return setHeader(key, oss.str());
}

const std::string&
HttpServer::Response::header(const std::string& key) const
{
  return mHeaders[ctolower(ctrim(key))];
}

std::ostream&
HttpServer::Response::send()
{
  setHeader(HTTP_HEADER_CONTENT_LENGTH, "");
  return sendHeaders();
}

void
HttpServer::Response::sendWithContent(const std::string& s)
{
  setHeader(HTTP_HEADER_CONTENT_LENGTH, s.size());
  sendHeaders().write(s.data(), s.size()).flush();
}

std::ostream&
HttpServer::Response::sendHeaders()
{
  setHeader(HTTP_HEADER_CONNECTION, "close");
  std::string encoding = ctolower(header(HTTP_HEADER_TRANSFER_ENCODING));
  if (encoding == "identity") {
    setHeader(HTTP_HEADER_TRANSFER_ENCODING, "");
  } else if (encoding == "chunked") {
    delete mpChunkstream;
    mpChunkstream = new Chunkstream(mStream);
    setHeader(HTTP_HEADER_CONTENT_LENGTH, "");
  } else if (!encoding.empty())
    throw std::runtime_error("unknown transfer-encoding: " + encoding);

  mStream << "HTTP/1.1 " << mStatus << " " << statusReason(mStatus) << "\r\n";
  for (const auto& h : mHeaders)
    if (!h.second.empty())
      mStream << h.first << ": " << h.second << "\r\n";
  mStream << "\r\n" << std::flush;
  mSent = true;
  mContentBegin = mStream.tellp();
  return mpChunkstream ? *mpChunkstream : mStream;
}

HttpServer::Request::Request(std::istream& is)
  : mStream(is)
  , mValid(true)
{
  std::string line;
  if (std::getline(is, line)) {
    if (!(std::istringstream(line) >> mMethod >> mUri >> mProtocol))
      mValid = false;
  }
  while (mValid && std::getline(is, line) && line != "\r") {
    if (line.empty())
      mValid = false;
    else if (line.back() != '\r')
      mValid = false;
    else {
      size_t pos = line.find(':');
      if (pos >= line.length())
        mValid = false;
      else {
        auto key = ctolower(ctrim(line.substr(0, pos)));
        auto value = ctrim(line.substr(pos + 1));
        mHeaders[key] = value;
      }
    }
  }
}

const std::string&
HttpServer::Request::content() const
{
  int length = contentLength();
  if (mContent.empty() && length >= 0) {
    mContent.resize(length);
    mStream.read(const_cast<char*>(mContent.data()), mContent.size());
  }
  return mContent;
}

bool
HttpServer::Request::hasFormData() const
{
  return header(HTTP_HEADER_CONTENT_TYPE) ==
           "application/x-www-form-urlencoded" &&
         contentLength() > 0;
}

const Dictionary&
HttpServer::Request::formData() const
{
  if (hasFormData() && mFormData.empty()) {
    std::istringstream iss(content());
    std::string entry;
    while (std::getline(iss, entry, '&')) {
      size_t pos = entry.find('=');
      std::string key = entry.substr(0, pos);
      std::string value = pos < entry.length() ? entry.substr(pos + 1) : "";
      mFormData[urldecode(key)] = urldecode(value);
    }
  }
  return mFormData;
}

int
HttpServer::Request::contentLength() const
{
  return mHeaders.hasKey(HTTP_HEADER_CONTENT_LENGTH)
           ? mHeaders.getNumber(HTTP_HEADER_CONTENT_LENGTH)
           : -1;
}

std::ostream&
HttpServer::Request::print(std::ostream& os) const
{
  os << mMethod << " " << mUri << " " << mProtocol << "\n";
  for (const auto& header : mHeaders)
    os << header.first << ": " << header.second << "\n";
  return os;
}

const std::string&
HttpServer::Request::header(const std::string& key) const
{
  return mHeaders[ctolower(ctrim(key))];
}
