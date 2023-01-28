/* *
 * @program: NatTypeProb
 *
 * @description: 进行Nat类型探测和公网端口获取，如果在不能访问外网的环境下使用，接口返回会失败
 *               使用方法：
 *               1、调用 Init() 函数进行初始化；
 *               2、调用 GetNatType() 函数探测Nat类型，因需要与服务器多次交互，较为耗时，仅需调用一次；
 *               3、调用 GetNatIpAndPort() 函数获取公网Ip和穿透端口，根据需要可调用多次；
 * @author: 909845
 *
 * @create: 2019-02-22 10:28
***/

#ifndef NATTYPEPROB_NATPROB_H
#define NATTYPEPROB_NATPROB_H

#include <string>
#include "NatTypeProbe/p2p_api.h"
#include <map>

using namespace std;

class __declspec(dllexport) CNatProb
{
public:
    /*
     * 功能:  Nat探测初始化
     * 参数:
     *       @strNatServerAddr      -- Nat服务器地址，可为Ip或者域名，可设置为公网公开的Nat服务器
     *       @uNatServerDefaultPort -- Nat服务访问端口，默认3478
     */
    bool Init(const string& strNatServerAddr, const UInt16 uNatServerDefaultPort = STUN_PORT);


    /*
     * 功能:  Nat类型探测，接口耗时1-2s返回结果，可单独起线程探测，避免阻塞业务线程
     * 参数:  @port                       探测使用的端口对port, port+1，
     *                                   传0则使用随机端口，若端口冲突则自动替换端口尝试，最多连续尝试10次
     *                                   非0则使用指定端，若端口冲突直接失败
     * 返回:  NatType 为
     *       StunTypeOpen                表示本身就在公网，具有独立的Ip地址，可直接使用
     *       StunTypeConeNat             圆锥形                        可使用
     *       StunTypeRestrictedNat       IP限制型                      可使用
     *       StunTypePortRestrictedNat   端口限制型                     可使用
     *       StunTypeSymNat              对称型                        需进行流量转发或端口预测才可能使用
     *       其他                         认为不可穿透或穿透失败
     */
    NatType GetNatType(UInt16 port = 0);

    /*
     * 功能:  获取公网IP和端口，接口返回300ms -1s，可单独起线程探测，避免阻塞业务线程
     * 参数:
     *       @strLocalIp                 本地Ip地址，可设为0.0.0.0
     *       @uLocalPort                 需要穿透的本地端口，端口冲突直接返回失败
     *       @strPublicIp                本地Ip对应的公网IP
     *       @uPublicPort                本地端口对应的公网端口
     * 返回:
     *       true                        成功
     *       false                       失败
     */
    bool GetNatIpAndPort(const string &strLocalIp, const UInt16& uLocalPort, string& strPublicIp, UInt16& uPublicPort);

    /*
     * 根据枚举值描述Nat类型
     */
    static string DescribeNatType(const NatType natType);

private:
    StunAddress4 m_NatServerAddr = {0};//保存Nat服务的域名解析结果

    static const map<NatType, string>  m_mpNatTypeDesc;
};


#endif //NATTYPEPROB_NATPROB_H