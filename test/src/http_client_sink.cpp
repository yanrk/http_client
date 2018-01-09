#include "http_client_sink.h"
#include "base/log/log.h"
#include "base/time/time.h"

static thread_return_t STUPID_STDCALL handle_download_callback(thread_argument_t argument)
{
    HttpClientSink * http_client_sink = reinterpret_cast<HttpClientSink *>(argument);
    if (nullptr != http_client_sink)
    {
        http_client_sink->handle_download_message();
    }
    return(THREAD_DEFAULT_RET);
}

HttpClientSink::HttpClientSink()
    : m_is_running(false)
    , m_download_message_list()
    , m_download_message_locker()
    , m_download_message_handler()
{
    m_download_message_handler.set_thread_args(handle_download_callback, this);
}

HttpClientSink::~HttpClientSink()
{
    exit();
}

bool HttpClientSink::init()
{
    clear();

    for (;;)
    {
        m_is_running = true;

        DBG_LOG("http client sink init begin");

        if (!m_download_message_handler.acquire())
        {
            RUN_LOG_ERR("download message handle thread acquire failure");
            break;
        }

        DBG_LOG("http client sink init end");

        return(true);
    }

    clear();

    return(false);
}

void HttpClientSink::exit()
{
    if (m_is_running)
    {
        DBG_LOG("http client sink exit begin");

        clear();

        DBG_LOG("http client sink exit end");
    }
}

void HttpClientSink::clear()
{
    if (m_is_running)
    {
        m_is_running = false;
        m_download_message_handler.release();
    }
}

void HttpClientSink::on_response(const http_response_callback_info_t & callback_info)
{
    push_download_message(callback_info);
}

void HttpClientSink::handle_download_message()
{
    bool open_ok = false;
    std::ofstream ofs("callback.txt", std::ios::trunc);
    if (ofs.is_open())
    {
        DBG_LOG("open callback.txt success");
        open_ok = true;
    }
    else
    {
        RUN_LOG_ERR("open callback.txt failure");
        open_ok = false;
    }

    while (m_is_running)
    {
        http_response_callback_info_t callback_info;
        if (!pop_download_message(callback_info))
        {
            Stupid::Base::stupid_ms_sleep(1000);
            continue;
        }

        handle_message(callback_info, ofs);
    }

    if (open_ok)
    {
        DBG_LOG("close callback.txt");
        ofs.close();
    }
}

void HttpClientSink::push_download_message(const http_response_callback_info_t & callback_info)
{
    guard_t guard(m_download_message_locker);

    m_download_message_list.push_back(callback_info);
}

bool HttpClientSink::pop_download_message(http_response_callback_info_t & callback_info)
{
    guard_t guard(m_download_message_locker);

    if (!m_download_message_list.empty())
    {
        callback_info = m_download_message_list.front();
        m_download_message_list.pop_front();
        return(true);
    }

    return(false);
}

void HttpClientSink::handle_message(const http_response_callback_info_t & callback_info, std::ofstream & ofs)
{
    ofs << std::endl;

    ofs << "[" << callback_info.url_request << ", " << callback_info.save_pathname << "]";

    switch (callback_info.error_code)
    {
        case http_response_callback_error_t::callback_message_response_success:
            ofs << "    " << "success" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_response_2xx_failure:
            ofs << "    " << "2xx failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_response_3xx_failure:
            ofs << "    " << "3xx failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_response_4xx_failure:
            ofs << "    " << "4xx failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_response_5xx_failure:
            ofs << "    " << "5xx failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_response_xxx_failure:
            ofs << "    " << "xxx failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_libcurl_init_failure:
            ofs << "    " << "libcurl init failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_libcurl_perform_failure:
            ofs << "    " << "libcurl perform failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_libcurl_getinfo_failure:
            ofs << "    " << "libcurl getinfo failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_argument_invalid:
            ofs << "    " << "argument invalid" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_get_message_digest_failure:
            ofs << "    " << "get message digest failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_create_file_failure:
            ofs << "    " << "create file failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_rename_file_failure:
            ofs << "    " << "rename file failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_unzip_file_failure:
            ofs << "    " << "unzip file failure" << std::endl;
            break;
        case http_response_callback_error_t::callback_message_download_been_stopped:
            ofs << "    " << "download been stopped" << std::endl;
            break;
        default:
            ofs << "    " << "<unknown message>" << std::endl;
            break;
    }

    ofs << std::endl;
}
