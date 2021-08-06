#include <thread>
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
};

#if defined TRINITYCORE || defined AZEROTHCORE
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include "libs/httplib.h"
#include "HttpManager.h"
#include "LuaEngine.h"

HttpWorkItem::HttpWorkItem(int funcRef, const std::string& httpVerb, const std::string& url, const std::string& body, const std::string& contentType, const httplib::Headers& headers)
    : funcRef(funcRef),
    httpVerb(httpVerb),
    url(url),
    body(body),
    contentType(contentType),
    headers(headers)
{ }

HttpResponse::HttpResponse(int funcRef, int statusCode, const std::string& body, const httplib::Headers& headers)
    : funcRef(funcRef),
    statusCode(statusCode),
    body(body),
    headers(headers)
{ }

HttpManager::HttpManager()
    : httpWorkQueue(16),
    httpResponseQueue(16),
    startedHttpWorkerThread(false),
    httpCancelationToken(false),
    httpCondVar(),
    httpCondVarMutex(),
    parseUrlRegex("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?")
{
    StartHttpWorker();
}

void HttpManager::PushRequest(HttpWorkItem* item)
{
    std::unique_lock<std::mutex> lock(httpCondVarMutex);
    httpWorkQueue.push(item);
    httpCondVar.notify_one();
}

void HttpManager::StartHttpWorker()
{
    while (httpWorkQueue.front())
    {
        httpWorkQueue.pop();
    }
    while (httpResponseQueue.front())
    {
        httpResponseQueue.pop();
    }

    if (!startedHttpWorkerThread)
    {
        httpWorkerThread = std::thread(&HttpManager::HttpWorkerThread, this);
        startedHttpWorkerThread = true;
    }
}

void HttpManager::HttpWorkerThread()
{
    while (true)
    {
        {
            std::unique_lock<std::mutex> lock(httpCondVarMutex);
            httpCondVar.wait(lock, [&] { return httpWorkQueue.front(); });
        }

        if (!httpWorkQueue.front())
        {
            continue;
        }
        if (httpCancelationToken)
        {
            break;
        }

        HttpWorkItem* req = *httpWorkQueue.front();
        httpWorkQueue.pop();
        if (!req)
        {
            continue;
        }

        try
        {
            std::string host;
            std::string path;

            if (!ParseUrl(req->url, host, path)) {
                ELUNA_LOG_ERROR("[Eluna]: Could not parse URL %s", req->url.c_str());
                continue;
            }

            httplib::Client cli(host);
            cli.set_connection_timeout(0, 3000000); // 3 seconds
            cli.set_read_timeout(5, 0); // 5 seconds
            cli.set_write_timeout(5, 0); // 5 seconds

            httplib::Result res = DoRequest(cli, req, path);
            httplib::Error err = res.error();
            if (err != httplib::Error::Success)
            {
                ELUNA_LOG_ERROR("[Eluna]: HTTP request error: %s", httplib::to_string(err).c_str());
                continue;
            }

            if (res->status == 301)
            {
                std::string location = res->get_header_value("Location");
                std::string host;
                std::string path;

                if (!ParseUrl(location, host, path))
                {
                    ELUNA_LOG_ERROR("[Eluna]: Could not parse URL after redirect: %s", location.c_str());
                    continue;
                }
                httplib::Client cli2(host);
                cli2.set_connection_timeout(0, 3000000); // 3 seconds
                cli2.set_read_timeout(5, 0); // 5 seconds
                cli2.set_write_timeout(5, 0); // 5 seconds
                res = DoRequest(cli2, req, path);
            }

            httpResponseQueue.push(new HttpResponse(req->funcRef, res->status, res->body, res->headers));
        }
        catch (const std::exception& ex)
        {
            ELUNA_LOG_ERROR("[Eluna]: HTTP request error: %s", ex.what());
        }

        delete req;
    }
}

httplib::Result HttpManager::DoRequest(httplib::Client& client, HttpWorkItem* req, const std::string& urlPath)
{
    const char* path = urlPath.c_str();
    if (req->httpVerb == "GET")
    {
        return client.Get(path, req->headers);
    }
    if (req->httpVerb == "HEAD")
    {
        return client.Head(path, req->headers);
    }
    if (req->httpVerb == "POST")
    {
        return client.Post(path, req->headers, req->body, req->contentType.c_str());
    }
    if (req->httpVerb == "PUT")
    {
        return client.Put(path, req->headers, req->body, req->contentType.c_str());
    }
    if (req->httpVerb == "PATCH")
    {
        return client.Patch(path, req->headers, req->body, req->contentType.c_str());
    }
    if (req->httpVerb == "DELETE")
    {
        return client.Delete(path, req->headers);
    }
    if (req->httpVerb == "OPTIONS")
    {
        return client.Options(path, req->headers);
    }

    ELUNA_LOG_ERROR("[Eluna]: HTTP request error: invalid HTTP verb %s", req->httpVerb.c_str());
    return client.Get(path, req->headers);
}

bool HttpManager::ParseUrl(const std::string& url, std::string& host, std::string& path)
{
    std::smatch matches;

    if (!std::regex_search(url, matches, parseUrlRegex))
    {
        return false;
    }

    std::string scheme = matches[2];
    std::string authority = matches[4];
    std::string query = matches[7];
    host = scheme + "://" + authority;
    path = matches[5];
    if (path.empty())
    {
        path = "/";
    }
    path += (query.empty() ? "" : "?") + query;

    return true;
}

void HttpManager::HandleHttpResponses()
{
    while (!httpResponseQueue.empty())
    {
        HttpResponse* res = *httpResponseQueue.front();
        httpResponseQueue.pop();

        if (res == nullptr)
        {
            continue;
        }

        LOCK_ELUNA;

        lua_State* L = Eluna::GEluna->L;

        // Get function
        lua_rawgeti(L, LUA_REGISTRYINDEX, res->funcRef);

        // Push parameters
        Eluna::Push(L, res->statusCode);
        Eluna::Push(L, res->body);
        lua_newtable(L);
        for (const auto& item : res->headers) {
            Eluna::Push(L, item.first);
            Eluna::Push(L, item.second);
            lua_settable(L, -3);
        }

        // Call function
        Eluna::GEluna->ExecuteCall(3, 0);

        luaL_unref(L, LUA_REGISTRYINDEX, res->funcRef);

        delete res;
    }
}