#include "smart_buf.h"
#include "smartbuf.pb.h"
#include <memory>
#include <QDebug>
#include <brotli/decode.h>
#include <brotli/encode.h>

smart_buf::smart_buf(int maxSize, std::function<void (const std::string &)> sender, std::function<void (const std::string &)> receiver)
{
    this->maxSize = maxSize;
    this->sender = sender;
    this->receiver = receiver;
}

void smart_buf::send(const std::string buf)
{
    txSeq++;
    auto maxPacketSize = maxSize - 24;
    auto part = 1;
    auto total = buf.size() / maxPacketSize + 1;
    for (int i = 0; i < buf.size(); i += maxPacketSize) {
        auto remain = buf.size() - i;
        auto packetSize = remain > maxPacketSize ? maxPacketSize : remain;

        auto msg = std::make_shared<SmartBuf>();
        msg->set_seq(txSeq);
        msg->set_part(part++);
        msg->set_total(total);
        msg->mutable_data()->assign(buf.data() + i, packetSize);

        sender(msg->SerializeAsString());
    }
}

void smart_buf::onReceive(const std::string buf)
{
    auto msg = std::make_unique<SmartBuf>();
    if (msg->ParseFromArray(buf.data(), buf.size())) {
        auto total = msg->total();
        if (total == 1) {
            receiver(msg->data());
        } else {
            auto seq = msg->seq();
            auto part = msg->part();
            auto& map = queue[seq];
            map[part] = std::move(msg);

            for (int i = 1; i <= total; ++i) {
                if (!map.contains(i))
                    return;
            }

            auto data = std::string();
            for (int i = 1; i <= total; ++i) {
                auto a = std::move(map[i]);
                auto& d = a->data();
                data.append(d.data(), d.size());
            }

            receiver(data);
            queue.erase(seq);
        }
    } else {
        qDebug() << "invalid smartbuf";
    }
}
