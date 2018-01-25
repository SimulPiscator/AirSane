/*
AirSane Imaging Daemon
Copyright (C) 2018 Simul Piscator

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

#include <thread>
#include <atomic>
#include <sstream>
#include <fstream>
#include <cstring>
#include <ctime>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <pthread.h>

#include "basic/fdbuf.h"

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

const char* HttpServer::MIME_TYPE_JPEG = "image/jpeg";
const char* HttpServer::MIME_TYPE_PDF = "application/pdf";
const char* HttpServer::MIME_TYPE_PNG = "image/png";

static const char* statusReason(int status)
{
    switch(status) {
    case HttpServer::HTTP_OK: return "OK";
    case HttpServer::HTTP_CREATED: return "Created";
    case HttpServer::HTTP_BAD_REQUEST: return "Bad Request";
    case HttpServer::HTTP_NOT_FOUND: return "Not Found";
    case HttpServer::HTTP_METHOD_NOT_ALLOWED: return "Method Not Allowed";
    case HttpServer::HTTP_SERVICE_UNAVAILABLE: return "Service Unavailable";
    }
    return "Unknown Reason";
}

static const std::locale clocale = std::locale("C");
static std::string ctolower(const std::string& s)
{
    std::string r = s;
    for(auto& c : r)
        c = std::tolower(c, clocale);
    return r;
}

static std::string ctrim(const std::string& s)
{
    std::string r;
    for(const auto& c : s)
        if(!std::isspace(c, clocale))
            r += c;
    return r;
}

static long hexdecode(const std::string& s)
{
    return ::strtol(s.c_str(), nullptr, 16);
}

static std::string urldecode(const std::string& s)
{
    std::string r;
    for(size_t i = 0; i < s.size(); ++i) {
        if(s[i] == '%') {
            if(i + 1 < s.size() && s[i+1] == '%')
                r += '%', i += 1;
            else if(i + 2 < s.size())
                r += hexdecode(s.substr(i+1, 2)), i += 2;
        }
        else if(s[i] == '+') {
            r += ' ';
        }
        else {
            r += s[i];
        }
    }
    return r;
}

struct HttpServer::Private
{
    HttpServer* mInstance;
    std::atomic<int> mTerminationStatus;

    uint16_t mPort;
    std::string mHostname, mAddress;
    int mBacklog;
    union Sockaddr { sockaddr sa; sockaddr_in in; sockaddr_in6 in6; } mSockaddr;

    std::atomic<bool> mRunning;
    std::atomic<int> mPipeWriteFd;

    Private(HttpServer* instance)
        : mInstance(instance), mPort(0), mBacklog(SOMAXCONN),
          mTerminationStatus(0), mRunning(false), mPipeWriteFd(-1)
    {
        ::memset(&mSockaddr, 0, sizeof(mSockaddr));
    }

    ~Private()
    {
    }

    bool run()
    {
        bool wasRunning = false;
        mRunning.compare_exchange_strong(wasRunning, true);
        if(wasRunning) {
            std::cerr << "server already running" << std::endl;
            mTerminationStatus = EAGAIN;
            return false;
        }
        int pipe[] = { -1, -1 };
        if(::pipe(pipe) < 0) {
            mTerminationStatus = errno;
            return false;
        }
        int pipeReadFd = pipe[0];
        mPipeWriteFd = pipe[1];
        mTerminationStatus = 0;
        size_t socklen = 0;
        switch(mSockaddr.sa.sa_family) {
        case AF_INET:
            mSockaddr.in.sin_port = htons(mPort);
            socklen = sizeof(sockaddr_in);
            break;
        case AF_INET6:
            mSockaddr.in6.sin6_port = htons(mPort);
            socklen = sizeof(sockaddr_in6);
            break;
        }
        int err = 0;
        int sockfd = ::socket(mSockaddr.sa.sa_family, SOCK_STREAM, 0);
        if(sockfd < 0)
            err = errno;
        int reuseaddr = 1;
        if(!err && ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr)) < 0)
            err = errno;
        if(!err && ::bind(sockfd, &mSockaddr.sa, socklen) < 0)
            err = errno;
        if(!err && ::listen(sockfd, mBacklog) < 0)
            err = errno;
        if(!err)
        {
            struct pollfd pfd[] =
            {
                { pipeReadFd, POLLIN, 0 },
                { sockfd, POLLIN, 0 },
            };
            bool done = false;
            while(!done)
            {
                int r = ::poll(pfd, 2, -1);
                if(r > 0 && pfd[0].revents)
                {
                    done = true;
                    int value;
                    if(::read(pipeReadFd, &value, sizeof(value)) != sizeof(value)) {
                        err = errno ? errno : EBADMSG;
                        std::cerr << "error reading from internal pipe" << std::endl;
                    } else {
                        mTerminationStatus = value;
                    }
                }
                else if(r > 0 && pfd[1].revents)
                {
                    Sockaddr addr;
                    socklen_t len = sizeof(addr);
                    int fd = ::accept(sockfd, &addr.sa, &len);
                    if(fd >= 0)
                        std::thread([this, fd, addr](){handleRequest(fd, addr);}).detach();
                }
                else if(r < 0 && errno != EINTR)
                {
                    done = true;
                    err = errno;
                    std::cerr << ::strerror(err) << std::endl;
                }
            }
        }
        if(err)
            mTerminationStatus = err;
        ::close(sockfd);
        ::close(pipeReadFd);
        ::close(mPipeWriteFd);
        mPipeWriteFd = -1;
        mRunning = false;
        return err == 0;
    }

    bool terminate(int status)
    {
        if(!mRunning) {
            mTerminationStatus = status;
            return true;
        }
        return ::write(mPipeWriteFd, &status, sizeof(mTerminationStatus)) == sizeof(mTerminationStatus);
    }

    void handleRequest(int fd, Sockaddr address)
    {
        fdbuf buf(fd);
        std::istream is(&buf);
        std::ostream os(&buf);
        Request request(is);
        Response response(os);
        if(!request.isValid())
        {
             response.setStatus(HTTP_BAD_REQUEST).send();
        }
        else
        {
            mInstance->onRequest(request, response);
            if(!response.sent())
                response.setStatus(HTTP_NOT_FOUND).send();
        }
        os.flush();

        if(std::cout.rdbuf())
        {
            char time[80] = "n/a";
            time_t now = ::time(nullptr);
            struct tm tm_;
            ::strftime(time, sizeof(time), "%d/%b/%Y:%T %z", ::localtime_r(&now, &tm_));

            char clientip[128] = "n/a";
            switch(address.sa.sa_family) {
            case AF_INET:
                ::inet_ntop(AF_INET, &address.in.sin_addr, clientip, sizeof(clientip));
                break;
            case AF_INET6:
                ::inet_ntop(AF_INET6, &address.in6.sin6_addr, clientip, sizeof(clientip));
                break;
            }
            std::cout // apache combined log format, custom loginfo added
                << clientip << " - - [" << time << "] "
                << "\"" << request.method() << " " << request.uri() << "\" "
                << response.status() << " " << os.tellp() - response.contentBegin()
                << " \"" << request.header(HTTP_HEADER_REFERER) << "\""
                << " \"" << request.header(HTTP_HEADER_USER_AGENT) << "\""
                << (request.logInfo().empty() ? "" : " \"" + request.logInfo() + "\"")
                << std::endl;
        }
    }
};

std::string HttpServer::fileExtension(const std::string &mimeType)
{
    if(mimeType == HttpServer::MIME_TYPE_JPEG)
        return ".jpg";
    if(mimeType == HttpServer::MIME_TYPE_PDF)
        return ".pdf";
    if(mimeType == HttpServer::MIME_TYPE_PNG)
        return ".png";
    return "";
}

HttpServer::HttpServer()
    : p(new Private(this))
{
    std::getline(std::ifstream("/etc/hostname"), p->mHostname);
    if(p->mHostname.empty())
        p->mHostname = "unknown";
    setAddress("*").setPort(8080);
}

HttpServer::~HttpServer()
{
    delete p;
}

const std::string &HttpServer::hostname() const
{
    return p->mHostname;
}

HttpServer &HttpServer::setAddress(const std::string &inAddr)
{
    p->mTerminationStatus = 0;
    std::string s = inAddr.empty() ? "*" : inAddr;
    int family = s[0] == '[' ? AF_INET6 : AF_INET;
    if(family == AF_INET6 && s.length() > 2)
        s = s.substr(1, s.length() - 2);
    union { in_addr in; in6_addr in6; } buf;
    if(s == "*") {
        switch(family) {
        case AF_INET:
            buf.in.s_addr = htonl(INADDR_ANY);
            break;
        case AF_INET6:
            buf.in6 = IN6ADDR_ANY_INIT;
            break;
        }
    } else {
        int r = ::inet_pton(family, s.c_str(), &buf);
        if(r < 0)
            p->mTerminationStatus = errno;
        else if(r == 0)
            p->mTerminationStatus = EINVAL;
    }
    if(!p->mTerminationStatus) {
        p->mAddress = inAddr;
        p->mSockaddr.sa.sa_family = family;
        switch(family) {
        case AF_INET:
            p->mSockaddr.in.sin_addr = buf.in;
            break;
        case AF_INET6:
            p->mSockaddr.in6.sin6_addr = buf.in6;
            break;
        }
    }
    return *this;
}

const std::string &HttpServer::address() const
{
    return p->mAddress;
}

HttpServer &HttpServer::setPort(uint16_t port)
{
    p->mPort = port;
    return *this;
}

uint16_t HttpServer::port() const
{
    return p->mPort;
}

HttpServer &HttpServer::setBacklog(int backlog)
{
    p->mBacklog = backlog;
    return *this;
}

int HttpServer::backlog() const
{
    return p->mBacklog;
}

bool HttpServer::run()
{
    return p->run();
}

bool HttpServer::terminate(int signal)
{
    return p->terminate(signal);
}

int HttpServer::terminationStatus() const
{
    return p->mTerminationStatus;
}

void HttpServer::onRequest(const HttpServer::Request &, HttpServer::Response &response)
{
    response.setStatus(HTTP_NOT_FOUND);
    response.send();
}


struct HttpServer::Response::Chunkstream : std::ostream
{
    explicit Chunkstream(std::ostream& os) : std::ostream(&mBuf), mBuf(os) {}
    struct chunkbuf : std::stringbuf
    {
      std::ostream& mStream;
      explicit chunkbuf(std::ostream& os) : mStream(os) {}
      ~chunkbuf() { pubsync(); mStream << "0\r\n\r\n" << std::flush; }
      int sync() override {
          std::stringbuf::sync();
          std::string s = str();
          if(!s.empty()) {
              str("");
              mStream << std::noshowbase << std::hex << s.size() << "\r\n";
              mStream.write(s.data(), s.size());
              mStream << "\r\n" << std::flush;
          }
          return !!mStream ? 0 : -1;
      }
    } mBuf;
};

HttpServer::Response::Response(std::ostream &os)
    : mStream(os), mStatus(HTTP_OK), mSent(false), mContentBegin(0), mpChunkstream(nullptr)
{
}

HttpServer::Response::~Response()
{
    delete mpChunkstream;
}

HttpServer::Response &HttpServer::Response::setHeader(const std::string &key, const std::string &value)
{
    auto nkey = ctolower(ctrim(key));
    auto nvalue = ctrim(value);
    if(nvalue.empty())
        mHeaders.eraseKey(key);
    else
        mHeaders[nkey] = nvalue;
    return *this;
}

HttpServer::Response &HttpServer::Response::setHeader(const std::string &key, int value)
{
    std::ostringstream oss;
    oss << value;
    return setHeader(key, oss.str());
}

const std::string& HttpServer::Response::header(const std::string &key) const
{
    return mHeaders[ctolower(ctrim(key))];
}

std::ostream &HttpServer::Response::send()
{
    setHeader(HTTP_HEADER_CONTENT_LENGTH, "");
    return sendHeaders();
}

void HttpServer::Response::sendWithContent(const std::string &s)
{
    setHeader(HTTP_HEADER_CONTENT_LENGTH, s.size());
    sendHeaders().write(s.data(), s.size()).flush();
}

std::ostream &HttpServer::Response::sendHeaders()
{
    setHeader(HTTP_HEADER_CONNECTION, "close");
    std::string encoding = ctolower(header(HTTP_HEADER_TRANSFER_ENCODING));
    if(encoding == "identity") {
        setHeader(HTTP_HEADER_TRANSFER_ENCODING, "");
    }
    else if(encoding == "chunked") {
        mpChunkstream = new Chunkstream(mStream);
        setHeader(HTTP_HEADER_CONTENT_LENGTH, "");
    }
    else if(!encoding.empty())
        throw std::runtime_error("unknown transfer-encoding: " + encoding);

    mStream << "HTTP/1.1 " << mStatus << " " << statusReason(mStatus) << "\r\n";
    for(const auto& h : mHeaders)
        if(!h.second.empty())
            mStream << h.first << ": " << h.second << "\r\n";
    mStream << "\r\n" << std::flush;
    mSent = true;
    mContentBegin = mStream.tellp();
    return mpChunkstream ? *mpChunkstream : mStream;
}

HttpServer::Request::Request(std::istream & is)
    : mStream(is), mValid(true)
{
    std::string line;
    if(std::getline(is, line)) {
        if(!(std::istringstream(line) >> mMethod >> mUri >> mProtocol))
            mValid = false;
    }
    while(mValid && std::getline(is, line) && line != "\r") {
        if(line.empty())
            mValid = false;
        else if(line.back() != '\r')
            mValid = false;
        else {
            size_t pos = line.find(':');
            if(pos >= line.length())
                mValid = false;
            else {
                auto key = ctolower(ctrim(line.substr(0, pos)));
                auto value = ctrim(line.substr(pos+1));
                mHeaders[key] = value;
            }
        }
    }
}

const std::string& HttpServer::Request::content() const
{
    int length = contentLength();
    if(mContent.empty() && length >= 0) {
        mContent.resize(length);
        mStream.read(const_cast<char*>(mContent.data()), mContent.size());
    }
    return mContent;
}

bool HttpServer::Request::hasFormData() const
{
    return header(HTTP_HEADER_CONTENT_TYPE) == "application/x-www-form-urlencoded"
            && contentLength() > 0;
}

const Dictionary &HttpServer::Request::formData() const
{
    if(hasFormData() && mFormData.empty()) {
        std::istringstream iss(content());
        std::string entry;
        while(std::getline(iss, entry, '&')) {
            size_t pos = entry.find('=');
            std::string key = entry.substr(0, pos);
            std::string value = pos < entry.length() ? entry.substr(pos + 1) : "";
            mFormData[urldecode(key)] = urldecode(value);
        }
    }
    return mFormData;
}

int HttpServer::Request::contentLength() const
{
    return mHeaders.hasKey(HTTP_HEADER_CONTENT_LENGTH) ?
                mHeaders.getNumber(HTTP_HEADER_CONTENT_LENGTH) :
                -1;
}

std::ostream &HttpServer::Request::print(std::ostream &os) const
{
    os << mMethod << " " << mUri << " " << mProtocol << "\n";
    for(const auto& header : mHeaders)
        os << header.first << ": " << header.second << "\n";
    return os;
}

const std::string& HttpServer::Request::header(const std::string& key) const
{
    return mHeaders[ctolower(ctrim(key))];
}
