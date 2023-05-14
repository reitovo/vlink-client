#ifndef SMARTBUF_H
#define SMARTBUF_H

#include <string>
#include <functional>
#include <map>
#include <memory>
#include <proto/vts.pb.h>

class smart_buf
{
    int txSeq = 0;
    int maxSize = 0;
    std::function<void (const std::string &)> sender;
    std::function<void (const std::string &)> receiver;

    std::map<uint32_t, std::map<uint32_t, std::unique_ptr<vts::SmartBuf>>> queue;

public:
    smart_buf(int maxSize,
              std::function<void (const std::string &)> sender,
              std::function<void (const std::string &)> receiver);

    void send(const std::string buf);
    void onReceive(const std::string buf);
};

#endif // SMARTBUF_H
