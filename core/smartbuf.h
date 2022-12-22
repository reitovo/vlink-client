#ifndef SMARTBUF_H
#define SMARTBUF_H

#include <string>
#include <functional>
#include <map>
#include <memory>
#include <smartbuf.pb.h>

class smartbuf
{
    int txSeq = 0;
    int maxSize = 0;
    std::function<void (const std::string &)> sender;
    std::function<void (const std::string &)> receiver;

    std::map<int, std::map<int, std::unique_ptr<SmartBuf>>> queue;

public:
    smartbuf(int maxSize,
             std::function<void (const std::string &)> sender,
             std::function<void (const std::string &)> receiver);

    void send(const std::string buf);
    void onReceive(const std::string buf);
};

#endif // SMARTBUF_H
