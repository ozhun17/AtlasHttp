//
// Created by mehme on 12/14/2025.
//

#ifndef ATLASHTTP_METRICMANAGER_H
#define ATLASHTTP_METRICMANAGER_H
#include "Namespace.h"
AtlasNamespaceBegin



struct MetricManager
{
    static MetricManager& The()
    {
        static MetricManager m{};
        return m;
    }

    int _httpResponses = 0;
    int _httpRequests = 0;
};

AtlasNamespaceEnd
#endif //ATLASHTTP_METRICMANAGER_H