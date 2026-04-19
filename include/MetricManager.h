//
// Created by mehme on 12/14/2025.
//

#ifndef ATLASHTTP_METRICMANAGER_H
#define ATLASHTTP_METRICMANAGER_H
#include <atomic>
#include "Namespace.h"
AtlasHttpNamespaceBegin



struct MetricManager
{
    static MetricManager& The()
    {
        static MetricManager m{};
        return m;
    }

    std::atomic<int> _currentHttpConnections = 0;
    std::atomic<int> _finishedHttpConnections = 0;
    std::atomic<int> _httpResponses = 0;
    std::atomic<int> _httpRequests = 0;
    std::atomic<int> _currentWebsocketSessions = 0;
    std::atomic<int> _closedWebsocketSessions = 0;
};

AtlasHttpNamespaceEnd
#endif //ATLASHTTP_METRICMANAGER_H