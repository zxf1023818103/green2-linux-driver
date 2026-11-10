// SPDX-License-Identifier: Apache-2.0
#include "vendor_nan.h"
#include <dlfcn.h>
#include <android/log.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
extern "C" {
#include "green2_sha256.h"
}
namespace green2 {
namespace {
constexpr char kCache[]="/data/vendor/wifi/green2/nik_cache";
struct Bootstrap {uint8_t status,remote,local;uint16_t method;uint8_t peer[6];};
struct Result {uint8_t status,role,type,caching,peer[6];};
static_assert(sizeof(Bootstrap)==12&&sizeof(Result)==10);
w::NanStatus status(int error){w::NanStatus s;s.status=error?w::NanStatusCode::INTERNAL_FAILURE:w::NanStatusCode::SUCCESS;return s;}
int atomicWrite(const char*path,const Bytes &data){
    std::string temp=std::string(path)+".XXXXXX";std::vector<char>name(temp.begin(),temp.end());name.push_back(0);
    int fd=mkstemp(name.data());if(fd<0)return -errno;
    size_t at=0;int error=0;
    while(at<data.size()){ssize_t n=write(fd,data.data()+at,data.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0){error=-EIO;break;}at+=n;}
    if(!error&&fsync(fd)<0)error=-errno;
    if(close(fd)<0&&!error)error=-errno;
    if(!error&&rename(name.data(),path)<0)error=-errno;
    if(error)unlink(name.data());return error;
}
Bytes readCache(){Bytes data(500);FILE*f=fopen(kCache,"rb");if(!f)return{};size_t n=fread(data.data(),1,data.size(),f);bool extra=fgetc(f)!=EOF;fclose(f);if(n!=500||extra||data[0]!=1)return{};return data;}
template<size_t N>int filters(const Bytes&encoded,rtw_nan_matching_filter(&out)[N],uint8_t &count){
    size_t at=0;count=0;while(at<encoded.size()){size_t n=encoded[at++];if(n>32||n>encoded.size()-at||count==N)return -EINVAL;out[count].length=n;memcpy(out[count].filter,encoded.data()+at,n);at+=n;count++;}return 0;
}
template<class T>int common(const w::NanDiscoveryCommonConfig &c,T &p){
    if(c.serviceName.empty()||c.serviceName.size()>sizeof(p.name.name)||c.serviceSpecificInfo.size()>sizeof(p.service_specific_info.info))return -EINVAL;
    Bytes lower=c.serviceName;for(auto &v:lower)v=std::tolower(v);uint8_t hash[32];sha256(lower.data(),lower.size(),hash);memcpy(p.service_hash,hash,6);memcpy(p.name.name,c.serviceName.data(),c.serviceName.size());
    p.service_specific_info.length=c.serviceSpecificInfo.size();memcpy(p.service_specific_info.info,c.serviceSpecificInfo.data(),c.serviceSpecificInfo.size());
    p.awake_discovery_window_interval=1;p.ttl=c.ttlSec?(uint32_t(c.ttlSec)*1000+511)/512:0;p.flags=RTW_NAN_PUBLISH_FLAG_ADD_DEVICE_CAPABILITY_ATTRIBUTE;
    p.control=RTW_NAN_SDE_CTL_DATAPATH_REQ_REQUIRED|RTW_NAN_SDE_CTL_DATAPATH_TYPE;
    int r=filters(c.txMatchFilter,p.matching_filter_tx,p.matching_filter_tx_count);if(r)return r;
    return filters(c.rxMatchFilter,p.matching_filter_rx,p.matching_filter_rx_count);
}
}
VendorNan::VendorNan(Backend &b):backend_(b){
    library_=dlopen("/vendor/lib64/libgreen2pasn.so",RTLD_NOW|RTLD_LOCAL);if(!library_)return;
    create_=reinterpret_cast<decltype(create_)>(dlsym(library_,"green2_pasn_create"));destroy_=reinterpret_cast<decltype(destroy_)>(dlsym(library_,"green2_pasn_destroy"));
    start_=reinterpret_cast<decltype(start_)>(dlsym(library_,"green2_pasn_start"));receive_=reinterpret_cast<decltype(receive_)>(dlsym(library_,"green2_pasn_receive"));keys_=reinterpret_cast<decltype(keys_)>(dlsym(library_,"green2_pasn_get_keys"));
    nira_=reinterpret_cast<decltype(nira_)>(dlsym(library_,"green2_pasn_event_nira"));identityKeys_=reinterpret_cast<decltype(identityKeys_)>(dlsym(library_,"green2_pasn_identity"));verificationKey_=reinterpret_cast<decltype(verificationKey_)>(dlsym(library_,"green2_pasn_verification_key"));
    auto selftest=reinterpret_cast<int(*)()>(dlsym(library_,"green2_pasn_crypto_selftest"));
    setupIdentity_=reinterpret_cast<decltype(setupIdentity_)>(dlsym(library_,"green2_pasn_setup_identity"));
    prepareEvent_=reinterpret_cast<decltype(prepareEvent_)>(dlsym(library_,"green2_pasn_prepare_event"));
    if(!setupIdentity_||!prepareEvent_||!create_||!destroy_||!start_||!receive_||!keys_||!nira_||!identityKeys_||!verificationKey_||!selftest||selftest()){dlclose(library_);library_=nullptr;}
}
VendorNan::~VendorNan(){reset();if(library_)dlclose(library_);}
int VendorNan::command(uint8_t id,const void*p,size_t size){if(size>sizeof(nandow_para))return -EMSGSIZE;nan_customer_nandow value{};value.cmd_id=id;value.cmd_type=2;value.para_len=size;memcpy(&value.para,p,size);int r=backend_.vendor(0x1905,&value,sizeof(value));__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","NANDOW command=%u result=%d",id,r);return r;}
int VendorNan::initialize(uint8_t preference){
    if(initialized_)return 0;
    // cfg80211 already owns the active radio. Do not stop it behind its back.
    (void)preference;
    rtw_nan_enable enable{};enable.enable=1;
    int r=command(RTW_NAN_CMD_NAN_ENABLE,enable);if(r)return r;
    // WHC acknowledges START even if the firmware could not start its radio.
    // Read the actual state before publishing: uninitialized queues crash the
    // pinned vendor library. Allow the USB/power transition to settle and retry.
    bool active=false;
    for(unsigned attempt=0;attempt<20;attempt++){
        nan_customer_nandow query{};query.cmd_id=RTW_NAN_CMD_NAN_ENABLE;query.cmd_type=1;query.para_len=sizeof(enable);
        Bytes reply;int result=backend_.vendor(0x1905,&query,sizeof(query),&reply);
        if(result)return result;
        if(reply.size()==sizeof(query)&&reply[offsetof(nan_customer_nandow,para)]==1){active=true;break;}
        __android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","NAN readiness reply=%zu enabled=%d",reply.size(),reply.size()>8?reply[8]:-1);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        r=command(RTW_NAN_CMD_NAN_ENABLE,enable);if(r)return r;
    }
    if(!active)return -EAGAIN;
    if(pairingSupported()){cache_=readCache();if(!cache_.empty()){r=backend_.vendor(0x190a,cache_.data(),cache_.size());if(r)return r;}}
    initialized_=true;return 0;
}
void VendorNan::reset(){
    if(engine_){destroy_(engine_);engine_=nullptr;}
    for(auto &[id,pair]:pairs_){std::fill(pair.password.begin(),pair.password.end(),0);pair.npk.fill(0);std::fill(pair.start.begin(),pair.start.end(),0);std::fill(pair.pairingCache.begin(),pair.pairingCache.end(),0);std::fill(pair.cacheBefore.begin(),pair.cacheBefore.end(),0);}
    pairs_.clear();authenticated_.clear();cache_.clear();publications_.clear();initialized_=subscribe_=secure_=false;
}
int VendorNan::publish(const w::NanPublishRequest &m,uint8_t &id){
    if(!initialized_)return -ENETDOWN;
    bool paired=m.pairingConfig.enablePairingSetup||m.pairingConfig.enablePairingVerification;
    if(paired&&!pairingSupported())return -EOPNOTSUPP;
    uint8_t chosen=m.baseConfigs.sessionId;
    if(chosen){if(!publications_.count(chosen))return -EINVAL;return -EOPNOTSUPP;}
    for(uint8_t candidate:{1,2})if(!publications_.count(candidate)){chosen=candidate;break;}
    if(!chosen)return -ENOSPC;
    rtw_nan_publish_data p{};int r=common(m.baseConfigs,p);if(r)return r;
    p.publish_id=chosen;p.publish_type=static_cast<int>(m.publishType)==0?RTW_NAN_PUBLISH_TYPE_UNSOLICITED:RTW_NAN_PUBLISH_TYPE_SOLICITED;
    p.solicited_tx_type=1;p.qos.max_service_internal=1;p.qos.low_latency_required=1;
    if(paired){
        p.pairing_enable=1;p.npk_nik=m.pairingConfig.enablePairingCache;p.gtk=1;p.bstrap_method=m.pairingConfig.supportedBootstrappingMethods;
        p.cipher_suite_id=(1<<RTW_NAN_CIPHER_ID_NCS_SK_128)|(1<<RTW_NAN_CIPHER_ID_PASN_128);p.control|=RTW_NAN_SDE_CTL_SECURITY_REQUIRED;
        if(mkdir("/data/vendor/wifi/green2",0700)<0&&errno!=EEXIST)return -errno;
        bool nonzero=std::any_of(m.identityKey.begin(),m.identityKey.end(),[](uint8_t v){return v!=0;});if(!nonzero)return -EINVAL;
        if(!publications_.empty()&&memcmp(identity_.data(),m.identityKey.data(),16))return -EINVAL;
        if(cache_.size()!=500||memcmp(cache_.data()+4,m.identityKey.data(),16)){
            cache_.assign(500,0);cache_[0]=1;memcpy(cache_.data()+4,m.identityKey.data(),16);
            int saved=atomicWrite(kCache,cache_);if(saved)return saved;
            unlink("/data/vendor/wifi/green2/pasn_npk");
            int update=backend_.vendor(0x190a,cache_.data(),cache_.size());__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","NIK cache update result=%d",update);if(update)return update;
        }
        identity_=m.identityKey;
        if(!engine_){engine_=create_("nan0",2437);if(!engine_)return -EIO;}
    }
    r=command(RTW_NAN_CMD_PUBLISH,p);if(!r){id=chosen;publications_[id]=paired;secure_=secure_||paired;}return r;
}
int VendorNan::subscribe(const w::NanSubscribeRequest&m,uint8_t&id){
    if(!initialized_)return -ENETDOWN;
    if(m.pairingConfig.enablePairingSetup||m.pairingConfig.enablePairingVerification)return -EOPNOTSUPP;
    if(m.baseConfigs.sessionId&&uint8_t(m.baseConfigs.sessionId)!=3)return -EINVAL;
    if(subscribe_&&!m.baseConfigs.sessionId)return -ENOSPC;
    rtw_nan_subscribe_data p{};int r=common(m.baseConfigs,p);if(r)return r;p.subscribe_id=3;
    p.subscribe_type=static_cast<int>(m.subscribeType)?RTW_NAN_SUBSCRIBE_TYPE_ACTIVE:RTW_NAN_SUBSCRIBE_TYPE_PASSIVE;
    r=command(RTW_NAN_CMD_SUBSCRIBE,p);if(!r){id=3;subscribe_=true;}return r;
}
void VendorNan::terminated(uint8_t id,bool sub){
 if(sub){if(id==3)subscribe_=false;return;}
 publications_.erase(id);secure_=false;for(const auto &[key,paired]:publications_)secure_|=paired;
 if(!secure_)authenticated_.clear();
}
int VendorNan::cancel(uint8_t id,bool sub){
 int r;if(sub){if(id!=3||!subscribe_)return -ENOENT;rtw_nan_cancel_subscribe_data p{};p.subscribe_id=id;r=command(RTW_NAN_CMD_CANCEL_SUBSCRIBE,p);}
 else{if(!publications_.count(id))return -ENOENT;rtw_nan_cancel_publish_data p{};p.publish_id=id;r=command(RTW_NAN_CMD_CANCEL_PUBLISH,p);}
 if(!r)terminated(id,sub);return r;
}
int VendorNan::followup(const w::NanTransmitFollowupRequest &m){
    if(m.serviceSpecificInfo.size()>512)return -EINVAL;rtw_nan_follow_up_transmit_data p{};p.local_service_id=m.discoverySessionId;p.requestor_instance_id=m.peerId;
    memcpy(p.destination_address.ether_addr_octet,m.addr.data(),6);p.service_specific_info.length=m.serviceSpecificInfo.size();memcpy(p.service_specific_info.info,m.serviceSpecificInfo.data(),m.serviceSpecificInfo.size());return command(RTW_NAN_CMD_FOLLOWUP_TX,p);
}
VendorNan::Pair *VendorNan::peerPair(const uint8_t*peer){for(auto i=pairs_.rbegin();i!=pairs_.rend();++i)if(!memcmp(i->second.peer.data(),peer,6))return &i->second;return nullptr;}
void VendorNan::request(Pair &pair){w::NanPairingRequestInd e;e.discoverySessionId=pair.local;e.peerId=pair.remote;e.peerDiscMacAddr=pair.peer;e.pairingInstanceId=pair.id;e.requestType=pair.type==2?w::NanPairingRequestType::NAN_PAIRING_VERIFICATION:w::NanPairingRequestType::NAN_PAIRING_SETUP;e.enablePairingCache=true;if(!pair.start.empty()){int result=nira_(pair.start.data(),pair.start.size(),e.peerNira.nonce.data(),e.peerNira.tag.data());__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","pairing request id=%d type=%u NIRA parsed=%d",pair.id,pair.type,result==0);}if(pairingRequest)pairingRequest(e);}
int VendorNan::startPair(Pair &pair){
    if(!pair.accepted)return -EACCES;
    if(!pair.start.empty())return start_(engine_,pair.start.data(),pair.start.size(),reinterpret_cast<const char*>(pair.password.data()),pair.password.size());
    struct {uint8_t role,type,peer[6],method,caching;} p{};p.role=2;p.type=pair.type;p.method=1;p.caching=1;memcpy(p.peer,pair.peer.data(),6);return backend_.vendor(0x1908,&p,sizeof(p));
}
int VendorNan::respond(const w::NanRespondToPairingIndicationRequest &m){
    auto it=pairs_.find(m.pairingInstanceId);if(it==pairs_.end())return -ENOENT;auto &pair=it->second;
    if(!m.acceptRequest){fail(pair,-ECONNREFUSED);return 0;}
    if(!engine_)return -ENODEV;
    auto &password=m.securityConfig.passphrase;
    if(pair.type==1&&(password.empty()||password.size()>16))return -EINVAL;
    if(pair.type==1&&m.securityConfig.securityType!=w::NanPairingSecurityType::PASSPHRASE)return -EOPNOTSUPP;
    if(pair.type==2&&(m.securityConfig.securityType!=w::NanPairingSecurityType::PMK||!std::any_of(m.securityConfig.pmk.begin(),m.securityConfig.pmk.end(),[](uint8_t v){return v!=0;})))return -EINVAL;
    pair.password.assign(password.begin(),password.end());pair.accepted=true;if(pair.type==1)pair.cacheBefore=cache_;
    if(pair.type==2){int r=verificationKey_(engine_,m.securityConfig.pmk.data());if(r)return -EIO;}
    if(!password.empty()){struct {uint8_t peer[6];char password[17];uint8_t length;} p{};memcpy(p.peer,pair.peer.data(),6);memcpy(p.password,password.data(),password.size());p.length=password.size();int r=backend_.vendor(0x1907,&p,sizeof(p));std::fill(std::begin(p.password),std::end(p.password),0);if(r)return r;}
    int result=startPair(pair);if(result)fail(pair,result);return result;
}
bool VendorNan::authenticated(const uint8_t *peer)const{std::array<uint8_t,6>key{};memcpy(key.data(),peer,6);return authenticated_.count(key);}
void VendorNan::confirm(Pair &pair){
    if(pair.notified||!pair.keysReady||!pair.firmwareConfirmed||cache_.size()!=500)return;
    std::array<uint8_t,16> local{},peer{};
    // A setup must export the NIK received during this handshake. The loaded
    // snapshot can still contain a different identity from an earlier pairing.
    const auto &identityCache=pair.type==1?pair.pairingCache:cache_;
    if(identityCache.size()!=500)return;
    int resolved=pair.type==1?setupIdentity_(engine_,identityCache.data(),identityCache.size(),pair.cacheBefore.data(),pair.cacheBefore.size(),local.data(),peer.data()):identityKeys_(engine_,identityCache.data(),identityCache.size(),local.data(),peer.data());
    if(resolved){__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","pairing identity unresolved id=%d type=%u",pair.id,pair.type);return;}
    w::NanPairingConfirmInd e;e.pairingInstanceId=pair.id;e.pairingSuccess=true;e.status=status(0);e.enablePairingCache=true;
    e.requestType=pair.type==2?w::NanPairingRequestType::NAN_PAIRING_VERIFICATION:w::NanPairingRequestType::NAN_PAIRING_SETUP;
    e.npksa.localNanIdentityKey=local;e.npksa.peerNanIdentityKey=peer;e.npksa.npk=pair.npk;e.npksa.akm=w::NanPairingAkm::SAE;e.npksa.cipherType=w::NanCipherSuiteType::PUBLIC_KEY_PASN_128_MASK;
    authenticated_.insert(pair.peer);pair.notified=true;__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","pairing confirmation id=%d type=%u success",pair.id,pair.type);if(pairingConfirm)pairingConfirm(e);
}
void VendorNan::fail(Pair &pair,int error){
 if(pair.notified)return;
 w::NanPairingConfirmInd e;e.pairingInstanceId=pair.id;e.pairingSuccess=false;e.status=status(error);e.enablePairingCache=false;
 e.requestType=pair.type==2?w::NanPairingRequestType::NAN_PAIRING_VERIFICATION:w::NanPairingRequestType::NAN_PAIRING_SETUP;
 authenticated_.erase(pair.peer);pair.notified=true;pair.accepted=false;
 std::fill(pair.password.begin(),pair.password.end(),0);pair.npk.fill(0);std::fill(pair.start.begin(),pair.start.end(),0);std::fill(pair.pairingCache.begin(),pair.pairingCache.end(),0);std::fill(pair.cacheBefore.begin(),pair.cacheBefore.end(),0);
 if(pairingConfirm)pairingConfirm(e);
}
void VendorNan::tick(){
 auto now=std::chrono::steady_clock::now();
 for(auto it=pairs_.begin();it!=pairs_.end();){auto &p=it->second;
  if(now-p.created>std::chrono::seconds(30)){if(!p.notified)fail(p,-ETIMEDOUT);std::fill(p.password.begin(),p.password.end(),0);p.npk.fill(0);std::fill(p.start.begin(),p.start.end(),0);std::fill(p.pairingCache.begin(),p.pairingCache.end(),0);std::fill(p.cacheBefore.begin(),p.cacheBefore.end(),0);it=pairs_.erase(it);}else ++it;
 }
}
bool VendorNan::event(uint32_t cmd,const Bytes &data){
    if(cmd<29||cmd>34)return false;
    if(!secure_||!engine_)return true;
    if(cmd==32){if(data.size()!=sizeof(Bootstrap))return true;Bootstrap b{};memcpy(&b,data.data(),sizeof(b));if((b.status>>4)!=0||!securePublisher(b.local))return true;
        Pair *old=peerPair(b.peer);if(old&&!old->notified)return true;if(pairs_.size()>=8)return true;Pair p;p.id=nextPair_++;p.local=b.local;p.remote=b.remote;memcpy(p.peer.data(),b.peer,6);pairs_[p.id]=p;request(pairs_[p.id]);}
    else if(cmd==29){if(data.size()!=645||data[0]||data[1]!=2)return true;Pair *pair=peerPair(data.data()+3);
        if(!pair||pair->notified){if(pairs_.size()>=8)return true;Pair p;p.id=nextPair_++;p.local=1;p.remote=1;p.type=data[2];memcpy(p.peer.data(),data.data()+3,6);pairs_[p.id]=p;pair=&pairs_[p.id];}
        pair->start=data;int normalized=prepareEvent_(pair->start.data(),pair->start.size());if(normalized<0){fail(*pair,-EINVAL);return true;}pair->type=pair->start[2];__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","PASN start id=%d type=%u accepted=%d",pair->id,pair->type,pair->accepted);if(pair->accepted){int r=startPair(*pair);if(r)fail(*pair,r);}else request(*pair);}
    else if(cmd==30){if(data.size()!=598)return true;for(auto &[id,pair]:pairs_)if(pair.accepted&&!pair.start.empty()&&!pair.notified){int r=receive_(engine_,data.data(),data.size());uint8_t type,peer[6];if(!r&&!keys_(engine_,peer,pair.npk.data(),&type)&&!memcmp(peer,pair.peer.data(),6)){
        if(!pair.keysReady&&type==1){struct {uint8_t type,remote,local,peer[6],status,reason;uint16_t method;uint8_t key;} follow{};
            follow.type=3;follow.remote=pair.remote;follow.local=pair.local;memcpy(follow.peer,pair.peer.data(),6);follow.key=1<<3;
            r=backend_.vendor(0x1906,&follow,sizeof(follow));if(r)break;}
        __android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","PASN authenticated id=%d type=%u freshCache=%d",pair.id,type,pair.pairingCache.size()==500);pair.keysReady=true;pair.firmwareConfirmed=true;authenticated_.insert(pair.peer);confirm(pair);}break;}}
    else if(cmd==33){if(data.size()!=sizeof(Result))return true;Result r{};memcpy(&r,data.data(),sizeof(r));Pair*pair=peerPair(r.peer);if(pair&&r.status==4){pair->firmwareConfirmed=true;confirm(*pair);}}
    else if(cmd==34){if(data.size()!=500||data[0]!=1)return true;if(atomicWrite(kCache,data))return true;cache_=data;for(auto &[id,pair]:pairs_){if(pair.type==1&&pair.accepted&&!pair.start.empty()&&!pair.notified){pair.pairingCache=data;__android_log_print(ANDROID_LOG_INFO,"Green2WifiHAL","fresh NIK cache for pairing id=%d",pair.id);}confirm(pair);}}
    return true;
}
}
