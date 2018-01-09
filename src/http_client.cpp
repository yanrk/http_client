#include <cassert>
#include <cstring>
#include <set>
#include <list>
#include <vector>
#include <string>
#include <fstream>
#include "http_client.h"
#include "base/charset/charset.h"
#include "base/filesystem/file.h"
#include "base/filesystem/directory.h"
#include "base/thread/thread_group.h"
#include "base/locker/locker.h"
#include "base/utility/guard.h"
#include "base/utility/utility.h"
#include "base/string/string.h"
#include "base/time/time.h"
#include "curl/curl.h"
#include "xzip/xunzip.h"

class Logger
{
public:
    static Logger   s_logger;

public:
    void write(const char * data, size_t data_len);

private:
    Logger();
    ~Logger();

private:
    typedef Stupid::Base::ThreadLocker              thread_locker_t;
    typedef Stupid::Base::Guard<thread_locker_t>    thread_locker_guard_t;

private:
    std::string         m_name;
    std::ofstream       m_file;
    thread_locker_t     m_locker;
};

Logger Logger::s_logger;

Logger::Logger()
    : m_name("./http_client.log")
    , m_file(m_name.c_str(), std::ios::binary | std::ios::trunc)
    , m_locker()
{

}

Logger::~Logger()
{
    m_file.close();
}

void Logger::write(const char * data, size_t data_len)
{
    if (nullptr != data)
    {
        thread_locker_guard_t guard(m_locker);
        m_file.write(data, data_len);
    }
}

static void do_run_log(const char * file, const char * func, size_t line, const char * format, va_list args)
{
    if (nullptr == file || nullptr == func || nullptr == format || nullptr == args)
    {
        return;
    }

    char record[2048] = { 0 };
    size_t record_size = 0;

    record_size += Stupid::Base::stupid_snprintf
    (
        record + record_size, sizeof(record) - record_size, 
        "%s", Stupid::Base::stupid_get_comprehensive_datetime("-", ":", " ", true).c_str()
    );

    record_size += Stupid::Base::stupid_snprintf
    (
        record + record_size, sizeof(record) - record_size, 
        " | %s | T%010u | %s:%s:%05d | ", 
        "TRACE", Stupid::Base::get_tid(), file, func, line
    );

    record_size += Stupid::Base::stupid_vsnprintf
    (
        record + record_size, sizeof(record) - record_size, format, args
    );

    record_size += 1; /* for '\n' or '\0' */
    if (record_size > sizeof(record))
    {
        record_size = sizeof(record);
    }

#ifdef _MSC_VER
    record[record_size - 1] = '\0';
    OutputDebugStringA(record);
#endif // _MSC_VER

    record[record_size - 1] = '\n';
    Logger::s_logger.write(record, record_size);
}

static void run_log(const char * file, const char * func, size_t line, const char * format, ...)
{
    va_list args;

    va_start(args, format);

    do_run_log(file, func, line, format, args);

    va_end(args);
}

#define RUN_LOG(fmt, ...)                                   \
run_log(__FILE__, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)

http_response_callback_info_t::http_response_callback_info_t()
    : user_data(0)
    , status_code(0)
    , error_code(http_response_callback_error_t::callback_message_response_xxx_failure)
    , url_request()
    , save_pathname()
{
    memset(url_request, 0x00, sizeof(url_request));
    memset(save_pathname, 0x00, sizeof(save_pathname));
}

IHttpClientSink::~IHttpClientSink()
{

}

http_download_request_t::http_download_request_t()
    : need_unzip(true)
    , user_data(0)
    , response_sink(nullptr)
    , url_request()
    , hash_request()
    , save_pathname()
    , message_digest()
{
    memset(url_request, 0x00, sizeof(url_request));
    memset(hash_request, 0x00, sizeof(hash_request));
    memset(save_pathname, 0x00, sizeof(save_pathname));
    memset(message_digest, 0x00, sizeof(message_digest));
}

IHttpClient::~IHttpClient()
{

}

struct download_request_status_t
{
    download_request_status_t();

    bool                        been_stopped;
    http_download_request_t     download_request;
};

download_request_status_t::download_request_status_t()
    : been_stopped(false)
    , download_request()
{

}

class HttpClient : public IHttpClient
{
public:
    HttpClient();
    virtual ~HttpClient();

public:
    virtual bool init(size_t max_downloader_count) override;
    virtual void exit() override;

public:
    virtual void post_download_request(const http_download_request_t & download_request) override;
    virtual void stop_download_request(const http_download_request_t & download_request) override;

public:
    virtual bool get_file_size(const char * url_request, size_t & file_size, size_t & url_status_code, size_t & url_error_code);
    virtual bool get_data(const char * url_request, storage_callback_t storage_callback, void * storage_buffer, size_t & url_status_code, size_t & url_error_code);

public:
    void do_download(size_t thread_index);

private:
    void clear();

private:
    bool url_download_with_libcurl(download_request_status_t & download_request_status, http_response_callback_info_t & callback_info);

private:
    typedef Stupid::Base::ThreadGroup               thread_group_t;
    typedef Stupid::Base::ThreadLocker              thread_locker_t;
    typedef Stupid::Base::Guard<thread_locker_t>    thread_locker_guard_t;
    typedef std::set<http_download_request_t>       download_request_set_t;
    typedef std::list<http_download_request_t>      download_request_list_t;
    typedef std::vector<download_request_status_t>  download_request_status_vector_t;

private:
    CURLSH                                        * m_share_handle; /* can be a static member */

private:
    bool                                            m_is_running;

    download_request_set_t                          m_download_request_set;
    thread_locker_t                                 m_download_request_set_locker;

    download_request_list_t                         m_download_request_list;
    thread_locker_t                                 m_download_request_list_locker;

    download_request_status_vector_t                m_download_request_status_vector;

    thread_group_t                                  m_download_thread_group;
};

struct http_thread_param_t
{
    http_thread_param_t(HttpClient & client, size_t index)
        : http_client(client)
        , thread_index(index)
    {

    }
    HttpClient    & http_client;
    size_t          thread_index;
};

thread_return_t STUPID_STDCALL download_thread_run(thread_argument_t argument)
{
    http_thread_param_t * thread_param = reinterpret_cast<http_thread_param_t *>(argument);
    if (nullptr != thread_param)
    {
        thread_param->http_client.do_download(thread_param->thread_index);
        delete thread_param;
    }
    return(THREAD_DEFAULT_RET);
}

static bool operator < (const http_download_request_t & lhs, const http_download_request_t & rhs)
{
    return(strncmp(lhs.url_request, rhs.url_request, sizeof(lhs.url_request)) < 0);
}

static bool operator == (const http_download_request_t & lhs, const http_download_request_t & rhs)
{
    return(0 == strncmp(lhs.url_request, rhs.url_request, sizeof(lhs.url_request)));
}

HttpClient::HttpClient()
    : m_is_running(false)
    , m_download_request_set()
    , m_download_request_set_locker()
    , m_download_request_list()
    , m_download_request_list_locker()
    , m_download_request_status_vector()
    , m_download_thread_group()
{

}

HttpClient::~HttpClient()
{
    exit();
}

bool HttpClient::init(size_t max_downloader_count)
{
    exit();

    do
    {
        m_is_running = true;

        RUN_LOG("[http_client] init begin");

        if (0 == max_downloader_count)
        {
            RUN_LOG("[http_client] init warning: max downloader count is zero (means: can not download asynchronously)");
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);

        m_share_handle = curl_share_init();
        if (nullptr == m_share_handle)
        {
            RUN_LOG("[http_client] init failure: curl_share_init failure");
            break;
        }

        curl_share_setopt(m_share_handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);

        m_download_request_status_vector.resize(max_downloader_count);

        for (size_t index = 0; index < max_downloader_count; ++index)
        {
            http_thread_param_t * thread_param = new http_thread_param_t(*this, index);
            if (nullptr == thread_param)
            {
                RUN_LOG("[http_client] init failure: create download thread %u parameter failure", index);
                break;
            }
            if (!m_download_thread_group.acquire_thread(download_thread_run, thread_param))
            {
                RUN_LOG("[http_client] init failure: acquire download thread %u failure", index);
                delete thread_param;
                break;
            }
        }
        if (m_download_thread_group.size() != max_downloader_count)
        {
            break;
        }

        RUN_LOG("[http_client] init success");

        return(true);
    } while (false);

    exit();

    return(false);
}

void HttpClient::exit()
{
    if (m_is_running)
    {
        RUN_LOG("[http_client] exit begin");
        clear();
        RUN_LOG("[http_client] exit end");
    }
}

void HttpClient::clear()
{
    m_is_running = false;

    for (download_request_status_vector_t::iterator iter = m_download_request_status_vector.begin(); m_download_request_status_vector.end() != iter; ++iter)
    {
        iter->been_stopped = true;
    }

    m_download_thread_group.release_threads();

    curl_share_cleanup(m_share_handle);
    m_share_handle = nullptr;

    curl_global_cleanup();

    m_download_request_status_vector.clear();

    {
        thread_locker_guard_t list_guard(m_download_request_list_locker);
        m_download_request_list.clear();
    }

    {
        thread_locker_guard_t set_guard(m_download_request_set_locker);
        m_download_request_set.clear();
    }
}

void HttpClient::post_download_request(const http_download_request_t & download_request)
{
    if (!m_is_running)
    {
        RUN_LOG("post_download_request failed, http_client is exit");
        return;
    }

    if (0 == m_download_thread_group.size())
    {
        RUN_LOG("post_download_request failed, can not download asynchronously");
        return;
    }

    {
        thread_locker_guard_t set_guard(m_download_request_set_locker);
        if (m_download_request_set.end() != m_download_request_set.find(download_request))
        {
            RUN_LOG("post download request[url request:%s, save pathname:%s] failure", download_request.url_request, download_request.save_pathname);
            return;
        }
        m_download_request_set.insert(download_request);
    }

    {
        thread_locker_guard_t list_guard(m_download_request_list_locker);
        m_download_request_list.push_back(download_request);
    }

    RUN_LOG("post download request[url request:%s, save pathname:%s] success", download_request.url_request, download_request.save_pathname);
}

void HttpClient::stop_download_request(const http_download_request_t & download_request)
{
    if (!m_is_running)
    {
        RUN_LOG("stop_download_request failed, http_client is exit");
        return;
    }

    if (0 == m_download_thread_group.size())
    {
        RUN_LOG("stop_download_request failed, can not download asynchronously");
        return;
    }

    RUN_LOG("stop download request[url request:%s, save pathname:%s] begin", download_request.url_request, download_request.save_pathname);

    {
        thread_locker_guard_t list_guard(m_download_request_list_locker);
        m_download_request_list.remove(download_request);
    }

    {
        thread_locker_guard_t set_guard(m_download_request_set_locker);
        m_download_request_set.erase(download_request);
    }

    for (download_request_status_vector_t::iterator iter = m_download_request_status_vector.begin(); m_download_request_status_vector.end() != iter; ++iter)
    {
        download_request_status_t & download_request_status = *iter;
        if (download_request == download_request_status.download_request)
        {
            download_request_status.been_stopped = true;
            break;
        }
    }

    RUN_LOG("stop download request[url request:%s, save pathname:%s] end", download_request.url_request, download_request.save_pathname);
}

static bool libcurl_get_file_size(CURL * curl, CURLSH * share_handle, const char * url_request, size_t & file_size, size_t & url_status_code, size_t & url_error_code)
{
    file_size = 0;

    curl_easy_setopt(curl, CURLOPT_SHARE, share_handle);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); /* zero means blocking, do not use other values */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url_request);

    CURLcode curl_code = CURLE_OK;

    curl_code = curl_easy_perform(curl);
    if (CURLE_OK != curl_code)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_libcurl_perform_failure;
        const char * curl_error = curl_easy_strerror(curl_code);
        RUN_LOG("curl_easy_perform(get_file_size) failed (%s), when get url (%s)", (nullptr == curl_error ? "unknown" : curl_error), url_request);
        return(false);
    }

    double content_length = 0.0;
    curl_code = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
    if (CURLE_OK != curl_code)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_libcurl_getinfo_failure;
        const char * curl_error = curl_easy_strerror(curl_code);
        RUN_LOG("curl_easy_getinfo(get_file_size) failed (%s), when get url (%s)", (nullptr == curl_error ? "unknown" : curl_error), url_request);
        return(false);
    }

    url_status_code = 200;
    url_error_code = http_response_callback_error_t::callback_message_response_success;
    file_size = static_cast<size_t>(content_length);
    return(true);
}

bool HttpClient::get_file_size(const char * url_request, size_t & file_size, size_t & url_status_code, size_t & url_error_code)
{
    if (!m_is_running)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_download_been_stopped;
        RUN_LOG("get_file_size failed, http_client is exit");
        return(false);
    }

    if (nullptr == url_request)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_argument_invalid;
        RUN_LOG("get_file_size failed, url_request is nullptr");
        return(false);
    }

    CURL * curl = curl_easy_init();
    if (nullptr == curl)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_libcurl_init_failure;
        RUN_LOG("curl_easy_init(get_file_size) failed, when get url (%s)", url_request);
        return(false);
    }

    libcurl_get_file_size(curl, m_share_handle, url_request, file_size, url_status_code, url_error_code);

    curl_easy_cleanup(curl);

    return(http_response_callback_error_t::callback_message_response_success == url_error_code);
}

struct get_data_userdata_t
{
    get_data_userdata_t(storage_callback_t callback, void * buffer);

    storage_callback_t      storage_callback;
    void                  * storage_buffer;
};

get_data_userdata_t::get_data_userdata_t(storage_callback_t callback, void * buffer)
    : storage_callback(callback)
    , storage_buffer(buffer)
{

}

static size_t libcurl_get_data_callback(void * ptr, size_t size, size_t nmemb, void * user_data)
{
    get_data_userdata_t * get_data_userdata = reinterpret_cast<get_data_userdata_t *>(user_data);
    if (nullptr == get_data_userdata || nullptr == get_data_userdata->storage_callback || nullptr == get_data_userdata->storage_buffer)
    {
        return(0); /* tell libcurl to stop get_data */
    }
    const size_t recv_len = size * nmemb;
    const char * data = reinterpret_cast<char *>(ptr);
    if (!get_data_userdata->storage_callback(data, recv_len, get_data_userdata->storage_buffer))
    {
        return(0); /* tell libcurl to stop get_data */
    }
    return(recv_len);
}

static bool libcurl_get_data(CURL * curl, CURLSH * share_handle, const char * url_request, storage_callback_t storage_callback, void * storage_buffer, size_t & url_status_code, size_t & url_error_code)
{
    get_data_userdata_t get_data_userdata(storage_callback, storage_buffer);

    curl_easy_setopt(curl, CURLOPT_SHARE, share_handle);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); /* zero means blocking, do not use other values */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 5L);
    curl_easy_setopt(curl, CURLOPT_URL, url_request);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, libcurl_get_data_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, reinterpret_cast<void *>(&get_data_userdata));

    CURLcode curl_code = CURLE_OK;

    curl_code = curl_easy_perform(curl);
    if (CURLE_OK != curl_code)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_libcurl_perform_failure;
        const char * curl_error = curl_easy_strerror(curl_code);
        RUN_LOG("curl_easy_perform(get_data) failed (%s), when get url (%s)", (nullptr == curl_error ? "unknown" : curl_error), url_request);
        return(false);
    }

    long status_code = 0;

    curl_code = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    if (CURLE_OK != curl_code)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_libcurl_getinfo_failure;
        const char * curl_error = curl_easy_strerror(curl_code);
        RUN_LOG("curl_easy_getinfo(get_data) failed (%s), when get url (%s)", (nullptr == curl_error ? "unknown" : curl_error), url_request);
        return(false);
    }

    url_status_code = status_code;

    if (200L == status_code)
    {
        url_error_code = http_response_callback_error_t::callback_message_response_success;
        RUN_LOG("libcurl_get_data success, when get url (%s)", url_request);
        return(true);
    }

    switch (status_code / 100)
    {
        case 2:
        {
            url_error_code = http_response_callback_error_t::callback_message_response_2xx_failure;
            break;
        }
        case 3:
        {
            url_error_code = http_response_callback_error_t::callback_message_response_3xx_failure;
            break;
        }
        case 4:
        {
            url_error_code = http_response_callback_error_t::callback_message_response_4xx_failure;
            break;
        }
        case 5:
        {
            url_error_code = http_response_callback_error_t::callback_message_response_5xx_failure;
            break;
        }
        default:
        {
            url_error_code = http_response_callback_error_t::callback_message_response_xxx_failure;
            break;
        }
    }

    RUN_LOG("curl_easy_getinfo(get_data) status code (%d), when get url (%s)", status_code, url_request);

    return(false);
}

bool HttpClient::get_data(const char * url_request, storage_callback_t storage_callback, void * storage_buffer, size_t & url_status_code, size_t & url_error_code)
{
    if (!m_is_running)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_download_been_stopped;
        RUN_LOG("get_data failed, http_client is exit");
        return(false);
    }

    if (nullptr == url_request)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_argument_invalid;
        RUN_LOG("get_data failed, url_request is nullptr");
        return(false);
    }

    if (nullptr == storage_callback)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_argument_invalid;
        RUN_LOG("get_data failed, storage_callback is nullptr");
        return(false);
    }

    if (nullptr == storage_buffer)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_argument_invalid;
        RUN_LOG("get_data failed, storage_buffer is nullptr");
        return(false);
    }

    CURL * curl = curl_easy_init();
    if (nullptr == curl)
    {
        url_status_code = 0;
        url_error_code = http_response_callback_error_t::callback_message_libcurl_init_failure;
        RUN_LOG("curl_easy_init(get_data) failed, when get url (%s)", url_request);
        return(false);
    }

    libcurl_get_data(curl, m_share_handle, url_request, storage_callback, storage_buffer, url_status_code, url_error_code);

    curl_easy_cleanup(curl);

    return(http_response_callback_error_t::callback_message_response_success == url_error_code);
}

static bool get_data_storage(const char * data, size_t data_len, void * storage)
{
    std::string * buffer = reinterpret_cast<std::string *>(storage);
    if (nullptr == buffer)
    {
        return(false);
    }
    buffer->append(data, data_len);
    return(true);
}

static bool libcurl_check_need_download(CURL * curl, CURLSH * share_handle, download_request_status_t & download_request_status, http_response_callback_info_t & callback_info)
{
    http_download_request_t & download_request = download_request_status.download_request;

    if ('\0' == download_request.hash_request[0] || '\0' == download_request.message_digest[0])
    {
        return(true);
    }

    std::string storage_buffer;
    if (!libcurl_get_data(curl, share_handle, download_request.hash_request, get_data_storage, reinterpret_cast<void *>(&storage_buffer), callback_info.status_code, callback_info.error_code))
    {
        callback_info.status_code = 0;
        callback_info.error_code = http_response_callback_error_t::callback_message_get_message_digest_failure;
        RUN_LOG("get_data(message_digest) failure, when get url (%s)", download_request.hash_request);
        return(false);
    }

    const size_t digest_size = strlen(download_request.message_digest);
    if (storage_buffer.size() < digest_size || std::string::npos != storage_buffer.substr(0, digest_size).find('<'))
    {
        callback_info.status_code = 0;
        callback_info.error_code = http_response_callback_error_t::callback_message_response_4xx_failure;
        RUN_LOG("get_data(message_digest) failure, message_digest is invalid, when get url (%s)", download_request.hash_request);
        return(false);
    }
    else if (0 == Stupid::Base::stupid_strncmp_ignore_case(storage_buffer.c_str(), download_request.message_digest, digest_size))
    {
        callback_info.status_code = 200;
        callback_info.error_code = http_response_callback_error_t::callback_message_response_success;
        RUN_LOG("get_data(message_digest) success (need not update), when get url (%s)", download_request.hash_request);
        return(false);
    }

    return(true);
}

struct download_userdata_t
{
    download_userdata_t(Stupid::Base::File & file, download_request_status_t & status);

    Stupid::Base::File        & save_file;
    download_request_status_t & download_request_status;
};

download_userdata_t::download_userdata_t(Stupid::Base::File & file, download_request_status_t & status)
    : save_file(file)
    , download_request_status(status)
{

}

static size_t libcurl_download_callback(void * ptr, size_t size, size_t nmemb, void * user_data)
{
    download_userdata_t * download_userdata = reinterpret_cast<download_userdata_t *>(user_data);
    if (nullptr == download_userdata)
    {
        return(0); /* tell libcurl to stop download */
    }
    if (download_userdata->download_request_status.been_stopped)
    {
        return(0); /* tell libcurl to stop download */
    }
    const size_t recv_len = size * nmemb;
    const char * data = reinterpret_cast<char *>(ptr);
    download_userdata->save_file.write(data, recv_len);
    return(recv_len);
}

static bool libcurl_download(CURL * curl, CURLSH * share_handle, download_request_status_t & download_request_status, http_response_callback_info_t & callback_info)
{
    http_download_request_t & download_request = download_request_status.download_request;

    const std::string temp_save_pathname(download_request.save_pathname + std::string(".http.temp"));
    Stupid::Base::File file;
    if (!file.open(temp_save_pathname.c_str(), true, true))
    {
        callback_info.status_code = 0;
        callback_info.error_code = http_response_callback_error_t::callback_message_create_file_failure;
        RUN_LOG("create file (%s) failed, when get url (%s)", temp_save_pathname.c_str(), download_request.url_request);
        return(false);
    }

    download_userdata_t download_userdata(file, download_request_status);

    curl_easy_setopt(curl, CURLOPT_SHARE, share_handle);
    curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L); /* zero means blocking, do not use other values */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 5L);
    curl_easy_setopt(curl, CURLOPT_URL, download_request.url_request);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, libcurl_download_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, reinterpret_cast<void *>(&download_userdata));

    CURLcode curl_code = CURLE_OK;

    curl_code = curl_easy_perform(curl);
    if (CURLE_OK != curl_code)
    {
        callback_info.status_code = 0;
        callback_info.error_code = http_response_callback_error_t::callback_message_libcurl_perform_failure;
        const char * curl_error = curl_easy_strerror(curl_code);
        RUN_LOG("curl_easy_perform failed (%s), when get url (%s)", (nullptr == curl_error ? "unknown" : curl_error), download_request.url_request);
        return(false);
    }

    long status_code = 0;

    curl_code = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
    if (CURLE_OK != curl_code)
    {
        callback_info.status_code = 0;
        callback_info.error_code = http_response_callback_error_t::callback_message_libcurl_getinfo_failure;
        const char * curl_error = curl_easy_strerror(curl_code);
        RUN_LOG("curl_easy_getinfo(status_code) failed (%s), when get url (%s)", (nullptr == curl_error ? "unknown" : curl_error), download_request.url_request);
        return(false);
    }

    file.close();

    if (200L == status_code)
    {
        Stupid::Base::stupid_unlink_safe(download_request.save_pathname);
        if (!Stupid::Base::stupid_rename_safe(temp_save_pathname.c_str(), download_request.save_pathname))
        {
            callback_info.status_code = 0;
            callback_info.error_code = http_response_callback_error_t::callback_message_rename_file_failure;
            RUN_LOG("rename file (%s) -> (%s) failed, when get url (%s)", temp_save_pathname.c_str(), download_request.save_pathname, download_request.url_request);
            return(false);
        }
    }
    else
    {
        Stupid::Base::stupid_unlink_safe(temp_save_pathname.c_str());
    }

    callback_info.status_code = status_code;

    if (200L == status_code)
    {
        callback_info.error_code = http_response_callback_error_t::callback_message_response_success;
        RUN_LOG("url_download_with_libcurl success, when get url (%s)", download_request.url_request);
        return(true);
    }

    switch (status_code / 100)
    {
        case 2:
        {
            callback_info.error_code = http_response_callback_error_t::callback_message_response_2xx_failure;
            break;
        }
        case 3:
        {
            callback_info.error_code = http_response_callback_error_t::callback_message_response_3xx_failure;
            break;
        }
        case 4:
        {
            callback_info.error_code = http_response_callback_error_t::callback_message_response_4xx_failure;
            break;
        }
        case 5:
        {
            callback_info.error_code = http_response_callback_error_t::callback_message_response_5xx_failure;
            break;
        }
        default:
        {
            callback_info.error_code = http_response_callback_error_t::callback_message_response_xxx_failure;
            break;
        }
    }

    RUN_LOG("curl_easy_getinfo status code (%d), when get url (%s)", status_code, download_request.url_request);

    return(false);
}

bool HttpClient::url_download_with_libcurl(download_request_status_t & download_request_status, http_response_callback_info_t & callback_info)
{
    http_download_request_t & download_request = download_request_status.download_request;

    callback_info.status_code = 0;
    callback_info.error_code = http_response_callback_error_t::callback_message_response_xxx_failure;
    callback_info.user_data = download_request.user_data;
    strncpy(callback_info.url_request, download_request.url_request, sizeof(callback_info.url_request));
    strncpy(callback_info.save_pathname, download_request.save_pathname, sizeof(callback_info.save_pathname));

    CURL * curl = curl_easy_init();
    if (nullptr == curl)
    {
        callback_info.status_code = 0;
        callback_info.error_code = http_response_callback_error_t::callback_message_libcurl_init_failure;
        RUN_LOG("curl_easy_init failed, when get url (%s)", download_request.url_request);
        return(false);
    }

    if (!download_request_status.been_stopped && libcurl_check_need_download(curl, m_share_handle, download_request_status, callback_info) && !download_request_status.been_stopped)
    {
        libcurl_download(curl, m_share_handle, download_request_status, callback_info);
    }

    curl_easy_cleanup(curl);

    return(http_response_callback_error_t::callback_message_response_success == callback_info.error_code);
}

static bool unzip_file(const std::string & unzip_dirname, const std::string & zip_filename, bool & been_stopped)
{
#ifdef _MSC_VER
    /*
    Stupid::Base::ThreadLocker s_unzip_locker;
    Stupid::Base::Guard<Stupid::Base::ThreadLocker> unzip_guard(s_unzip_locker);

    std::string current_dirname;
    Stupid::Base::stupid_get_current_work_directory(current_dirname);
    if (!Stupid::Base::stupid_set_current_work_directory(Stupid::Base::ansi_to_utf8(unzip_dirname).c_str()))
    {
        RUN_LOG("set current work directory failure (%s)", unzip_dirname.c_str());
    }
    */

    HZIP hzip = OpenZip(const_cast<char *>(zip_filename.c_str()), 0, ZIP_FILENAME);
    if (nullptr == hzip)
    {
        RUN_LOG("openzip(%s) failed", zip_filename.c_str());
        return(false);
    }

    ZIPENTRY zipentry = { 0 };
    ZRESULT zresult = GetZipItem(hzip, -1, &zipentry);
    if (ZR_OK != zresult)
    {
        RUN_LOG("getzipitem(%s) failed(%u)", zip_filename.c_str(), zresult);
    }
    const int count = zipentry.index;
    for (int index = 0; index < count && !been_stopped; ++index)
    {
        ZRESULT zresult_get = GetZipItem(hzip, index, &zipentry);
        if (ZR_OK != zresult_get)
        {
            RUN_LOG("getzipitem(%s) failed(get_ret=%u)", zip_filename.c_str(), zresult_get);
        }
        const std::string unzip_filename(unzip_dirname + zipentry.name);
        ZRESULT zresult_unzip = UnzipItem(hzip, index, const_cast<char *>(unzip_filename.c_str()), 0, ZIP_FILENAME);
        if (ZR_OK != zresult_unzip)
        {
            RUN_LOG("unzipitem(%s) failed(get_ret=%u, unzip_ret=%u)", zip_filename.c_str(), zresult_get, zresult_unzip);
        }
    }

    CloseZip(hzip);

    /*
    Stupid::Base::stupid_set_current_work_directory(current_dirname);
    */
#endif // _MSC_VER
    return(!been_stopped);
}

void HttpClient::do_download(size_t thread_index)
{
    assert(thread_index < m_download_request_status_vector.size());

    RUN_LOG("do download thread - %u begin", thread_index);

    download_request_status_t & download_request_status = m_download_request_status_vector[thread_index];

    while (m_is_running)
    {
        bool has_request = false;

        {
            thread_locker_guard_t list_guard(m_download_request_list_locker);
            if (!m_download_request_list.empty())
            {
                download_request_status.been_stopped = false;
                download_request_status.download_request = m_download_request_list.front();
                m_download_request_list.pop_front();
                has_request = true;
            }
        }

        if (!m_is_running)
        {
            break;
        }

        if (!has_request)
        {
            Stupid::Base::stupid_ms_sleep(50);
            continue;
        }

        {
            thread_locker_guard_t set_guard(m_download_request_set_locker);
            if (m_download_request_set.end() == m_download_request_set.find(download_request_status.download_request))
            {
                continue; /* has been stopped and removed */
            }
        }

        http_download_request_t & download_request = download_request_status.download_request;

        std::string save_dirname;
        Stupid::Base::stupid_extract_directory(download_request.save_pathname, save_dirname, true);
        Stupid::Base::stupid_create_directory_recursive(save_dirname);

        http_response_callback_info_t callback_info;
        if (url_download_with_libcurl(download_request_status, callback_info) && download_request.need_unzip)
        {
            if (!unzip_file(Stupid::Base::utf8_to_ansi(save_dirname), Stupid::Base::utf8_to_ansi(download_request.save_pathname), download_request_status.been_stopped))
            {
                callback_info.status_code = 0;
                callback_info.error_code = http_response_callback_error_t::callback_message_unzip_file_failure;
                RUN_LOG("unzip file (%s) failure", download_request.save_pathname);
            }
        }

        if (http_response_callback_error_t::callback_message_response_success == callback_info.error_code)
        {
            RUN_LOG("handle download request [%s, %s] success", download_request.url_request, download_request.save_pathname);
        }
        else if (download_request_status.been_stopped)
        {
            callback_info.error_code = http_response_callback_error_t::callback_message_download_been_stopped;
            RUN_LOG("handle download request [%s, %s] been stopped", download_request.url_request, download_request.save_pathname);
        }
        else
        {
            RUN_LOG("handle download request [%s, %s] failure", download_request.url_request, download_request.save_pathname);
        }

        if (nullptr != download_request.response_sink)
        {
            download_request.response_sink->on_response(callback_info);
        }

        {
            thread_locker_guard_t set_guard(m_download_request_set_locker);
            m_download_request_set.erase(download_request);
        }
    }

    RUN_LOG("do download thread - %u end", thread_index);
}

IHttpClient * create_http_client()
{
    return(new HttpClient);
}

void destroy_http_client(IHttpClient * http_client)
{
    delete http_client;
}
