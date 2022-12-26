#include "util.h"
#include <d3d11.h>

Elapsed::Elapsed(const QString &name)
{
    this->name = name;
    timer.start();
}

Elapsed::~Elapsed()
{
    if (!ended)
        end();
}

void Elapsed::end()
{
    auto e = timer.nsecsElapsed();
    qDebug() << name << "cost" << e << "ns";
    ended = true;
}

void printDxDebugInfo(ID3D11Device *dev)
{
    ID3D11InfoQueue* debug_info_queue;
    dev->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&debug_info_queue);

    auto hr = debug_info_queue->PushEmptyStorageFilter();

    UINT64 message_count = debug_info_queue->GetNumStoredMessages();

    for(UINT64 i = 0; i < message_count; i++){
        SIZE_T message_size = 0;
        debug_info_queue->GetMessage(i, nullptr, &message_size); //get the size of the message

        D3D11_MESSAGE* message = (D3D11_MESSAGE*) malloc(message_size); //allocate enough space
        hr = debug_info_queue->GetMessage(i, message, &message_size); //get the actual message

        //do whatever you want to do with it
        qDebug() << "Direct3D 11:" << QString::fromStdString(std::string(message->pDescription, message->DescriptionByteLength));

        free(message);
    }

    debug_info_queue->ClearStoredMessages();
}

void FpsCounter::add(long nsConsumed)
{
    auto sec = QDateTime::currentSecsSinceEpoch();
    if (sec != currentSec) {
        currentSec = sec;
        lastCount = count;
        count = 0;
    }
    count++;
    nsAverage = (nsAverage * 31 + nsConsumed) / 32;
    lastAddSec = sec;
}

QString FpsCounter::stat()
{
    auto sec = QDateTime::currentSecsSinceEpoch();
    if (sec - lastAddSec > 3) {
        return QString("FPS: - Cost: - us");
    }
    return QString("FPS: %2 Cost: %3 us").arg(lastCount).arg(nsAverage / 1000);
}

double FpsCounter::fps()
{
    auto sec = QDateTime::currentSecsSinceEpoch();
    if (sec - lastAddSec > 3) {
        return 0.0;
    }

    return 1000000000.0 / nsAverage;
}
