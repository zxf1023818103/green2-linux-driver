// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <aidl/android/hardware/wifi/NanEnableRequest.h>
#include <aidl/android/hardware/wifi/NanPublishRequest.h>
#include <aidl/android/hardware/wifi/NanSubscribeRequest.h>
namespace green2 {
using Bytes=std::vector<uint8_t>;
using Attrs=std::map<uint16_t,Bytes>;
Bytes attr(uint16_t,const void*,size_t);
template<class T> Bytes attr(uint16_t n,const T& v){return attr(n,&v,sizeof(v));}
Bytes textAttr(uint16_t,const std::string&);
Bytes nest(uint16_t,const Bytes&);
void append(Bytes&,const Bytes&);
Attrs parse(const void*,size_t);
template<class T> T value(const Attrs&a,uint16_t n,T fallback={}) {auto i=a.find(n);T v=fallback;if(i!=a.end()&&i->second.size()==sizeof(T)) __builtin_memcpy(&v,i->second.data(),sizeof(T));return v;}
std::array<uint8_t,6> mac(const std::string&);
int setMac(const std::string&,const std::array<uint8_t,6>&);
int linkUp(const std::string&,bool);
class Netlink {
 int fd_=-1; uint32_t seq_=0; std::mutex lock_;
public:
 int family=0;std::map<std::string,int> groups;
 explicit Netlink(int proto);~Netlink();
 int request(uint16_t type,uint16_t flags,const Bytes&,const std::function<void(const void*,size_t)>& cb={});
 int connectGeneric();
 int command(uint8_t cmd,const Bytes&,const std::function<void(const Attrs&)>& cb={});
 int join(int group);int receive(const std::function<void(uint8_t,const Attrs&)>&,int timeout);
};
class Backend {
 Netlink nl_,route_,events_; std::atomic<bool> stop_{false};std::thread thread_;
public:
 unsigned radioIndex=0;int phy=-1;uint64_t wdev=0;bool enabled=false;
 std::function<void(uint8_t,const Attrs&)> event;
 Backend();~Backend();void stopEvents();
 int init();int enable(const aidl::android::hardware::wifi::NanEnableRequest&);int config(uint8_t master);
 int disable();int release();
 int publish(const aidl::android::hardware::wifi::NanDiscoveryCommonConfig&,bool subscribe,bool active,uint8_t pubType,uint8_t& id,uint64_t& cookie);
 int cancel(uint64_t cookie);int vendor(uint32_t cmd,const void*,size_t,Bytes*reply=nullptr);
 int bridge(const std::string& name,bool create);
 int neighbor(const std::array<uint8_t,16>&address,const std::array<uint8_t,6>&peer,bool add);
 int followup(uint8_t local,uint8_t peer,const std::array<uint8_t,6>& dst,const Bytes& info);
};
}
