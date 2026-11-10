// SPDX-License-Identifier: Apache-2.0
#include "UnsupportedInterfaces.h"
#include "netlink_backend.h"
#include "vendor_abi.h"
#include "vendor_nan.h"
#include <aidl/android/hardware/wifi/IWifiChipEventCallback.h>
#include <aidl/android/hardware/wifi/IWifiEventCallback.h>
#include <aidl/android/hardware/wifi/IWifiNanIfaceEventCallback.h>
#include <aidl/android/hardware/wifi/IWifiStaIfaceEventCallback.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/log.h>
#include <linux/nl80211.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
extern "C" {
#include "green2_sha256.h"
}
namespace green2 {
using Status=ndk::ScopedAStatus;
static Status ok(){return Status::ok();}
static Status error(int e){return Status::fromServiceSpecificErrorWithMessage(static_cast<int>(w::WifiStatusCode::ERROR_UNKNOWN),strerror(e<0?-e:e));}
static w::NanStatus nanStatus(int e){w::NanStatus s;s.status=w::NanStatusCode::SUCCESS;if(e){s.status=e==-EINVAL?w::NanStatusCode::INVALID_ARGS:e==-EOPNOTSUPP?w::NanStatusCode::NOT_SUPPORTED:w::NanStatusCode::INTERNAL_FAILURE;s.description=strerror(e<0?-e:e);}return s;}
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL",__VA_ARGS__)
template<class T> static bool copy(const Bytes&b,T&v){if(b.size()<sizeof(T))return false;memcpy(&v,b.data(),sizeof(T));return true;}
static Bytes tlvInfo(const uint8_t*p,size_t n){
 Bytes fallback;
 for(size_t at=0;n-at>=3;){
  size_t len=p[at+1]|(size_t(p[at+2])<<8);if(len>n-at-3)return{};
  const uint8_t*d=p+at+3;size_t i=0;
  if(p[at]==14&&len>=3){
   unsigned control=d[1]|(unsigned(d[2])<<8);i=3;
   if(control&0x100)i+=4;if(control&0x200)i++;
   if(i>len)return{};if(len-i>=2){size_t count=d[i]|(size_t(d[i+1])<<8);i+=2;if(count>len-i)return{};if(count)return Bytes(d+i,d+i+count);}
  }else if(p[at]==3&&len>=9){
   i=9;uint8_t flags=d[8];if(flags&4)i+=2;
   for(int bit:{3,4})if(flags&(1<<bit)){if(i>=len)return{};size_t count=d[i++];if(count>len-i)return{};i+=count;}
   if(flags&32){if(i>=len)return{};size_t count=d[i++];if(count>len-i)return{};fallback.assign(d+i,d+i+count);}
  }
  at+=3+len;
 }
 return fallback;
}
class NanImpl:public UnsupportedIWifiNanIface {
 std::unique_ptr<Backend>b_=std::make_unique<Backend>();std::unique_ptr<VendorNan>v_=std::make_unique<VendorNan>(*b_);std::mutex mu_;std::atomic<bool>valid_{true};
 std::vector<std::shared_ptr<w::IWifiNanIfaceEventCallback>> callbacks_;
 struct Session{bool sub;uint64_t cookie;w::NanDiscoveryCommonConfig config;};std::map<uint8_t,Session> sessions_;
 std::map<int,rtw_nan_datapath_request_received_event_data> incoming_;std::optional<char16_t> pendingNdp_;std::deque<char16_t>followups_;std::map<int,bool>initiator_;
 struct PeerNeighbor{std::array<uint8_t,16>address{};std::array<uint8_t,6>mac{};bool installed=false;unsigned attempts=0;};std::map<int,PeerNeighbor>neighbors_;
 void installNeighbor(int id,PeerNeighbor &peer){if(peer.installed||peer.attempts>=25)return;int result=b_->neighbor(peer.address,peer.mac,true);peer.attempts++;peer.installed=!result;if(!result||peer.attempts==1||peer.attempts==25)LOG("NDP neighbor id=%d result=%d attempt=%u",id,result,peer.attempts);}
 void removeNeighbor(int id){auto found=neighbors_.find(id);if(found==neighbors_.end())return;auto peer=found->second;neighbors_.erase(found);for(const auto &[other,value]:neighbors_)if(value.address==peer.address)return;b_->neighbor(peer.address,peer.mac,false);}
 template<class Fn>void notify(Fn fn){for(const auto&c:callbacks_)if(c)fn(*c);}
 int nandow(uint8_t cmd,const void*p,size_t n){if(n>sizeof(nandow_para))return -EMSGSIZE;nan_customer_nandow v{};v.cmd_id=cmd;v.cmd_type=2;v.para_len=n;memcpy(&v.para,p,n);return b_->vendor(0x1905,&v,sizeof(v));}
 template<class T>int nandow(uint8_t cmd,const T&p){return nandow(cmd,&p,sizeof(p));}
 void confirm(int id,bool success,const std::array<uint8_t,6>&peer,const Bytes&info){if(pendingNdp_){auto tx=*pendingNdp_;pendingNdp_.reset();initiator_[id]=true;notify([&](auto&c){c.notifyInitiateDataPathResponse(tx,nanStatus(0),id);});}w::NanDataPathConfirmInd e;e.ndpInstanceId=id;e.dataPathSetupSuccess=success;e.peerNdiMacAddr=peer;e.appInfo=info;e.status=nanStatus(success?0:-ECONNREFUSED);notify([&](auto&c){c.eventDataPathConfirm(e);});}
 void vendorEvent(uint32_t cmd,const Bytes&data){
  if(v_->event(cmd,data))return;
  if(cmd==1){rtw_nan_cluster_changed_event_data v{};if(!copy(data,v))return;w::NanClusterEventInd e;e.eventType=w::NanClusterEventType::STARTED_CLUSTER;memcpy(e.addr.data(),v.cluster_id.ether_addr_octet,6);notify([&](auto&c){c.eventClusterEvent(e);});}
  else if(cmd==2){constexpr auto off=offsetof(rtw_nan_discovery_result_received_event_data,publish_attribute_list);if(data.size()<off)return;rtw_nan_discovery_result_received_event_data v{};memcpy(&v,data.data(),off);if(v.publish_attribute_list_length>data.size()-off||!sessions_.count(v.subscribe_id))return;w::NanMatchInd e;e.discoverySessionId=v.subscribe_id;e.peerId=v.publisher_id;memcpy(e.addr.data(),v.peer_nan_address.ether_addr_octet,6);e.rssiValue=v.publish_rssi;e.serviceSpecificInfo=tlvInfo(data.data()+off,v.publish_attribute_list_length);notify([&](auto&c){c.eventMatch(e);});LOG("NAN match local=%u peer=%u",v.subscribe_id,v.publisher_id);}
  else if(cmd==3){constexpr auto off=offsetof(rtw_nan_publish_replied_private_event_data,subscribe_attribute_list);if(data.size()<off)return;rtw_nan_publish_replied_private_event_data v{};memcpy(&v,data.data(),off);if(v.subscribe_attribute_list_length>data.size()-off||!sessions_.count(v.publish_id))return;w::NanMatchInd e;e.discoverySessionId=v.publish_id;e.peerId=v.peer_subscriber_id;memcpy(e.addr.data(),v.peer_nan_address.ether_addr_octet,6);e.serviceSpecificInfo=tlvInfo(data.data()+off,v.subscribe_attribute_list_length);e.rssiValue=v.subscribe_rssi;notify([&](auto&c){c.eventMatch(e);});}
  else if(cmd==4){constexpr auto off=offsetof(rtw_nan_datapath_request_received_event_data,attribute_list);if(data.size()<off)return;rtw_nan_datapath_request_received_event_data v{};memcpy(&v,data.data(),off);if(v.attribute_list_length>data.size()-off)return;incoming_[v.datapath_id]=v;initiator_[v.datapath_id]=false;w::NanDataPathRequestInd e;e.discoverySessionId=v.publish_id;e.ndpInstanceId=v.datapath_id;e.securityRequired=v.security;memcpy(e.peerDiscMacAddr.data(),v.initiator_management_address.ether_addr_octet,6);notify([&](auto&c){c.eventDataPathRequest(e);});}
  else if(cmd==5){constexpr auto off=offsetof(rtw_nan_datapath_response_received_event_data,attribute_list);if(data.size()<off)return;rtw_nan_datapath_response_received_event_data v{};memcpy(&v,data.data(),off);if(pendingNdp_){auto tx=*pendingNdp_;pendingNdp_.reset();initiator_[v.datapath_id]=true;notify([&](auto&c){c.notifyInitiateDataPathResponse(tx,nanStatus(0),v.datapath_id);});}}
  else if(cmd==7){rtw_nan_datapath_established_event_data v{};if(!copy(data,v))return;std::array<uint8_t,6> peer{};memcpy(peer.data(),initiator_[v.datapath_id]?v.responder_data_address.ether_addr_octet:v.initiator_data_address.ether_addr_octet,6);if(v.service_specific_info.length>sizeof(v.service_specific_info.info))return;Bytes networkInfo;
 // AIDL uses NDPE subelement TLVs, while the SDK separates the peer IID
 // and generic service information. Preserve iOS's advertised IPv6 IID.
 if(std::any_of(std::begin(v.peer_interface_identifier),std::end(v.peer_interface_identifier),[](uint8_t b){return b!=0;})){
  networkInfo={0,8,0};networkInfo.insert(networkInfo.end(),std::begin(v.peer_interface_identifier),std::end(v.peer_interface_identifier));}
 if(v.service_specific_info.length){size_t n=v.service_specific_info.length;networkInfo.push_back(1);networkInfo.push_back(n&255);networkInfo.push_back(n>>8);networkInfo.insert(networkInfo.end(),v.service_specific_info.info,v.service_specific_info.info+n);}
 PeerNeighbor neighbor;neighbor.mac=peer;neighbor.address[0]=0xfe;neighbor.address[1]=0x80;
 if(networkInfo.size()>=11&&networkInfo[0]==0)memcpy(neighbor.address.data()+8,networkInfo.data()+3,8);
 else {neighbor.address[8]=peer[0]^2;neighbor.address[9]=peer[1];neighbor.address[10]=peer[2];neighbor.address[11]=0xff;neighbor.address[12]=0xfe;neighbor.address[13]=peer[3];neighbor.address[14]=peer[4];neighbor.address[15]=peer[5];}
 removeNeighbor(v.datapath_id);neighbors_[v.datapath_id]=neighbor;installNeighbor(v.datapath_id,neighbors_[v.datapath_id]);
 confirm(v.datapath_id,true,peer,networkInfo);}
  else if(cmd==8||cmd==27){rtw_nan_datapath_end_event_data v{};if(!copy(data,v))return;removeNeighbor(v.datapath_id);incoming_.erase(v.datapath_id);initiator_.erase(v.datapath_id);notify([&](auto&c){c.eventDataPathTerminated(v.datapath_id);});}
  else if(cmd==10||cmd==11){if(data.empty())return;uint8_t id=data[0];v_->terminated(id,cmd==10);sessions_.erase(id);notify([&](auto&c){if(cmd==10)c.eventSubscribeTerminated(id,nanStatus(0));else c.eventPublishTerminated(id,nanStatus(0));});}
  else if(cmd==12){constexpr auto off=offsetof(rtw_nan_follow_up_received_event_data,attribute_list);if(data.size()<off)return;rtw_nan_follow_up_received_event_data v{};memcpy(&v,data.data(),off);if(v.attribute_list_length>data.size()-off)return;w::NanFollowupReceivedInd e;e.discoverySessionId=v.local_service_id;e.peerId=v.peer_service_id;memcpy(e.addr.data(),v.peer_nan_address.ether_addr_octet,6);e.serviceSpecificInfo=tlvInfo(data.data()+off,v.attribute_list_length);notify([&](auto&c){c.eventFollowupReceived(e);});}
  else if(cmd==13){rtw_nan_follow_up_tx_complete_event_data v{};if(!copy(data,v)||followups_.empty())return;auto id=followups_.front();followups_.pop_front();notify([&](auto&c){c.eventTransmitFollowup(id,nanStatus(v.status? -ENOMSG:0));});}
 }
 void event(uint8_t cmd,const Attrs&a){std::lock_guard<std::mutex>lock(mu_);if(!valid_)return;
  if(cmd==0){v_->tick();for(auto &[id,peer]:neighbors_)installNeighbor(id,peer);if(b_->wdev&&(if_nametoindex("wlan1")!=b_->radioIndex||!if_nametoindex("nan0"))){
   b_->wdev=0;b_->enabled=false;b_->phy=-1;v_->reset();
   // Retire this radio generation without disabling Android's Aware service.
   // eventDisabled disables usage until a global Wi-Fi restart. Discovery
   // termination instead lets clients reattach when this USB radio returns.
   for(const auto &[id,session]:sessions_)notify([&](auto&c){if(session.sub)c.eventSubscribeTerminated(id,nanStatus(-ENODEV));else c.eventPublishTerminated(id,nanStatus(-ENODEV));});
   for(const auto &[id,value]:initiator_)notify([&](auto&c){c.eventDataPathTerminated(id);});
   sessions_.clear();incoming_.clear();initiator_.clear();pendingNdp_.reset();followups_.clear();b_->bridge("aware_data0",false);
   LOG("Green2 removed: discovery/data sessions terminated");
  }return;}
  if(a.count(NL80211_ATTR_WIPHY)&&value<uint32_t>(a,NL80211_ATTR_WIPHY)!=uint32_t(b_->phy))return;
  if(cmd==NL80211_CMD_VENDOR&&value<uint32_t>(a,NL80211_ATTR_VENDOR_ID)==0xe04c&&a.count(NL80211_ATTR_VENDOR_DATA)){vendorEvent(value<uint32_t>(a,NL80211_ATTR_VENDOR_SUBCMD),a.at(NL80211_ATTR_VENDOR_DATA));}
  else if(cmd==NL80211_CMD_NAN_MATCH&&a.count(NL80211_ATTR_NAN_MATCH)){auto match=parse(a.at(NL80211_ATTR_NAN_MATCH).data(),a.at(NL80211_ATTR_NAN_MATCH).size());if(!match.count(NL80211_NAN_MATCH_FUNC_LOCAL)||!match.count(NL80211_NAN_MATCH_FUNC_PEER))return;auto&l=match[NL80211_NAN_MATCH_FUNC_LOCAL];auto&p=match[NL80211_NAN_MATCH_FUNC_PEER];auto local=parse(l.data(),l.size()),peer=parse(p.data(),p.size());w::NanMatchInd e;e.discoverySessionId=value<uint8_t>(local,NL80211_NAN_FUNC_INSTANCE_ID);e.peerId=value<uint8_t>(peer,NL80211_NAN_FUNC_INSTANCE_ID);if(a.count(NL80211_ATTR_MAC)&&a.at(NL80211_ATTR_MAC).size()==6)memcpy(e.addr.data(),a.at(NL80211_ATTR_MAC).data(),6);if(peer.count(NL80211_NAN_FUNC_SERVICE_INFO))e.serviceSpecificInfo=peer[NL80211_NAN_FUNC_SERVICE_INFO];notify([&](auto&c){c.eventMatch(e);});}
 }
public:
 NanImpl(){b_->event=[this](uint8_t c,const Attrs&a){event(c,a);};v_->pairingRequest=[this](const auto&e){notify([&](auto&c){c.eventPairingRequest(e);});};v_->pairingConfirm=[this](const auto&e){notify([&](auto&c){c.eventPairingConfirm(e);});};}
 ~NanImpl(){shutdown();}
 void shutdown(){if(valid_.exchange(false)){b_->stopEvents();v_->reset();b_->release();}}
 Status getName(std::string*out)override{*out="nan0";return valid_?ok():error(ENODEV);}
 Status registerEventCallback(const std::shared_ptr<w::IWifiNanIfaceEventCallback>&cb)override{std::lock_guard<std::mutex>g(mu_);if(!cb)return error(EINVAL);callbacks_.push_back(cb);return ok();}
 Status getCapabilitiesRequest(char16_t id)override{std::lock_guard<std::mutex>g(mu_);w::NanCapabilities cap;cap.maxConcurrentClusters=1;cap.maxPublishes=2;cap.maxSubscribes=1;cap.maxServiceNameLen=255;cap.maxMatchFilterLen=32;cap.maxTotalMatchFilterLen=256;cap.maxServiceSpecificInfoLen=512;cap.maxExtendedServiceSpecificInfoLen=0;cap.maxNdiInterfaces=1;cap.maxNdpSessions=3;cap.maxAppInfoLen=512;cap.maxQueuedTransmitFollowupMsgs=1;cap.maxSubscribeInterfaceAddresses=0;cap.supportedCipherSuites=v_->pairingSupported()?65:1;cap.supportsPairing=v_->pairingSupported();notify([&](auto&c){c.notifyCapabilitiesResponse(id,nanStatus(0),cap);});return ok();}
 Status enableRequest(char16_t id,const w::NanEnableRequest&m,const w::NanConfigRequestSupplemental&)override{std::lock_guard<std::mutex>g(mu_);int r=valid_?b_->enable(m):-ENODEV;if(!r)r=v_->initialize(uint8_t(m.configParams.masterPref));if(r){v_->reset();b_->release();}LOG("enableRequest id=%u masterPref=%d result=%d",id,int(m.configParams.masterPref),r);notify([&](auto&c){c.notifyEnableResponse(id,nanStatus(r));});if(!r){w::NanClusterEventInd e;e.eventType=w::NanClusterEventType::DISCOVERY_MAC_ADDRESS_CHANGED;e.addr=mac("nan0");notify([&](auto&c){c.eventClusterEvent(e);});}return ok();}
 Status configRequest(char16_t id,const w::NanConfigRequest&m,const w::NanConfigRequestSupplemental&)override{std::lock_guard<std::mutex>g(mu_);int r=b_->config(m.masterPref);notify([&](auto&c){c.notifyConfigResponse(id,nanStatus(r));});return ok();}
 Status disableRequest(char16_t id)override{std::lock_guard<std::mutex>g(mu_);v_->reset();int r=b_->disable();sessions_.clear();notify([&](auto&c){c.notifyDisableResponse(id,nanStatus(r));});return ok();}
 Status recordPublish(char16_t tx,const w::NanDiscoveryCommonConfig&conf,bool sub,int r,uint8_t id){if(!r)sessions_[id]={sub,0,conf};LOG("%s tx=%u instance=%u result=%d",sub?"subscribe":"publish",tx,id,r);notify([&](auto&c){if(sub)c.notifyStartSubscribeResponse(tx,nanStatus(r),id);else c.notifyStartPublishResponse(tx,nanStatus(r),id);});return ok();}
 Status startPublishRequest(char16_t tx,const w::NanPublishRequest&m)override{std::lock_guard<std::mutex>g(mu_);uint8_t id=0;int r=v_->publish(m,id);return recordPublish(tx,m.baseConfigs,false,r,id);}
 Status startSubscribeRequest(char16_t tx,const w::NanSubscribeRequest&m)override{std::lock_guard<std::mutex>g(mu_);uint8_t id=0;int r=v_->subscribe(m,id);return recordPublish(tx,m.baseConfigs,true,r,id);}
 Status respondToPairingIndicationRequest(char16_t tx,const w::NanRespondToPairingIndicationRequest&m)override{std::lock_guard<std::mutex>g(mu_);int r=v_->respond(m);notify([&](auto&c){c.notifyRespondToPairingIndicationResponse(tx,nanStatus(r));});return ok();}
 Status stop(char16_t tx,uint8_t id,bool sub){std::lock_guard<std::mutex>g(mu_);int r=-ENOENT;auto i=sessions_.find(id);if(i!=sessions_.end()){r=v_->cancel(id,sub);if(!r)sessions_.erase(i);}notify([&](auto&c){if(sub)c.notifyStopSubscribeResponse(tx,nanStatus(r));else c.notifyStopPublishResponse(tx,nanStatus(r));});return ok();}
 Status stopPublishRequest(char16_t id,int8_t session)override{return stop(id,session,false);}
 Status stopSubscribeRequest(char16_t id,int8_t session)override{return stop(id,session,true);}
 Status createDataInterfaceRequest(char16_t id,const std::string&name)override{std::lock_guard<std::mutex>g(mu_);int r=b_->init();if(!r)r=b_->bridge(name,true);LOG("createDataInterface %s result=%d",name.c_str(),r);notify([&](auto&c){c.notifyCreateDataInterfaceResponse(id,nanStatus(r));});return ok();}
 Status deleteDataInterfaceRequest(char16_t id,const std::string&name)override{std::lock_guard<std::mutex>g(mu_);int r=b_->bridge(name,false);notify([&](auto&c){c.notifyDeleteDataInterfaceResponse(id,nanStatus(r));});return ok();}
 Status transmitFollowupRequest(char16_t id,const w::NanTransmitFollowupRequest&m)override{std::lock_guard<std::mutex>g(mu_);int r=followups_.empty()?v_->followup(m):-ENOSPC;if(!r)followups_.push_back(id);notify([&](auto&c){c.notifyTransmitFollowupResponse(id,nanStatus(r));});return ok();}
 Status initiateDataPathRequest(char16_t id,const w::NanInitiateDataPathRequest&m)override{std::lock_guard<std::mutex>g(mu_);if(m.ifaceName!="aware_data0"||m.appInfo.size()>512||pendingNdp_)return error(EINVAL);rtw_nan_datapath_request p{};p.responder_publish_id=m.peerId;p.initiator_subscribe_id=m.discoverySessionId;p.confirm_required=1;memcpy(p.responder_nan_addr.ether_addr_octet,m.peerDiscMacAddr.data(),6);auto own=mac("nan0");memcpy(p.initiator_data_address.ether_addr_octet,own.data(),6);p.service_specific_info.length=m.appInfo.size();memcpy(p.service_specific_info.info,m.appInfo.data(),m.appInfo.size());auto cfg=sessions_.find(uint8_t(m.discoverySessionId));Bytes service=m.serviceNameOutOfBand;if(service.empty()&&cfg!=sessions_.end())service=cfg->second.config.serviceName;for(auto&v:service)v=std::tolower(v);uint8_t h[32];sha256(service.data(),service.size(),h);memcpy(p.service_hash,h,6);if(m.securityConfig.securityType==w::NanDataPathSecurityType::PMK){p.cipher_suite_id=1;p.sec_type=1;memcpy(p.key,m.securityConfig.pmk.data(),32);}else if(m.securityConfig.securityType==w::NanDataPathSecurityType::PASSPHRASE){if(m.securityConfig.passphrase.size()<8||m.securityConfig.passphrase.size()>31)return error(EINVAL);p.cipher_suite_id=1;p.sec_type=2;memcpy(p.key,m.securityConfig.passphrase.data(),m.securityConfig.passphrase.size());}pendingNdp_=id;int r=nandow(RTW_NAN_CMD_DATAPATH_REQ,p);if(r){pendingNdp_.reset();notify([&](auto&c){c.notifyInitiateDataPathResponse(id,nanStatus(r),0);});}return ok();}
 Status respondToDataPathIndicationRequest(char16_t id,const w::NanRespondToDataPathIndicationRequest&m)override{std::lock_guard<std::mutex>g(mu_);auto it=incoming_.find(m.ndpInstanceId);if(it==incoming_.end()||m.appInfo.size()>512)return error(EINVAL);rtw_nan_datapath_response p{};p.datapath_id=m.ndpInstanceId;p.publish_id=it->second.publish_id;auto service=sessions_.find(p.publish_id);if(service==sessions_.end())return error(ENOENT);
 Bytes name=service->second.config.serviceName;for(auto &ch:name)ch=std::tolower(ch);uint8_t serviceHash[32];sha256(name.data(),name.size(),serviceHash);memcpy(p.service_hash,serviceHash,6);
 p.status=m.acceptRequest?RTW_NAN_DATAPATH_ACCEPTED:RTW_NAN_DATAPATH_REJECTED;memcpy(p.initiator_data_address.ether_addr_octet,it->second.initiator_data_address.ether_addr_octet,6);memcpy(p.initiator_management_address.ether_addr_octet,it->second.initiator_management_address.ether_addr_octet,6);auto own=mac("nan0");memcpy(p.responder_data_address.ether_addr_octet,own.data(),6);p.service_specific_info.length=m.appInfo.size();memcpy(p.service_specific_info.info,m.appInfo.data(),m.appInfo.size());if(m.securityConfig.securityType==w::NanDataPathSecurityType::PMK){p.cipher_suite_id=1;memcpy(p.key,m.securityConfig.pmk.data(),32);}else if(m.securityConfig.securityType==w::NanDataPathSecurityType::PASSPHRASE){if(m.securityConfig.passphrase.size()<8||m.securityConfig.passphrase.size()>31)return error(EINVAL);p.cipher_suite_id=1;memcpy(p.key,m.securityConfig.passphrase.data(),m.securityConfig.passphrase.size());}int denied=0;if(v_->securePublisher(p.publish_id)){if(!v_->authenticated(p.initiator_management_address.ether_addr_octet)){p.status=RTW_NAN_DATAPATH_REJECTED;denied=-EACCES;}else {p.cipher_suite_id=RTW_NAN_CIPHER_ID_PASN_128;
 // Preserve the Core publisher's WFA generic NDPE TCP/8000 advertisement.
 if(m.appInfo.empty()){uint8_t info[]={0x50,0x6f,0x9a,2,0,2,0,0x40,0x1f,1,1,0,6};
 auto config=sessions_.find(p.publish_id);if(config!=sessions_.end()){const auto &name=config->second.config.serviceName;
 const char udp[]="._udp";if(name.size()>=5&&!memcmp(name.data()+name.size()-5,udp,5)){info[7]=0x28;info[8]=0x23;info[12]=17;}}
 p.service_specific_info.length=sizeof(info);memcpy(p.service_specific_info.info,info,sizeof(info));}}
}int r=nandow(RTW_NAN_CMD_DATAPATH_RSP,p);if(!r&&denied)r=denied;notify([&](auto&c){c.notifyRespondToDataPathIndicationResponse(id,nanStatus(r));});return ok();}
 Status terminateDataPathRequest(char16_t id,int32_t ndp)override{std::lock_guard<std::mutex>g(mu_);if(ndp<0||ndp>255)return error(EINVAL);rtw_nan_datapath_end p{};p.datapath_id=ndp;p.reason_code=RTW_NAN_DATAPATH_END_REASON_USER_INITIATED;int r=nandow(RTW_NAN_CMD_DATAPATH_END,p);notify([&](auto&c){c.notifyTerminateDataPathResponse(id,nanStatus(r));});return ok();}
};
class StaImpl:public UnsupportedIWifiStaIface {
 std::vector<std::shared_ptr<w::IWifiStaIfaceEventCallback>> callbacks_;
public:
 Status getName(std::string*out)override{*out="wlan0";return ok();}
 Status getFeatureSet(int32_t*out)override{*out=static_cast<int>(w::IWifiStaIface::FeatureSetMask::STA_5G);return ok();}
 Status registerEventCallback(const std::shared_ptr<w::IWifiStaIfaceEventCallback>&c)override{if(!c)return error(EINVAL);callbacks_.push_back(c);return ok();}
 Status getFactoryMacAddress(std::array<uint8_t,6>*out)override{*out=mac("wlan0");return ok();}
 Status setMacAddress(const std::array<uint8_t,6>&m)override{int r=setMac("wlan0",m);return r?error(r):ok();}
 Status setScanMode(bool)override{return ok();}
 Status enableLinkLayerStatsCollection(bool)override{return unsupported(__func__);}
 Status setRoamingState(w::StaRoamingState)override{return unsupported(__func__);}
};
class ChipImpl:public UnsupportedIWifiChip {
 int id_,mode_=-1;std::mutex mu_;std::shared_ptr<StaImpl>sta_;std::shared_ptr<NanImpl>nan_;std::vector<std::shared_ptr<w::IWifiChipEventCallback>> callbacks_;
public:
 explicit ChipImpl(int id):id_(id){}
 void reset(){std::lock_guard<std::mutex>g(mu_);if(nan_)nan_->shutdown();nan_.reset();sta_.reset();mode_=-1;}
 Status getId(int32_t*out)override{*out=id_;return ok();}
 Status getMode(int32_t*out)override{if(mode_<0)return Status::fromServiceSpecificError(static_cast<int>(w::WifiStatusCode::ERROR_NOT_AVAILABLE));*out=mode_;return ok();}
 Status getFeatureSet(int32_t*out)override{*out=0;return ok();}
 Status getAvailableModes(std::vector<w::IWifiChip::ChipMode>*out)override{w::IWifiChip::ChipConcurrencyCombinationLimit limit;limit.maxIfaces=1;limit.types={id_?w::IfaceConcurrencyType::NAN_IFACE:w::IfaceConcurrencyType::STA};w::IWifiChip::ChipConcurrencyCombination combo;combo.limits={limit};w::IWifiChip::ChipMode mode;mode.id=0;mode.availableCombinations={combo};*out={mode};return ok();}
 Status configureChip(int32_t mode)override{if(mode!=0)return error(EINVAL);mode_=mode;for(auto&c:callbacks_)c->onChipReconfigured(mode);LOG("configureChip id=%d mode=%d",id_,mode);return ok();}
 Status registerEventCallback(const std::shared_ptr<w::IWifiChipEventCallback>&c)override{if(!c)return error(EINVAL);callbacks_.push_back(c);return ok();}
 Status createStaIface(std::shared_ptr<w::IWifiStaIface>*out)override{std::lock_guard<std::mutex>g(mu_);if(id_)return unsupported(__func__);if(!sta_){sta_=ndk::SharedRefBase::make<StaImpl>();for(auto&c:callbacks_)c->onIfaceAdded(w::IfaceType::STA,"wlan0");}*out=sta_;return ok();}
 Status getStaIfaceNames(std::vector<std::string>*out)override{*out=sta_?std::vector<std::string>{"wlan0"}:std::vector<std::string>{};return ok();}
 Status getStaIface(const std::string&n,std::shared_ptr<w::IWifiStaIface>*out)override{if(n!="wlan0"||!sta_)return error(ENODEV);*out=sta_;return ok();}
 Status removeStaIface(const std::string&n)override{if(n!="wlan0"||!sta_)return error(ENODEV);sta_.reset();for(auto&c:callbacks_)c->onIfaceRemoved(w::IfaceType::STA,n);return ok();}
 Status createNanIface(std::shared_ptr<w::IWifiNanIface>*out)override{std::lock_guard<std::mutex>g(mu_);if(!id_)return unsupported(__func__);if(!nan_){nan_=ndk::SharedRefBase::make<NanImpl>();for(auto&c:callbacks_)c->onIfaceAdded(w::IfaceType::NAN_IFACE,"nan0");}*out=nan_;return ok();}
 Status getNanIfaceNames(std::vector<std::string>*out)override{*out=nan_?std::vector<std::string>{"nan0"}:std::vector<std::string>{};return ok();}
 Status getNanIface(const std::string&n,std::shared_ptr<w::IWifiNanIface>*out)override{if(n!="nan0"||!nan_)return error(ENODEV);*out=nan_;return ok();}
 Status removeNanIface(const std::string&n)override{std::lock_guard<std::mutex>g(mu_);if(n!="nan0"||!nan_)return error(ENODEV);nan_->shutdown();nan_.reset();for(auto&c:callbacks_)c->onIfaceRemoved(w::IfaceType::NAN_IFACE,n);return ok();}
 Status getApIfaceNames(std::vector<std::string>*out)override{out->clear();return ok();}
 Status getP2pIfaceNames(std::vector<std::string>*out)override{out->clear();return ok();}
 Status requestChipDebugInfo(w::IWifiChip::ChipDebugInfo*out)override{out->driverDescription=id_?"Green2 WHC USB NAN":"TI GKI mwifiex SD8997";out->firmwareDescription=id_?"existing Green2 flash firmware":"NXP SDIO/UART";return ok();}
 Status setCountryCode(const std::array<uint8_t,2>&code)override{Netlink nl(NETLINK_GENERIC);int r=nl.connectGeneric();if(r)return error(r);std::string cc(code.begin(),code.end());r=nl.command(NL80211_CMD_REQ_SET_REG,textAttr(NL80211_ATTR_REG_ALPHA2,cc));return r?error(r):ok();}
};
class WifiImpl:public UnsupportedIWifi {

 std::atomic<bool>started_{false};std::array<std::shared_ptr<ChipImpl>,2>chips_{ndk::SharedRefBase::make<ChipImpl>(0),ndk::SharedRefBase::make<ChipImpl>(1)};std::vector<std::shared_ptr<w::IWifiEventCallback>>callbacks_;
public:
 Status getChip(int32_t id,std::shared_ptr<w::IWifiChip>*out)override{if(id<0||id>=2)return error(EINVAL);*out=chips_[id];return ok();}
 Status getChipIds(std::vector<int32_t>*out)override{*out={0,1};return ok();}
 Status isStarted(bool*out)override{*out=started_;return ok();}
 Status registerEventCallback(const std::shared_ptr<w::IWifiEventCallback>&c)override{if(!c)return error(EINVAL);callbacks_.push_back(c);return ok();}
 Status start()override{started_=true;LOG("IWifi.start");for(auto&c:callbacks_)c->onStart();return ok();}
 Status stop()override{for(auto&c:chips_)c->reset();started_=false;for(auto&c:callbacks_)c->onStop();return ok();}
 binder_status_t dump(int fd,const char**,uint32_t)override{dprintf(fd,"Green2 dual-radio HAL: started=%d\nchip0=88W8997 STA wlan0\nchip1=Green2 NAN nan0, NDI aware_data0\n",int(started_));return STATUS_OK;}
};
}
int main(int argc,char**argv){using namespace green2;if(argc>1&&std::string(argv[1])=="--probe"){Backend b;w::NanEnableRequest e;e.configParams.masterPref=static_cast<int8_t>(128);e.operateInBand={true,true,false};int r=b.enable(e);printf("NAN enable=%d phy=%d wdev=%llx\n",r,b.phy,(unsigned long long)b.wdev);if(r)return 1;w::NanDiscoveryCommonConfig c;std::string s="green2-hal-probe";c.serviceName.assign(s.begin(),s.end());c.ttlSec=10;uint8_t id=0;uint64_t cookie=0;r=b.publish(c,false,false,0,id,cookie);printf("NAN publish=%d instance=%u cookie=%llu\n",r,id,(unsigned long long)cookie);if(!r&&(!id||!cookie))r=-EPROTO;if(!r)b.cancel(cookie);int br=b.bridge("aware_data0",true);printf("NDI bridge=%d\n",br);b.bridge("aware_data0",false);return r||br?1:0;}
 ABinderProcess_setThreadPoolMaxThreadCount(4);auto service=ndk::SharedRefBase::make<WifiImpl>();auto name=std::string(w::IWifi::descriptor)+"/default";binder_status_t status=AServiceManager_addService(service->asBinder().get(),name.c_str());LOG("register %s status=%d",name.c_str(),status);if(status!=STATUS_OK)return 1;ABinderProcess_joinThreadPool();return 0;}
