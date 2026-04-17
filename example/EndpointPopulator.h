#include <nlohmann/json.hpp>
#include "../include/HttpServer.h"
#include "../include/MetricManager.h"
#include "../include/Namespace.h"
AtlasHttpNamespaceBegin
struct EndpointPopulator
{
    void operator()(Atlas::Http::HTTPServer & server)
    {
        auto readinessResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
            nlohmann::json json;
            json["status"] = "ok";
            responder->_connectionContext._response->result(boost::beast::http::status::ok);
            responder->_connectionContext._response->set(boost::beast::http::field::content_type, "application/json");
            responder->_connectionContext._response->set(boost::beast::http::field::keep_alive, "false");
            responder->_connectionContext._response->body() = json.dump();
        };
        server.AddRequestHandler("/ready", boost::beast::http::verb::get, readinessResponder);
        auto metricsResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
            nlohmann::json json;
            json["CurrentHttpConnections"] = MetricManager::The()._currentHttpConnections.load();
            json["FinishedHttpConnections"] = MetricManager::The()._finishedHttpConnections.load();
            json["HttpRequests"] = MetricManager::The()._httpRequests.load();
            json["HttpResponses"] = MetricManager::The()._httpResponses.load();
            auto sharedReader = responder->_weakReader.lock();
            if(!sharedReader)
            {
                return;
            }
            boost::asio::post(responder->_connectionContext._strand, [sharedReader, responder = std::shared_ptr<AsyncMethodResponder>(responder), json = std::move(json)]() mutable
            {
                responder->_connectionContext._response->result(boost::beast::http::status::ok);
                responder->_connectionContext._response->set(boost::beast::http::field::content_type, "application/json");
                responder->_connectionContext._response->set(boost::beast::http::field::keep_alive, "false");
                responder->_connectionContext._response->body() = json.dump();
            });
        };
        server.AddRequestHandler("/metrics", boost::beast::http::verb::get, metricsResponder);
    }
};
AtlasHttpNamespaceEnd