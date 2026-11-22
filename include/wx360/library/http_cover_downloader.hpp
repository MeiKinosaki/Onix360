#pragma once
#include "wx360/library/cover_library.hpp"
#include <cstdint>
#include <fstream>
#include <string>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif
namespace wx360::covers {
class HttpCoverDownloader {
public:
 static constexpr std::uint64_t kMaxImageBytes=8ULL*1024ULL*1024ULL;
 static bool download(std::string_view url,const std::filesystem::path& destination,void*) {
#if defined(_WIN32)
  if(url.size()>4096||url.substr(0,8)!="https://")return false;
  std::wstring wide(url.begin(),url.end());
  URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
  parts.dwSchemeLength=static_cast<DWORD>(-1);parts.dwHostNameLength=static_cast<DWORD>(-1);
  parts.dwUrlPathLength=static_cast<DWORD>(-1);parts.dwExtraInfoLength=static_cast<DWORD>(-1);
  if(!WinHttpCrackUrl(wide.c_str(),static_cast<DWORD>(wide.size()),0,&parts)||parts.nScheme!=INTERNET_SCHEME_HTTPS)return false;
  std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
  std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
  if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
  HINTERNET session=WinHttpOpen(L"WX360 Cover Downloader/1.0",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
  if(!session)return false;
  WinHttpSetTimeouts(session,5000,5000,10000,10000);
  HINTERNET connection=WinHttpConnect(session,host.c_str(),parts.nPort,0);
  HINTERNET request=connection?WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr;
  bool ok=false;
  if(request&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr)){
   DWORD status=0,status_size=sizeof(status);
   if(WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&status_size,WINHTTP_NO_HEADER_INDEX)&&status==200){
    std::ofstream out(destination,std::ios::binary|std::ios::trunc);std::uint64_t total=0;ok=static_cast<bool>(out);
    while(ok){
     DWORD available=0;if(!WinHttpQueryDataAvailable(request,&available)){ok=false;break;}
     if(!available)break;
     if(total+available>kMaxImageBytes){ok=false;break;}
     std::vector<char> buffer(available);DWORD received=0;
     if(!WinHttpReadData(request,buffer.data(),available,&received)||received==0){ok=false;break;}
     out.write(buffer.data(),received);if(!out){ok=false;break;}total+=received;
    }
    ok=ok&&total>0&&static_cast<bool>(out);out.close();ok=ok&&static_cast<bool>(out);
   }
  }
  if(request)WinHttpCloseHandle(request);if(connection)WinHttpCloseHandle(connection);WinHttpCloseHandle(session);
  if(!ok){std::error_code ec;std::filesystem::remove(destination,ec);}return ok;
#else
  (void)url;(void)destination;return false;
#endif
 }
};
}