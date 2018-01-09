#ifndef HTTP_CLIENT_SINK_H
#define HTTP_CLIENT_SINK_H


#include <list>
#include <string>
#include <fstream>
#include "http_client.h"
#include "base/locker/locker.h"
#include "base/thread/thread.h"
#include "base/utility/guard.h"
#include "base/utility/uncopy.h"

class HttpClientSink : public IHttpClientSink, private Stupid::Base::Uncopy
{
public:
    HttpClientSink();
    virtual ~HttpClientSink();

public:
    bool init();
    void exit();

public:
    void handle_download_message();

private:
    virtual void on_response(const http_response_callback_info_t & callback_info) override;

private:
    void push_download_message(const http_response_callback_info_t & callback_info);
    bool pop_download_message(http_response_callback_info_t & callback_info);

private:
    void handle_message(const http_response_callback_info_t & callback_info, std::ofstream & ofs);

private:
    void clear();

private:
    typedef Stupid::Base::Thread                        thread_t;
    typedef Stupid::Base::ThreadLocker                  locker_t;
    typedef Stupid::Base::Guard<locker_t>               guard_t;
    typedef std::list<http_response_callback_info_t>    download_callback_list_t;

private:
    bool                                            m_is_running;
    download_callback_list_t                        m_download_message_list;
    locker_t                                        m_download_message_locker;
    thread_t                                        m_download_message_handler;
};


#endif // HTTP_CLIENT_SINK_H
