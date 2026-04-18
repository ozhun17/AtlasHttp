#ifndef ATLASHTTP_ASYNCREADER_H
#define ATLASHTTP_ASYNCREADER_H
#include "Namespace.h"
AtlasHttpNamespaceBegin

struct AsyncReader
{
    virtual ~AsyncReader() = default;
    virtual void AsyncReadNextRequest() = 0;
};

AtlasHttpNamespaceEnd

#endif //ATLASHTTP_ASYNCREADERWRITER_H