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

#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include "basic/dictionary.h"
#include <iostream>
#include <string>

class HttpServer
{
public:
  enum
  {
    HTTP_OK = 200,
    HTTP_CREATED = 201,

    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_CONFLICT = 409,

    HTTP_SERVICE_UNAVAILABLE = 503,
  };
  static std::string statusReason(int status);
  static const char *HTTP_GET, *HTTP_POST, *HTTP_DELETE;
  static const char *HTTP_HEADER_CONTENT_LENGTH, *HTTP_HEADER_CONTENT_TYPE,
    *HTTP_HEADER_LOCATION, *HTTP_HEADER_ACCEPT, *HTTP_HEADER_USER_AGENT,
    *HTTP_HEADER_REFERER, *HTTP_HEADER_TRANSFER_ENCODING,
    *HTTP_HEADER_CONNECTION, *HTTP_HEADER_CONTENT_DISPOSITION,
    *HTTP_HEADER_REFRESH;

  static const char *MIME_TYPE_JPEG, *MIME_TYPE_PDF, *MIME_TYPE_PNG;
  static std::string fileExtension(const std::string& mimeType);

  HttpServer();
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  HttpServer& setInterfaceName(const std::string&);
  const std::string& interfaceName() const;
  HttpServer& setInterfaceIndex(int);
  enum
  {
    anyInterface = -1,
    invalidInterface = 0
  };
  int interfaceIndex() const;
  HttpServer& setPort(uint16_t port);
  uint16_t port() const;
  HttpServer& setUnixSocket(const std::string&);
  const std::string& unixSocket() const;
  HttpServer& setBacklog(int);
  int backlog() const;

  bool run();
  bool terminate(int status);
  int terminationStatus() const;
  int lastError() const;

  class Request
  {
  public:
    explicit Request(std::istream&);
    bool isValid() const { return mValid; }
    const std::string& uri() const { return mUri; }
    const std::string& method() const { return mMethod; }
    const std::string& protocol() const { return mProtocol; }
    const std::string& header(const std::string& s) const;
    const Dictionary& headers() const { return mHeaders; }
    int contentLength() const;
    const std::string& content() const;
    bool hasFormData() const;
    const Dictionary& formData() const;
    const std::istream& stream() const { return mStream; }
    std::string& logInfo() { return mLogInfo; }
    std::ostream& print(std::ostream&) const;

  private:
    std::istream& mStream;
    bool mValid;
    std::string mUri, mMethod, mProtocol, mLogInfo;
    Dictionary mHeaders;
    mutable std::string mContent;
    mutable Dictionary mFormData;
  };

  class Response
  {
  public:
    explicit Response(std::ostream&);
    ~Response();
    Response& setStatus(int status)
    {
      mStatus = status;
      return *this;
    }
    int status() const { return mStatus; }
    Response& setHeader(const std::string& key, const std::string& value);
    Response& setHeader(const std::string& key, int value);
    const std::string& header(const std::string& key) const;
    std::ostream& send();
    void sendWithContent(const std::string&);
    bool sent() const { return mSent; }
    std::streampos contentBegin() const { return mContentBegin; }
    std::ostream& print(std::ostream&) const;

  private:
    std::ostream& sendHeaders();
    std::ostream& mStream;
    bool mSent;
    std::streampos mContentBegin;
    int mStatus;
    Dictionary mHeaders;
    struct Chunkstream;
    Chunkstream* mpChunkstream;
  };

protected:
  virtual void onRequest(const Request&, Response&);

private:
  struct Private;
  Private* p;
};

inline std::ostream&
operator<<(std::ostream& os, const HttpServer::Request& r)
{
  return r.print(os);
}
inline std::ostream&
operator<<(std::ostream& os, const HttpServer::Response& r)
{
  return r.print(os);
}

#endif // HTTPSERVER_H
