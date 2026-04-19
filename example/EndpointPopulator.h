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
            responder->_connectionContext.lock()->_response->result(boost::beast::http::status::ok);
            responder->_connectionContext.lock()->_response->set(boost::beast::http::field::content_type, "application/json");
            responder->_connectionContext.lock()->_response->set(boost::beast::http::field::keep_alive, "false");
            responder->_connectionContext.lock()->_response->body() = json.dump();
        };
        server.AddRequestHandler("/ready", boost::beast::http::verb::get, readinessResponder);
        auto metricsResponder = [](const std::shared_ptr<AsyncMethodResponder> & responder)
        {
            nlohmann::json json;
            json["CurrentHttpConnections"] = MetricManager::The()._currentHttpConnections.load();
            json["FinishedHttpConnections"] = MetricManager::The()._finishedHttpConnections.load();
            json["HttpRequests"] = MetricManager::The()._httpRequests.load();
            json["HttpResponses"] = MetricManager::The()._httpResponses.load();
			json["CurrentWebsocketSessions"] = MetricManager::The()._currentWebsocketSessions.load();
			json["ClosedWebsocketSessions"] = MetricManager::The()._closedWebsocketSessions.load();
            auto liveKeeper = responder->GetAliveKeeper();
            if(!liveKeeper.IsAlive())
            {
                return;
            }
            boost::asio::post(responder->_connectionContext.lock()->_strand, [liveKeeper, json = std::move(json)]() mutable
            {
                auto responder = liveKeeper._responder; 
                responder->_connectionContext.lock()->_response->result(boost::beast::http::status::ok);
                responder->_connectionContext.lock()->_response->set(boost::beast::http::field::content_type, "application/json");
                responder->_connectionContext.lock()->_response->set(boost::beast::http::field::keep_alive, "false");
                responder->_connectionContext.lock()->_response->body() = json.dump();
            });
        };
        server.AddRequestHandler("/metrics", boost::beast::http::verb::get, metricsResponder);

		auto onWebSocketConnect = [](const std::shared_ptr<WebSocketSession>& session)
		{
			Logger(Info) << "WebSocket client connected";
		};

		auto onWebSocketText = [](const std::shared_ptr<WebSocketSession>& session, const std::string& data)
		{
			Logger(Info) << "WebSocket text received: " << data;
			auto aliveKeeper = session->GetAliveKeeper();
			boost::asio::post(session->GetConnectionContext().lock()->_strand, [aliveKeeper, dataCpy = data]()
			{
				if (!aliveKeeper.IsAlive())
				{
					return;
				}
				Logger(Info) << "LateEcho: " << dataCpy;
				for (int i = 0; i < 10; i++)
				{
					boost::asio::post(aliveKeeper._session->GetConnectionContext().lock()->_strand, [aliveKeeper, dataCpy, i]()
					{
						aliveKeeper._session->SendText("LateEcho: " + std::to_string(i) + " " + dataCpy);
					});
				}

			});
			session->SendText("Echo: " + data);
		};

		auto onWebSocketBinary = [](const std::shared_ptr<WebSocketSession>& session, const std::string& data)
		{
			Logger(Info) << "WebSocket binary received, echoing back";
			session->SendBinary(data);
		};

		auto onWebSocketDisconnect = [](const std::shared_ptr<WebSocketSession>& session)
		{
			Logger(Info) << "WebSocket client disconnected";
		};

		WebSocketSession::WebSocketHandlers wsHandlers{
			onWebSocketConnect,
			onWebSocketBinary,
			onWebSocketText,
			onWebSocketDisconnect
		};

		server.AddWebSocketHandler("/ws", wsHandlers);

    }
};
AtlasHttpNamespaceEnd