#include <cstring>
#include <string>
#include <iostream>
#include "http_client.h"
#include "http_client_sink.h"
#include "base/log/log.h"
#include "base/config/xml.h"

static bool parse_configuration(size_t & max_downloader_count, std::list<std::string> & get_data_task_list, std::list<http_download_request_t> & download_task_list)
{
    Stupid::Base::Xml xml;

    if (!xml.load("task.xml"))
    {
        RUN_LOG_CRI("xml::load(%s) failure", "task.xml");
        return(false);
    }

    if (!xml.into_element("root"))
    {
        RUN_LOG_ERR("xml::into_element(%s) failure", "root");
        return(false);
    }

    if (!xml.into_element("http_client"))
    {
        RUN_LOG_ERR("xml::into_element(%s) failure", "http_client");
        return(false);
    }

    if (!xml.get_element("max_downloader_count", max_downloader_count))
    {
        RUN_LOG_ERR("xml::get_element(%s) failure", "max_downloader_count");
        return(false);
    }

    if (!xml.outof_element())
    {
        RUN_LOG_ERR("xml::outof_element(%s) failure", "http_client");
        return(false);
    }

    if (!xml.into_element("get_data_tasks"))
    {
        RUN_LOG_ERR("xml::into_element(%s) failure", "get_data_tasks");
        return(false);
    }

    std::string get_data_task_content;
    while (xml.get_sub_document(get_data_task_content))
    {
        Stupid::Base::Xml sub_xml;

        if (!sub_xml.set_document(get_data_task_content.c_str()))
        {
            RUN_LOG_ERR("xml::set_document(%s) failure", get_data_task_content.c_str());
            return(false);
        }

        if (!sub_xml.into_element("task"))
        {
            RUN_LOG_ERR("xml::into_element(%s) failure", "task");
            return(false);
        }

        std::string url_request;
        if (!sub_xml.get_element("url_request", url_request))
        {
            RUN_LOG_ERR("xml::get_element(%s) failure", "url_request");
            return(false);
        }

        if (!sub_xml.outof_element())
        {
            RUN_LOG_ERR("xml::outof_element(%s) failure", "task");
            return(false);
        }

        get_data_task_list.push_back(url_request);
    }

    if (!xml.outof_element())
    {
        RUN_LOG_ERR("xml::outof_element(%s) failure", "get_data_tasks");
        return(false);
    }

    if (!xml.into_element("download_tasks"))
    {
        RUN_LOG_ERR("xml::into_element(%s) failure", "download_tasks");
        return(false);
    }

    std::string download_task_content;
    while (xml.get_sub_document(download_task_content))
    {
        http_download_request_t download_task;
        Stupid::Base::Xml sub_xml;

        if (!sub_xml.set_document(download_task_content.c_str()))
        {
            RUN_LOG_ERR("xml::set_document(%s) failure", download_task_content.c_str());
            return(false);
        }

        if (!sub_xml.into_element("task"))
        {
            RUN_LOG_ERR("xml::into_element(%s) failure", "task");
            return(false);
        }

        size_t need_unzip = 0;
        if (!sub_xml.get_element("need_unzip", need_unzip))
        {
            RUN_LOG_ERR("xml::get_element(%s) failure", "need_unzip");
            return(false);
        }
        download_task.need_unzip = (0 != need_unzip);

        std::string url_request;
        if (!sub_xml.get_element("url_request", url_request))
        {
            RUN_LOG_ERR("xml::get_element(%s) failure", "url_request");
            return(false);
        }
        strncpy(download_task.url_request, url_request.c_str(), sizeof(download_task.url_request));

        std::string hash_request;
        if (!sub_xml.get_element("hash_request", hash_request))
        {
            RUN_LOG_ERR("xml::get_element(%s) failure", "hash_request");
            return(false);
        }
        strncpy(download_task.hash_request, hash_request.c_str(), sizeof(download_task.hash_request));

        std::string save_pathname;
        if (!sub_xml.get_element("save_pathname", save_pathname))
        {
            RUN_LOG_ERR("xml::get_element(%s) failure", "save_pathname");
            return(false);
        }
        strncpy(download_task.save_pathname, save_pathname.c_str(), sizeof(download_task.save_pathname));

        std::string message_digest;
        if (!sub_xml.get_element("message_digest", message_digest))
        {
            RUN_LOG_ERR("xml::get_element(%s) failure", "message_digest");
            return(false);
        }
        strncpy(download_task.message_digest, message_digest.c_str(), sizeof(download_task.message_digest));

        if (!sub_xml.outof_element())
        {
            RUN_LOG_ERR("xml::outof_element(%s) failure", "task");
            return(false);
        }

        download_task_list.push_back(download_task);
    }

    if (!xml.outof_element())
    {
        RUN_LOG_ERR("xml::outof_element(%s) failure", "download_tasks");
        return(false);
    }

    if (!xml.outof_element())
    {
        RUN_LOG_ERR("xml::outof_element(%s) failure", "root");
        return(false);
    }

    return(true);
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

int main(int, char * [])
{
    size_t max_downloader_count = 0;
    std::list<std::string> get_data_task_list;
    std::list<http_download_request_t> download_task_list;
    if (!parse_configuration(max_downloader_count, get_data_task_list, download_task_list))
    {
        std::cout << "parse configuration failure" << std::endl;
        return(1);
    }

    HttpClientSink http_client_sink;
    if (!http_client_sink.init())
    {
        std::cout << "http client sink init failure" << std::endl;
        return(2);
    }

    IHttpClient * http_client = create_http_client();
    if (nullptr == http_client)
    {
        std::cout << "create http client failure" << std::endl;
        return(3);
    }

    if (!http_client->init(max_downloader_count))
    {
        std::cout << "http client init failure" << std::endl;
        return(4);
    }

    size_t file_size = 0;
    size_t status_code = 0;
    size_t error_code = 0;
    if (http_client->get_file_size("http://www.rayvision.com/client/newClient/plugins.xml", file_size, status_code, error_code))
    {
        std::cout << "get file size success" << " size(" << file_size << ")" << std::endl;
    }
    else
    {
        std::cout << "get file size failure" << std::endl;
    }

    for (std::list<http_download_request_t>::iterator iter = download_task_list.begin(); download_task_list.end() != iter; ++iter)
    {
        iter->response_sink = &http_client_sink;
        http_client->post_download_request(*iter);
    }

    for (std::list<std::string>::iterator iter = get_data_task_list.begin(); get_data_task_list.end() != iter; ++iter)
    {
        std::string url_response;
        size_t url_status_code = 0;
        size_t url_error_code = 0;
        if (http_client->get_data(iter->c_str(), get_data_storage, &url_response, url_status_code, url_error_code))
        {
            std::cout << "request:" << std::endl << "\t" << *iter << std::endl << "success, response:" << std::endl << "\t" << url_response << std::endl << std::endl;
        }
        else
        {
            std::cout << "request:" << std::endl << "\t" << *iter << std::endl << "failure, response code:" << std::endl << "\t" << url_error_code << std::endl << std::endl;
        }
    }

    while (true)
    {
        std::cin.sync();
        std::cin.clear();
        std::string command;
        std::cin >> command;
        if ("exit" == command)
        {
            break;
        }
        else if ("stop" == command)
        {
            if (!download_task_list.empty())
            {
                http_client->stop_download_request(download_task_list.front());
                download_task_list.pop_front();
            }
        }
    }

    http_client->exit();

    destroy_http_client(http_client);

    http_client = nullptr;

    http_client_sink.exit();

    return(0);
}
