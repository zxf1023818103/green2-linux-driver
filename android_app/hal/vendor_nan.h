// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "netlink_backend.h"
#include "vendor_abi.h"
#include "pasn_engine.h"
#include <aidl/android/hardware/wifi/NanPublishRequest.h>
#include <aidl/android/hardware/wifi/NanSubscribeRequest.h>
#include <aidl/android/hardware/wifi/NanTransmitFollowupRequest.h>
#include <aidl/android/hardware/wifi/NanPairingRequestInd.h>
#include <aidl/android/hardware/wifi/NanPairingConfirmInd.h>
#include <aidl/android/hardware/wifi/NanRespondToPairingIndicationRequest.h>
#include <set>
#include <chrono>

namespace green2 {
namespace w=aidl::android::hardware::wifi;
class VendorNan {
    Backend &backend_;
    void *library_=nullptr;
    decltype(&green2_pasn_create) create_=nullptr;
    decltype(&green2_pasn_destroy) destroy_=nullptr;
    decltype(&green2_pasn_start) start_=nullptr;
    decltype(&green2_pasn_receive) receive_=nullptr;
    decltype(&green2_pasn_get_keys) keys_=nullptr;
    decltype(&green2_pasn_event_nira) nira_=nullptr;
    decltype(&green2_pasn_identity) identityKeys_=nullptr;
    decltype(&green2_pasn_verification_key) verificationKey_=nullptr;
    decltype(&green2_pasn_setup_identity) setupIdentity_=nullptr;
    decltype(&green2_pasn_prepare_event) prepareEvent_=nullptr;
    green2_pasn *engine_=nullptr;
    bool initialized_=false,subscribe_=false,secure_=false;
    std::map<uint8_t,bool>publications_;
    std::array<uint8_t,16> identity_{};
    Bytes cache_;
    struct Pair {
        std::chrono::steady_clock::time_point created=std::chrono::steady_clock::now();
        int id=0;uint8_t local=0,remote=0,type=1;
        std::array<uint8_t,6>peer{};
        Bytes start, password, pairingCache, cacheBefore;
        bool accepted=false,keysReady=false,firmwareConfirmed=false,notified=false;
        std::array<uint8_t,32>npk{};
    };
    int nextPair_=1;
    std::map<int,Pair>pairs_;
    std::set<std::array<uint8_t,6>> authenticated_;
    Pair *peerPair(const uint8_t *peer);
    void request(Pair &pair);
    int startPair(Pair &pair);
    void confirm(Pair &pair);
    void fail(Pair &pair,int error);
    int command(uint8_t id,const void *data,size_t size);
    template<class T>int command(uint8_t id,const T &data){return command(id,&data,sizeof(data));}
public:
    std::function<void(const w::NanPairingRequestInd&)> pairingRequest;
    std::function<void(const w::NanPairingConfirmInd&)> pairingConfirm;
    explicit VendorNan(Backend &backend);
    ~VendorNan();
    bool pairingSupported()const{return library_!=nullptr;}
    int initialize(uint8_t preference);
    void reset();
    int publish(const w::NanPublishRequest &request,uint8_t &id);
    int subscribe(const w::NanSubscribeRequest &request,uint8_t &id);
    int cancel(uint8_t id,bool subscription);
    void terminated(uint8_t id,bool sub);
    int followup(const w::NanTransmitFollowupRequest &request);
    bool event(uint32_t command,const Bytes &data);
    void tick();
    int respond(const w::NanRespondToPairingIndicationRequest &request);
    bool securePublisher(uint8_t id)const{auto p=publications_.find(id);return p!=publications_.end()&&p->second;}
    bool authenticated(const uint8_t *peer)const;
};
}
