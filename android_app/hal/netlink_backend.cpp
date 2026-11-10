// SPDX-License-Identifier: Apache-2.0
#include "netlink_backend.h"
#include <linux/genetlink.h>
#include <algorithm>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <linux/if_bridge.h>
#include <linux/neighbour.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cctype>
#include <cstdio>
extern "C" {
#include "green2_sha256.h"
}
namespace green2 {
void append(Bytes&a,const Bytes&b){a.insert(a.end(),b.begin(),b.end());}
Bytes attr(uint16_t id,const void*p,size_t n){Bytes b(NLA_ALIGN(NLA_HDRLEN+n));nlattr h{uint16_t(NLA_HDRLEN+n),id};memcpy(b.data(),&h,NLA_HDRLEN);if(n)memcpy(b.data()+NLA_HDRLEN,p,n);return b;}
Bytes textAttr(uint16_t id,const std::string&s){return attr(id,s.c_str(),s.size()+1);}
Bytes nest(uint16_t id,const Bytes&b){return attr(id|NLA_F_NESTED,b.data(),b.size());}
Attrs parse(const void*p,size_t size){Attrs out;auto b=static_cast<const uint8_t*>(p);while(size>=NLA_HDRLEN){nlattr h;memcpy(&h,b,NLA_HDRLEN);if(h.nla_len<NLA_HDRLEN||h.nla_len>size)break;out[h.nla_type&NLA_TYPE_MASK]=Bytes(b+NLA_HDRLEN,b+h.nla_len);size_t step=NLA_ALIGN(h.nla_len);if(step>size)break;b+=step;size-=step;}return out;}
Netlink::Netlink(int proto){fd_=socket(AF_NETLINK,SOCK_RAW|SOCK_CLOEXEC,proto);if(fd_>=0){sockaddr_nl sa{};sa.nl_family=AF_NETLINK;if(bind(fd_,reinterpret_cast<sockaddr*>(&sa),sizeof(sa))<0){close(fd_);fd_=-1;}}}
Netlink::~Netlink(){if(fd_>=0)close(fd_);}
int Netlink::request(uint16_t type,uint16_t flags,const Bytes&b,const std::function<void(const void*,size_t)>&cb){std::lock_guard<std::mutex>g(lock_);if(fd_<0)return -ENOTCONN;Bytes packet(NLMSG_HDRLEN+b.size());auto*h=reinterpret_cast<nlmsghdr*>(packet.data());h->nlmsg_len=packet.size();h->nlmsg_type=type;h->nlmsg_flags=NLM_F_REQUEST|NLM_F_ACK|flags;h->nlmsg_seq=++seq_;memcpy(packet.data()+NLMSG_HDRLEN,b.data(),b.size());sockaddr_nl dst{};dst.nl_family=AF_NETLINK;if(sendto(fd_,packet.data(),packet.size(),0,reinterpret_cast<sockaddr*>(&dst),sizeof(dst))<0)return -errno;
 for(int attempt=0;attempt<16;attempt++){pollfd f{fd_,POLLIN,0};int r=poll(&f,1,2500);if(r<=0)return r==0?-ETIMEDOUT:-errno;uint8_t buf[65536];ssize_t n=recv(fd_,buf,sizeof(buf),0);if(n<0)return -errno;for(auto*q=reinterpret_cast<nlmsghdr*>(buf);NLMSG_OK(q,n);q=NLMSG_NEXT(q,n)){if(q->nlmsg_seq!=seq_)continue;if(q->nlmsg_type==NLMSG_ERROR){if(q->nlmsg_len<NLMSG_LENGTH(sizeof(nlmsgerr)))return -EPROTO;return reinterpret_cast<nlmsgerr*>(NLMSG_DATA(q))->error;}if(q->nlmsg_type==NLMSG_DONE)return 0;if(cb)cb(NLMSG_DATA(q),NLMSG_PAYLOAD(q,0));}}return -ETIMEDOUT;}
int Netlink::connectGeneric(){Bytes b(sizeof(genlmsghdr));reinterpret_cast<genlmsghdr*>(b.data())->cmd=CTRL_CMD_GETFAMILY;append(b,textAttr(CTRL_ATTR_FAMILY_NAME,"nl80211"));int r=request(GENL_ID_CTRL,0,b,[&](const void*p,size_t n){if(n<GENL_HDRLEN)return;auto a=parse(static_cast<const uint8_t*>(p)+GENL_HDRLEN,n-GENL_HDRLEN);family=value<uint16_t>(a,CTRL_ATTR_FAMILY_ID);if(a.count(CTRL_ATTR_MCAST_GROUPS))for(auto&[k,v]:parse(a[CTRL_ATTR_MCAST_GROUPS].data(),a[CTRL_ATTR_MCAST_GROUPS].size())){auto g=parse(v.data(),v.size());if(g.count(CTRL_ATTR_MCAST_GRP_NAME)){auto&s=g[CTRL_ATTR_MCAST_GRP_NAME];groups[std::string(reinterpret_cast<char*>(s.data()),strnlen(reinterpret_cast<char*>(s.data()),s.size()))]=value<uint32_t>(g,CTRL_ATTR_MCAST_GRP_ID);}}});return r?r:(family?0:-ENOENT);}
int Netlink::command(uint8_t cmd,const Bytes&a,const std::function<void(const Attrs&)>&cb){Bytes b(GENL_HDRLEN);auto*h=reinterpret_cast<genlmsghdr*>(b.data());h->cmd=cmd;h->version=1;append(b,a);return request(family,0,b,[&](const void*p,size_t n){if(cb&&n>=GENL_HDRLEN)cb(parse(static_cast<const uint8_t*>(p)+GENL_HDRLEN,n-GENL_HDRLEN));});}
int Netlink::join(int group){return setsockopt(fd_,SOL_NETLINK,NETLINK_ADD_MEMBERSHIP,&group,sizeof(group))<0?-errno:0;}
int Netlink::receive(const std::function<void(uint8_t,const Attrs&)>&cb,int timeout){pollfd p{fd_,POLLIN,0};int r=poll(&p,1,timeout);if(r<=0)return r;uint8_t b[65536];ssize_t n=recv(fd_,b,sizeof(b),0);if(n<0)return -errno;for(auto*h=reinterpret_cast<nlmsghdr*>(b);NLMSG_OK(h,n);h=NLMSG_NEXT(h,n)){if(h->nlmsg_type!=family||NLMSG_PAYLOAD(h,0)<GENL_HDRLEN)continue;auto*g=reinterpret_cast<genlmsghdr*>(NLMSG_DATA(h));cb(g->cmd,parse(reinterpret_cast<uint8_t*>(g)+GENL_HDRLEN,NLMSG_PAYLOAD(h,0)-GENL_HDRLEN));}return 0;}
std::array<uint8_t,6> mac(const std::string&name){std::array<uint8_t,6>a{};int f=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(f<0)return a;ifreq r{};strncpy(r.ifr_name,name.c_str(),IFNAMSIZ-1);if(ioctl(f,SIOCGIFHWADDR,&r)==0)memcpy(a.data(),r.ifr_hwaddr.sa_data,6);close(f);return a;}
int linkUp(const std::string&name,bool up){int f=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(f<0)return -errno;ifreq r{};strncpy(r.ifr_name,name.c_str(),IFNAMSIZ-1);int v=ioctl(f,SIOCGIFFLAGS,&r);if(!v){r.ifr_flags=up?(r.ifr_flags|IFF_UP):(r.ifr_flags&~IFF_UP);v=ioctl(f,SIOCSIFFLAGS,&r);}int e=v?-errno:0;close(f);return e;}
int setMac(const std::string&name,const std::array<uint8_t,6>&m){int f=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(f<0)return -errno;ifreq r{};strncpy(r.ifr_name,name.c_str(),IFNAMSIZ-1);int ret=ioctl(f,SIOCGIFFLAGS,&r);if(ret<0){int e=-errno;close(f);return e;}short flags=r.ifr_flags;if(flags&IFF_UP){r.ifr_flags=flags&~IFF_UP;if(ioctl(f,SIOCSIFFLAGS,&r)<0){int e=-errno;close(f);return e;}}r.ifr_hwaddr.sa_family=1;memcpy(r.ifr_hwaddr.sa_data,m.data(),6);ret=ioctl(f,SIOCSIFHWADDR,&r);int e=ret?-errno:0;if(flags&IFF_UP){r.ifr_flags=flags;ioctl(f,SIOCSIFFLAGS,&r);}close(f);return e;}
Backend::Backend():nl_(NETLINK_GENERIC),route_(NETLINK_ROUTE),events_(NETLINK_GENERIC){}
void Backend::stopEvents(){stop_=true;if(thread_.joinable())thread_.join();}
Backend::~Backend(){stopEvents();release();}
int Backend::init(){if(wdev)return 0;radioIndex=if_nametoindex("wlan1");int r=nl_.connectGeneric();if(r)return r;Bytes q=attr(NL80211_ATTR_IFINDEX,uint32_t(if_nametoindex("wlan1")));r=nl_.command(NL80211_CMD_GET_INTERFACE,q,[&](const Attrs&a){phy=value<uint32_t>(a,NL80211_ATTR_WIPHY,uint32_t(-1));});if(r||phy<0)return r?r:-ENODEV;
 if(!if_nametoindex("nan0")){Bytes a=attr(NL80211_ATTR_WIPHY,uint32_t(phy));append(a,textAttr(NL80211_ATTR_IFNAME,"nan0"));append(a,attr(NL80211_ATTR_IFTYPE,uint32_t(NL80211_IFTYPE_NAN)));r=nl_.command(NL80211_CMD_NEW_INTERFACE,a);if(r)return r;}
 q=attr(NL80211_ATTR_IFINDEX,uint32_t(if_nametoindex("nan0")));r=nl_.command(NL80211_CMD_GET_INTERFACE,q,[&](const Attrs&a){if(value<uint32_t>(a,NL80211_ATTR_WIPHY,uint32_t(-1))==uint32_t(phy))wdev=value<uint64_t>(a,NL80211_ATTR_WDEV);});if(r||!wdev)return r?r:-ENODEV; // START_NAN must precede netdev UP: cfg80211 uses netif_running here.
 r=events_.connectGeneric();if(r)return r;for(auto name:{"nan","vendor"})if(events_.groups.count(name)){r=events_.join(events_.groups[name]);if(r)return r;}if(!thread_.joinable())thread_=std::thread([this]{while(!stop_){events_.receive([&](uint8_t c,const Attrs&a){if(event)event(c,a);},200);if(!stop_&&event)event(0,{});}});return 0;}
int Backend::enable(const aidl::android::hardware::wifi::NanEnableRequest&m){int r=init();if(r)return r;Bytes a=attr(NL80211_ATTR_WDEV,wdev);append(a,attr(NL80211_ATTR_NAN_MASTER_PREF,uint8_t(m.configParams.masterPref == 0 || uint8_t(m.configParams.masterPref) == 255 ? 128 : m.configParams.masterPref)));uint32_t bands=(m.operateInBand[0]?1:0)|(m.operateInBand[1]?2:0);if(!bands)bands=3;append(a,attr(NL80211_ATTR_BANDS,bands));r=nl_.command(NL80211_CMD_START_NAN,a);if(r)return r;enabled=true;r=linkUp("nan0",true);if(r){release();return r;}return 0;}
int Backend::config(uint8_t pref){if(!enabled)return -ENETDOWN;Bytes a=attr(NL80211_ATTR_WDEV,wdev);append(a,attr(NL80211_ATTR_NAN_MASTER_PREF,uint8_t(pref == 0 || pref == 255 ? 128 : pref)));return nl_.command(NL80211_CMD_CHANGE_NAN_CONFIG,a);}
int Backend::disable(){if(!enabled)return 0;int r=nl_.command(NL80211_CMD_STOP_NAN,attr(NL80211_ATTR_WDEV,wdev));if(!r||r==-ENOENT||r==-ENODEV){enabled=false;int down=linkUp("nan0",false);if(!r&&down&&down!=-ENODEV)return down;}return r;}
int Backend::release(){if(!wdev)return 0;bridge("aware_data0",false);disable();int r=nl_.command(NL80211_CMD_DEL_INTERFACE,attr(NL80211_ATTR_WDEV,wdev));wdev=0;return r;}
static int filters(Bytes&fn,int attrid,const Bytes&encoded){Bytes nested;size_t p=0;unsigned i=1;while(p<encoded.size()){size_t n=encoded[p++];if(n>32||n>encoded.size()-p)return -EINVAL;append(nested,attr(i++,encoded.data()+p,n));p+=n;}if(!nested.empty())append(fn,nest(attrid,nested));return 0;}
int Backend::publish(const aidl::android::hardware::wifi::NanDiscoveryCommonConfig&c,bool sub,bool active,uint8_t pubType,uint8_t&id,uint64_t&cookie){if(!enabled)return -ENETDOWN;if(c.serviceName.empty()||c.serviceName.size()>255||c.serviceSpecificInfo.size()>512)return -EINVAL;Bytes lower=c.serviceName;for(auto&v:lower)v=std::tolower(v);uint8_t hash[32];sha256(lower.data(),lower.size(),hash);Bytes f=attr(NL80211_NAN_FUNC_TYPE,uint8_t(sub?NL80211_NAN_FUNC_SUBSCRIBE:NL80211_NAN_FUNC_PUBLISH));append(f,attr(NL80211_NAN_FUNC_SERVICE_ID,hash,6));if(c.ttlSec)append(f,attr(NL80211_NAN_FUNC_TTL,uint32_t(c.ttlSec)));if(!c.serviceSpecificInfo.empty())append(f,attr(NL80211_NAN_FUNC_SERVICE_INFO,c.serviceSpecificInfo.data(),c.serviceSpecificInfo.size()));if(sub&&active)append(f,attr(NL80211_NAN_FUNC_SUBSCRIBE_ACTIVE,nullptr,0));if(!sub)append(f,attr(NL80211_NAN_FUNC_PUBLISH_TYPE,uint8_t(pubType?NL80211_NAN_SOLICITED_PUBLISH:NL80211_NAN_UNSOLICITED_PUBLISH)));int r=filters(f,NL80211_NAN_FUNC_RX_MATCH_FILTER,c.rxMatchFilter);if(r)return r;r=filters(f,NL80211_NAN_FUNC_TX_MATCH_FILTER,c.txMatchFilter);if(r)return r;Bytes a=attr(NL80211_ATTR_WDEV,wdev);append(a,nest(NL80211_ATTR_NAN_FUNC,f));return nl_.command(NL80211_CMD_ADD_NAN_FUNCTION,a,[&](const Attrs&reply){cookie=value<uint64_t>(reply,NL80211_ATTR_COOKIE);if(reply.count(NL80211_ATTR_NAN_FUNC)){auto n=parse(reply.at(NL80211_ATTR_NAN_FUNC).data(),reply.at(NL80211_ATTR_NAN_FUNC).size());id=value<uint8_t>(n,NL80211_NAN_FUNC_INSTANCE_ID);}});}
int Backend::cancel(uint64_t cookie){Bytes a=attr(NL80211_ATTR_WDEV,wdev);append(a,attr(NL80211_ATTR_COOKIE,cookie));return nl_.command(NL80211_CMD_DEL_NAN_FUNCTION,a);}
int Backend::vendor(uint32_t cmd,const void*p,size_t n,Bytes*reply){if(!wdev)return -ENODEV;Bytes a=attr(NL80211_ATTR_WDEV,wdev);append(a,attr(NL80211_ATTR_VENDOR_ID,uint32_t(0x00e04c)));append(a,attr(NL80211_ATTR_VENDOR_SUBCMD,cmd));append(a,attr(NL80211_ATTR_VENDOR_DATA,p,n));return nl_.command(NL80211_CMD_VENDOR,a,[&](const Attrs&result){if(reply&&result.count(NL80211_ATTR_VENDOR_DATA))*reply=result.at(NL80211_ATTR_VENDOR_DATA);});}
int Backend::followup(uint8_t local,uint8_t peer,const std::array<uint8_t,6>&dst,const Bytes&info){if(info.size()>512)return -EINVAL;Bytes f=attr(NL80211_NAN_FUNC_TYPE,uint8_t(NL80211_NAN_FUNC_FOLLOW_UP));append(f,attr(NL80211_NAN_FUNC_FOLLOW_UP_ID,local));append(f,attr(NL80211_NAN_FUNC_FOLLOW_UP_REQ_ID,peer));append(f,attr(NL80211_NAN_FUNC_FOLLOW_UP_DEST,dst.data(),6));if(!info.empty())append(f,attr(NL80211_NAN_FUNC_SERVICE_INFO,info.data(),info.size()));Bytes a=attr(NL80211_ATTR_WDEV,wdev);append(a,nest(NL80211_ATTR_NAN_FUNC,f));return nl_.command(NL80211_CMD_ADD_NAN_FUNCTION,a);}
int Backend::neighbor(const std::array<uint8_t,16>&address,const std::array<uint8_t,6>&peer,bool add){
 unsigned index=if_nametoindex("aware_data0");if(!index)return -ENODEV;
 if(address[0]!=0xfe||address[1]!=0x80||(peer[0]&1)||std::all_of(peer.begin(),peer.end(),[](uint8_t v){return v==0;}))return -EINVAL;
 Bytes b(sizeof(ndmsg));auto *message=reinterpret_cast<ndmsg*>(b.data());message->ndm_family=AF_INET6;message->ndm_ifindex=index;message->ndm_state=NUD_PERMANENT;
 append(b,attr(NDA_DST,address.data(),address.size()));if(add)append(b,attr(NDA_LLADDR,peer.data(),peer.size()));
 int result=route_.request(add?RTM_NEWNEIGH:RTM_DELNEIGH,add?(NLM_F_CREATE|NLM_F_REPLACE):0,b);
 return !add&&result==-ENOENT?0:result;
}
int Backend::bridge(const std::string&name,bool create){if(name!="aware_data0")return -EINVAL;int r=0;unsigned idx=if_nametoindex(name.c_str());if(create&&!idx){Bytes a(sizeof(ifinfomsg));reinterpret_cast<ifinfomsg*>(a.data())->ifi_family=AF_UNSPEC;append(a,textAttr(IFLA_IFNAME,name));auto m=mac("nan0");append(a,attr(IFLA_ADDRESS,m.data(),6));Bytes config;append(config,attr(IFLA_BR_STP_STATE,uint32_t(0)));append(config,attr(IFLA_BR_FORWARD_DELAY,uint32_t(0)));append(config,attr(IFLA_BR_MCAST_SNOOPING,uint8_t(0)));Bytes link=textAttr(IFLA_INFO_KIND,"bridge");append(link,nest(IFLA_INFO_DATA,config));append(a,nest(IFLA_LINKINFO,link));r=route_.request(RTM_NEWLINK,NLM_F_CREATE|NLM_F_EXCL,a);if(r)return r;idx=if_nametoindex(name.c_str());}
 if(create){if(!idx)return -ENODEV;Bytes a(sizeof(ifinfomsg));reinterpret_cast<ifinfomsg*>(a.data())->ifi_index=if_nametoindex("nan0");append(a,attr(IFLA_MASTER,uint32_t(idx)));return route_.request(RTM_NEWLINK,0,a);}
 if(!idx)return 0;Bytes a(sizeof(ifinfomsg));reinterpret_cast<ifinfomsg*>(a.data())->ifi_index=idx;return route_.request(RTM_DELLINK,0,a);}
}
