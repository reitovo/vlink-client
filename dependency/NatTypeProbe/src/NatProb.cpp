/* *
 * @program: NatTypeProb
 *
 * @description: ${description}
 *
 * @author: 909845
 *
 * @create: 2019-02-22 10:28
***/
#include <cstring>
#include <sstream>
#include "NatTypeProbe/NatProb.h"
#include "NatTypeProbe/udp.h"

static const int TRY_NUM = 10; //穿透公网端口事假超时次数

const map<NatType, string> CNatProb::m_mpNatTypeDesc = {
        {StunTypeUnavailable, "StunTypeUnavailable"},
        {StunTypeFailure, "StunTypeFailure"},
        {StunTypeOpen, "StunTypeOpen"},
        {StunTypeBlocked, "StunTypeBlocked"},
        {StunTypeIndependentFilter, "StunTypeIndependentFilter"},
        {StunTypeDependentFilter, "StunTypeDependentFilter"},
        {StunTypePortDependedFilter, "StunTypePortDependedFilter"},
        {StunTypeDependentMapping, "StunTypeDependentMapping"},
        {StunTypeConeNat, "StunTypeConeNat"},
        {StunTypeRestrictedNat, "StunTypeRestrictedNat"},
        {StunTypePortRestrictedNat, "StunTypePortRestrictedNat"},
        {StunTypeSymNat, "StunTypeSymNat"},
        {StunTypeFirewall, "StunTypeFirewall"},
        {StunTypeSymFirewall, "StunTypeSymFirewall"},
};

bool CNatProb::Init(const string &strNatServerAddr, const UInt16 uNatServerDefaultPort)
{
    initNetwork();
    return stunParseHostName((char*)strNatServerAddr.c_str(), m_NatServerAddr.addr, m_NatServerAddr.port, uNatServerDefaultPort);
}

NatType CNatProb::GetNatType(UInt16 port)
{
    NatType natType = StunTypeUnknown;
    const int nMaxTry = (port > 0) ? 1 : 10;//传入端口非0，若端口冲突直接退出；若端口为0，则使用随机端口，端口冲突最多尝试10次
    for (int i = 0; i < nMaxTry; ++i)
    {
        natType = stunNatType(m_NatServerAddr, false, nullptr, nullptr, port, nullptr);
        if (natType != StunTypeFailure)
        {
            break;
        }
    }
    return natType;
}

bool CNatProb::GetNatIpAndPort(const string &strLocalIp, const UInt16 &uLocalPort, string &strPublicIp, UInt16 &uPublicPort)
{
    bool bRet = false;
    Socket myFd = INVALID_SOCKET;
    do
    {
        if (strLocalIp.empty() || uLocalPort == 0)
        {
            cout<<"strLocalIp and uLocalPort must not be null."<<endl;
            break;
        }
        StunAddress4 localAddr = {0};
        if (!stunParseHostName((char *) strLocalIp.c_str(), localAddr.addr, localAddr.port, uLocalPort))
        {
            cout << "stunParseHostName failed.  strLocalIp = " << strLocalIp << " uLocalPort = " << uLocalPort << endl;
            break;
        };
        UInt32 interfaceIp = localAddr.addr;
        myFd = openPort(uLocalPort, interfaceIp, false);
        if (INVALID_SOCKET == myFd)
        {
            cerr << "Some problem opening port/interface to send on" << endl;
            break;
        }
        StunAtrString username = {0};
        StunAtrString password = {0};
        const unsigned char selectEventFlag = uLocalPort % 127;// select事件的标识符, 使用本地端口号作为事假标识
        int nCount = 0;
        while (nCount < TRY_NUM)//最多等待5次超时
        {
            struct timeval tv;
            fd_set fdSet;
#ifdef WIN32
            unsigned int fdSetSize = 0;
#else
            int fdSetSize = 0;
#endif
            FD_ZERO(&fdSet); fdSetSize=0;
            FD_SET(myFd,&fdSet); fdSetSize = (myFd+1>fdSetSize) ? myFd+1 : fdSetSize;
            tv.tv_sec=0;
            tv.tv_usec=100*1000; // 100ms 超时
            if (0 ==  nCount)
            {
                tv.tv_usec = 0;
            }
            int  nSelect = select(fdSetSize, &fdSet, NULL, NULL, &tv);
            int errNo = getErrno();
            if (nSelect == SOCKET_ERROR)//select 错误
            {
                cerr << "Error " << errNo << " " << strerror(errNo) << " in select" << endl;
                break;
            }
            else if (0 == nSelect) // select 超时
            {
                nCount++;
                StunMessage req;
                memset(&req, 0, sizeof(StunMessage));
                stunBuildReqSimple(&req, username, false , false , selectEventFlag);

                char buf[STUN_MAX_MESSAGE_SIZE];
                int len = STUN_MAX_MESSAGE_SIZE;
                len = stunEncodeMessage( req, buf, len, password, false);
                sendMessage(myFd, buf, len, m_NatServerAddr.addr, m_NatServerAddr.port, false);

                // add some delay so the packets don't get sent too quickly
#ifdef WIN32 // TODO - should fix this up in windows
                 clock_t now = clock();
             //    assert( CLOCKS_PER_SEC == 1000 );
                 while ( clock() <= now+10 ) { };
#else
                usleep(10*1000);
#endif
            }
            else // 收到select事件
            {
                if ( myFd!=INVALID_SOCKET )
                {
                    if (FD_ISSET(myFd, &fdSet))
                    {
                        char msg[STUN_MAX_MESSAGE_SIZE];
                        int msgLen = sizeof(msg);
                        StunAddress4 from;
                        getMessage(myFd, msg, &msgLen, &from.addr, &from.port, false);
                        StunMessage resp;
                        memset(&resp, 0, sizeof(StunMessage));
                        stunParseMessage(msg, msgLen, resp, false);
                        if (selectEventFlag == resp.msgHdr.id.octet[0])
                        {
                            const UInt32 pubAddr = resp.mappedAddress.ipv4.addr;
                            const UInt16 pubPort = resp.mappedAddress.ipv4.port;

                            stringstream ss;
                            ss << ((pubAddr >> 24) & 0xFF) << "." << ((pubAddr >> 16) & 0xFF) << "." << ((pubAddr >> 8) & 0xFF) << "." << ((pubAddr >> 0) & 0xFF);
                            strPublicIp = ss.str();
                            uPublicPort = pubPort;
                            break;
                        }
                    }
                }
            }
        }
        bRet = (nCount < TRY_NUM) ? true : false;
    }while (0);

    if (INVALID_SOCKET != myFd)
    {
        closesocket(myFd);
    }
    return bRet;
}

string CNatProb::DescribeNatType(const NatType natType)
{
    if (m_mpNatTypeDesc.find(natType) != m_mpNatTypeDesc.end())
    {
        return m_mpNatTypeDesc.at(natType);
    }
    return m_mpNatTypeDesc.at(StunTypeUnknown);
}
