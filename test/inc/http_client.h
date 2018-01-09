/********************************************************
 * Description : http client
 * Author      : ryan
 * Email       : ryan@rayvision.com
 * Version     : 1.0
 * History     :
 * Copyright(C): RAYVISION
 ********************************************************/

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H


#ifdef _MSC_VER
    #define HTTP_CLIENT_CDECL            __cdecl
    #ifdef EXPORT_HTTP_CLIENT_DLL
        #define HTTP_CLIENT_TYPE         __declspec(dllexport)
    #else
        #ifdef USE_HTTP_CLIENT_DLL
            #define HTTP_CLIENT_TYPE     __declspec(dllimport)
        #else
            #define HTTP_CLIENT_TYPE
        #endif // USE_HTTP_CLIENT_DLL
    #endif // EXPORT_HTTP_CLIENT_DLL
#else
    #define HTTP_CLIENT_CDECL
    #define HTTP_CLIENT_TYPE
#endif // _MSC_VER

#define HTTP_CLIENT_CXX_API(return_type) extern HTTP_CLIENT_TYPE return_type HTTP_CLIENT_CDECL

struct http_response_callback_error_t
{
    enum value_t
    {
        callback_message_response_success, 
        callback_message_response_2xx_failure, 
        callback_message_response_3xx_failure, 
        callback_message_response_4xx_failure, 
        callback_message_response_5xx_failure, 
        callback_message_response_xxx_failure, 
        callback_message_libcurl_init_failure, 
        callback_message_libcurl_perform_failure, 
        callback_message_libcurl_getinfo_failure, 
        callback_message_argument_invalid, 
        callback_message_get_message_digest_failure, 
        callback_message_create_file_failure, 
        callback_message_rename_file_failure, 
        callback_message_unzip_file_failure, 
        callback_message_download_been_stopped
    };
};

struct HTTP_CLIENT_TYPE http_response_callback_info_t
{
    http_response_callback_info_t();

    size_t              user_data;
    size_t              status_code;
    size_t              error_code;
    char                url_request[512];
    char                save_pathname[512];
};

struct HTTP_CLIENT_TYPE IHttpClientSink
{
    virtual ~IHttpClientSink() = 0;
    virtual void on_response(const http_response_callback_info_t & callback_info) = 0;
};

struct HTTP_CLIENT_TYPE http_download_request_t
{
    http_download_request_t();

    bool                need_unzip;
    size_t              user_data;
    IHttpClientSink   * response_sink;
    char                url_request[512];
    char                hash_request[512];
    char                save_pathname[512];
    char                message_digest[64];
};

typedef bool (* storage_callback_t) (const char * data, size_t data_len, void * storage);

class HTTP_CLIENT_TYPE IHttpClient
{
public:
    virtual ~IHttpClient() = 0;

public:
    virtual bool init(size_t max_downloader_count = 1) = 0;
    virtual void exit() = 0;

public:
    virtual void post_download_request(const http_download_request_t & download_request) = 0;
    virtual void stop_download_request(const http_download_request_t & download_request) = 0;

public:
    virtual bool get_file_size(const char * url_request, size_t & file_size, size_t & url_status_code, size_t & url_error_code) = 0;
    virtual bool get_data(const char * url_request, storage_callback_t storage_callback, void * storage_buffer, size_t & url_status_code, size_t & url_error_code) = 0;
};

HTTP_CLIENT_CXX_API(IHttpClient *) create_http_client();
HTTP_CLIENT_CXX_API(void) destroy_http_client(IHttpClient *);


#endif // HTTP_CLIENT_H
