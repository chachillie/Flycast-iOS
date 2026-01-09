// Copyright (c) 2009, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <curl/curl.h>
#include <curl/easy.h>

#include <iostream>
#include <string>

#include "common/linux/libcurl_wrapper.h"
#include "common/using_std_string.h"

namespace google_breakpad {
LibcurlWrapper::LibcurlWrapper()
    : init_ok_(false),
      curl_lib_(nullptr),
      last_curl_error_(""),
      curl_(nullptr),
      formpost_(nullptr),
      lastptr_(nullptr),
      headerlist_(nullptr) {}

LibcurlWrapper::~LibcurlWrapper() {
  if (init_ok_) {
    curl_easy_cleanup(curl_);
  }
}

bool LibcurlWrapper::SetProxy(const string& proxy_host,
                              const string& proxy_userpwd) {
  if (!CheckInit()) return false;

  // Set proxy information if necessary.
  if (!proxy_host.empty()) {
    curl_easy_setopt(curl_, CURLOPT_PROXY, proxy_host.c_str());
  } else {
    std::cout << "SetProxy called with empty proxy host.";
    return false;
  }
  if (!proxy_userpwd.empty()) {
    curl_easy_setopt(curl_, CURLOPT_PROXYUSERPWD, proxy_userpwd.c_str());
  } else {
    std::cout << "SetProxy called with empty proxy username/password.";
    return false;
  }
  std::cout << "Set proxy host to " << proxy_host;
  return true;
}

bool LibcurlWrapper::AddFile(const string& upload_file_path,
                             const string& basename) {
  if (!CheckInit()) return false;

  std::cout << "Adding " << upload_file_path << " to form upload.";
  // Add form file.
  curl_formadd(&formpost_, &lastptr_,
              CURLFORM_COPYNAME, basename.c_str(),
              CURLFORM_FILE, upload_file_path.c_str(),
              CURLFORM_END);

  return true;
}

// Callback to get the response data from server.
static size_t WriteCallback(void* ptr, size_t size,
                            size_t nmemb, void* userp) {
  if (!userp)
    return 0;

  string* response = reinterpret_cast<string*>(userp);
  size_t real_size = size * nmemb;
  response->append(reinterpret_cast<char*>(ptr), real_size);
  return real_size;
}

bool LibcurlWrapper::SendRequest(const string& url,
                                 const std::map<string, string>& parameters,
                                 long* http_status_code,
                                 string* http_header_data,
                                 string* http_response_data) {
  if (!CheckInit()) return false;

  std::map<string, string>::const_iterator iter = parameters.begin();
  for (; iter != parameters.end(); ++iter)
    curl_formadd(&formpost_, &lastptr_,
                CURLFORM_COPYNAME, iter->first.c_str(),
                CURLFORM_COPYCONTENTS, iter->second.c_str(),
                CURLFORM_END);

  curl_easy_setopt(curl_, CURLOPT_HTTPPOST, formpost_);

  return SendRequestInner(url, http_status_code, http_header_data,
                          http_response_data);
}

bool LibcurlWrapper::SendGetRequest(const string& url,
                                    long* http_status_code,
                                    string* http_header_data,
                                    string* http_response_data) {
  if (!CheckInit()) return false;

  curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);

  return SendRequestInner(url, http_status_code, http_header_data,
                          http_response_data);
}

bool LibcurlWrapper::SendPutRequest(const string& url,
                                    const string& path,
                                    long* http_status_code,
                                    string* http_header_data,
                                    string* http_response_data) {
  if (!CheckInit()) return false;

  FILE* file = fopen(path.c_str(), "rb");
  curl_easy_setopt(curl_, CURLOPT_UPLOAD, 1L);
  curl_easy_setopt(curl_, CURLOPT_PUT, 1L);
  curl_easy_setopt(curl_, CURLOPT_READDATA, file);

  bool success = SendRequestInner(url, http_status_code, http_header_data,
                                  http_response_data);

  fclose(file);
  return success;
}

bool LibcurlWrapper::SendSimplePostRequest(const string& url,
                                           const string& body,
                                           const string& content_type,
                                           long* http_status_code,
                                           string* http_header_data,
                                           string* http_response_data) {
  if (!CheckInit()) return false;

  curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body.size());
  curl_easy_setopt(curl_, CURLOPT_COPYPOSTFIELDS, body.c_str());

  if (!content_type.empty()) {
    string content_type_header = "Content-Type: " + content_type;
    headerlist_ = curl_slist_append(
        headerlist_,
        content_type_header.c_str());
  }

  return SendRequestInner(url, http_status_code, http_header_data,
                          http_response_data);
}

bool LibcurlWrapper::Init() {
  curl_ = curl_easy_init();

  last_curl_error_ = "No Error";

  init_ok_ = true;
  return true;
}

#define SET_AND_CHECK_FUNCTION_POINTER(var, function_name, type)

bool LibcurlWrapper::SetFunctionPointers() {

  SET_AND_CHECK_FUNCTION_POINTER(easy_init_,
                                 "curl_easy_init",
                                 CURL*(*)());

  SET_AND_CHECK_FUNCTION_POINTER(easy_setopt_,
                                 "curl_easy_setopt",
                                 CURLcode(*)(CURL*, CURLoption, ...));

  SET_AND_CHECK_FUNCTION_POINTER(formadd_, "curl_formadd",
      CURLFORMcode(*)(curl_httppost**, curl_httppost**, ...));

  SET_AND_CHECK_FUNCTION_POINTER(slist_append_, "curl_slist_append",
      curl_slist*(*)(curl_slist*, const char*));

  SET_AND_CHECK_FUNCTION_POINTER(easy_perform_,
                                 "curl_easy_perform",
                                 CURLcode(*)(CURL*));

  SET_AND_CHECK_FUNCTION_POINTER(easy_cleanup_,
                                 "curl_easy_cleanup",
                                 void(*)(CURL*));

  SET_AND_CHECK_FUNCTION_POINTER(easy_getinfo_,
                                 "curl_easy_getinfo",
                                 CURLcode(*)(CURL*, CURLINFO info, ...));

  SET_AND_CHECK_FUNCTION_POINTER(easy_reset_,
                                 "curl_easy_reset",
                                 void(*)(CURL*));

  SET_AND_CHECK_FUNCTION_POINTER(slist_free_all_,
                                 "curl_slist_free_all",
                                 void(*)(curl_slist*));

  SET_AND_CHECK_FUNCTION_POINTER(formfree_,
                                 "curl_formfree",
                                 void(*)(curl_httppost*));
  return true;
}

bool LibcurlWrapper::SendRequestInner(const string& url,
                                      long* http_status_code,
                                      string* http_header_data,
                                      string* http_response_data) {
  string url_copy(url);
  curl_easy_setopt(curl_, CURLOPT_URL, url_copy.c_str());

  // Disable 100-continue header.
  char buf[] = "Expect:";
  headerlist_ = curl_slist_append(headerlist_, buf);
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headerlist_);

  if (http_response_data != nullptr) {
    http_response_data->clear();
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA,
                    reinterpret_cast<void*>(http_response_data));
  }
  if (http_header_data != nullptr) {
    http_header_data->clear();
    curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, WriteCallback);
    curl_easy_setopt(curl_, CURLOPT_HEADERDATA,
                    reinterpret_cast<void*>(http_header_data));
  }
  CURLcode err_code = CURLE_OK;
  err_code = curl_easy_perform(curl_);

  if (http_status_code != nullptr) {
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, http_status_code);
  }

#ifndef NDEBUG
  if (err_code != CURLE_OK)
    fprintf(stderr, "Failed to send http request to %s, error: %s\n",
            url.c_str(),
            curl_easy_strerror(err_code));
#endif

  Reset();

  return err_code == CURLE_OK;
}

void LibcurlWrapper::Reset() {
  if (headerlist_ != nullptr) {
    curl_slist_free_all(headerlist_);
    headerlist_ = nullptr;
  }

  if (formpost_ != nullptr) {
    curl_formfree(formpost_);
    formpost_ = nullptr;
  }

  curl_easy_reset(curl_);
}

bool LibcurlWrapper::CheckInit() {
  if (!init_ok_) {
    std::cout << "LibcurlWrapper: You must call Init(), and have it return "
                 "'true' before invoking any other methods.\n";
    return false;
  }

  return true;
}

}  // namespace google_breakpad
