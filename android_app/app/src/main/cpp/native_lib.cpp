// SPDX-License-Identifier: Apache-2.0
#include <jni.h>
#include <android/multinetwork.h>
#include <android/log.h>
#include <srt/srt.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// Avoid resolving the dependency's Java wrapper initializer through dlsym.
extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*,void*) { return JNI_VERSION_1_6; }

static int boundSocket(jlong network,const char*address,int scope,int port,int type){
    bool v6=strchr(address,':');int fd=socket(v6?AF_INET6:AF_INET,type|SOCK_CLOEXEC,0);if(fd<0)return -errno;
    int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
    if(network&&android_setsocknetwork(static_cast<net_handle_t>(network),fd)<0){int e=errno;close(fd);return -e;}
    int ret;
    if(v6&&scope>0&&!network){
        // Bind the specific NAN link-local address before IPv6 DAD completes.
        // The scope still restricts this listener to the Aware interface.
        constexpr int ipv6Freebind=78;
        if(setsockopt(fd,IPPROTO_IPV6,ipv6Freebind,&one,sizeof(one))<0){int e=errno;close(fd);return -e;}
    }
    if(v6){sockaddr_in6 a{};a.sin6_family=AF_INET6;a.sin6_port=htons(port);a.sin6_scope_id=scope;
        if(inet_pton(AF_INET6,address,&a.sin6_addr)!=1){close(fd);return -EINVAL;}
        ret=bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a));}
    else{sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);
        if(inet_pton(AF_INET,address,&a.sin_addr)!=1){close(fd);return -EINVAL;}
        ret=bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a));}
    if(ret<0){int e=errno;close(fd);return -e;}return fd;
}
#define JNI_METHOD(name) Java_cn_zenghome_flashstudio_NativeTransport_##name
extern "C" JNIEXPORT jint JNICALL JNI_METHOD(listen)(JNIEnv*env,jclass,jlong network,jstring address,jint scope,jint port){
    const char*s=env->GetStringUTFChars(address,nullptr);if(!s)return -ENOMEM;
    int fd=boundSocket(network,s,scope,port,SOCK_STREAM);env->ReleaseStringUTFChars(address,s);
    if(fd>=0&&listen(fd,8)<0){int e=errno;close(fd);return -e;}return fd;
}
extern "C" JNIEXPORT jint JNICALL JNI_METHOD(accept)(JNIEnv*,jclass,jint fd){
    int n;do{n=accept4(fd,nullptr,nullptr,SOCK_CLOEXEC);}while(n<0&&errno==EINTR);return n<0?-errno:n;
}
struct Video {
    std::mutex mutex;std::condition_variable ready;std::atomic<bool> stop{false};
    std::vector<char> latest;int64_t epoch=0,received=0;uint64_t sequence=0;
    SRTSOCKET server=SRT_INVALID_SOCK;std::thread sender;
    static int64_t now(){return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    bool send(SRTSOCKET peer,const char*p,size_t size){
        while(size&&!stop){int n=int(size>1316?1316:size);if(srt_sendmsg(peer,p,n,-1,0)!=n)return false;p+=n;size-=n;}return !stop;
    }
    void run(){
        int poller=srt_epoll_create();int events=SRT_EPOLL_IN;srt_epoll_add_usock(poller,server,&events);
        SRTSOCKET peer=SRT_INVALID_SOCK;
        while(!stop){
            SRT_EPOLL_EVENT event{};
            if(srt_epoll_uwait(poller,&event,1,peer==SRT_INVALID_SOCK?100:0)>0){
                SRTSOCKET next=srt_accept(server,nullptr,nullptr);
                if(next!=SRT_INVALID_SOCK){if(peer!=SRT_INVALID_SOCK)srt_close(peer);peer=next;}
            }
            if(peer==SRT_INVALID_SOCK)continue;
            std::vector<char> frame;int64_t timestamp,at;uint64_t seq;
            {std::unique_lock<std::mutex>lock(mutex);ready.wait_for(lock,std::chrono::milliseconds(100),[&]{return stop||!latest.empty();});
                if(stop)break;frame.swap(latest);timestamp=epoch;at=received;seq=sequence;}
            if(frame.empty()||now()-at>200000)continue;
            char header[256];int len=snprintf(header,sizeof(header),"--ffmpeg\r\nContent-type: image/jpeg\r\nContent-length: %zu\r\nX-Capture-Unix-Us: %lld\r\nX-Sequence: %llu\r\n\r\n",frame.size(),(long long)timestamp,(unsigned long long)seq);
            if(!send(peer,header,len)||!send(peer,frame.data(),frame.size())||!send(peer,"\r\n",2)){srt_close(peer);peer=SRT_INVALID_SOCK;}
        }
        if(peer!=SRT_INVALID_SOCK)srt_close(peer);srt_epoll_release(poller);
    }
    ~Video(){stop=true;ready.notify_all();if(sender.joinable())sender.join();if(server!=SRT_INVALID_SOCK)srt_close(server);}
};
template<class T>static bool option(SRTSOCKET s,SRT_SOCKOPT key,T value){return srt_setsockflag(s,key,&value,sizeof(value))!=SRT_ERROR;}
extern "C" JNIEXPORT jlong JNICALL JNI_METHOD(startVideo)(JNIEnv*env,jclass,jlong network,jstring address,jint scope,jint port){
    static std::once_flag initialized;std::call_once(initialized,[]{srt_startup();});
    const char*s=env->GetStringUTFChars(address,nullptr);if(!s)return 0;
    int fd=boundSocket(network,s,scope,port,SOCK_DGRAM);env->ReleaseStringUTFChars(address,s);if(fd<0)return 0;
    auto*v=new Video;v->server=srt_create_socket();auto server=v->server;
    bool ok=server!=SRT_INVALID_SOCK&&option(server,SRTO_TRANSTYPE,SRTT_LIVE)&&option(server,SRTO_SENDER,true)&&
        option(server,SRTO_TLPKTDROP,true)&&option(server,SRTO_PEERLATENCY,150)&&option(server,SRTO_SNDDROPDELAY,0)&&
        option(server,SRTO_SNDBUF,262144)&&option(server,SRTO_SNDTIMEO,100)&&option(server,SRTO_PEERIDLETIMEO,3000)&&
        option(server,SRTO_LINGER,linger{0,0})&&option(server,SRTO_MAXBW,int64_t{1500000})&&option(server,SRTO_RCVSYN,false);
    if(!ok){close(fd);delete v;return 0;}
    if(srt_bind_acquire(server,fd)==SRT_ERROR){close(fd);delete v;return 0;}
    if(srt_listen(server,1)==SRT_ERROR){delete v;return 0;}
    v->sender=std::thread([v]{v->run();});return reinterpret_cast<jlong>(v);
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(sendJpeg)(JNIEnv*env,jclass,jlong handle,jbyteArray bytes,jlong timestamp,jlong sequence){
    auto*v=reinterpret_cast<Video*>(handle);if(!v)return;jsize n=env->GetArrayLength(bytes);if(n<4||n>4*1024*1024)return;
    std::vector<char> jpeg(n);env->GetByteArrayRegion(bytes,0,n,reinterpret_cast<jbyte*>(jpeg.data()));if(env->ExceptionCheck())return;
    if((unsigned char)jpeg[0]!=0xff||(unsigned char)jpeg[1]!=0xd8||(unsigned char)jpeg[n-2]!=0xff||(unsigned char)jpeg[n-1]!=0xd9)return;
    {std::lock_guard<std::mutex>lock(v->mutex);v->latest.swap(jpeg);v->epoch=timestamp;v->received=Video::now();v->sequence=sequence;}v->ready.notify_one();
}
extern "C" JNIEXPORT void JNICALL JNI_METHOD(stopVideo)(JNIEnv*,jclass,jlong handle){delete reinterpret_cast<Video*>(handle);}

// Exercise the same SRT sender and multipart framing without a camera or peer device.
extern "C" JNIEXPORT jboolean JNICALL JNI_METHOD(checkVideo)(JNIEnv*env,jclass cls){
    jstring address=env->NewStringUTF("127.0.0.1");
    jlong handle=JNI_METHOD(startVideo)(env,cls,0,address,0,19000);env->DeleteLocalRef(address);
    if(!handle)return false;
    SRTSOCKET receiver=srt_create_socket();
    bool ok=receiver!=SRT_INVALID_SOCK&&option(receiver,SRTO_TRANSTYPE,SRTT_LIVE)&&
        option(receiver,SRTO_RCVTIMEO,3000)&&option(receiver,SRTO_CONNTIMEO,3000)&&option(receiver,SRTO_LINGER,linger{0,0});
    sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(19000);inet_pton(AF_INET,"127.0.0.1",&remote.sin_addr);
    if(ok)ok=srt_connect(receiver,reinterpret_cast<sockaddr*>(&remote),sizeof(remote))!=SRT_ERROR;
    if(ok){
        const jbyte fixture[]={jbyte(0xff),jbyte(0xd8),1,2,jbyte(0xff),jbyte(0xd9)};
        jbyteArray frame=env->NewByteArray(sizeof(fixture));env->SetByteArrayRegion(frame,0,sizeof(fixture),fixture);
        JNI_METHOD(sendJpeg)(env,cls,handle,frame,123456789,7);env->DeleteLocalRef(frame);
        std::vector<char> received;
        for(int part=0;part<3&&ok;part++){char data[1500];int n=srt_recvmsg(receiver,data,sizeof(data));if(n<=0){ok=false;break;}received.insert(received.end(),data,data+n);}
        const char header[]="--ffmpeg\r\nContent-type: image/jpeg\r\nContent-length: 6\r\nX-Capture-Unix-Us: 123456789\r\nX-Sequence: 7\r\n\r\n";
        ok=ok&&received.size()==sizeof(header)-1+sizeof(fixture)+2&&
            !memcmp(received.data(),header,sizeof(header)-1)&&!memcmp(received.data()+sizeof(header)-1,fixture,sizeof(fixture))&&
            received[received.size()-2]=='\r'&&received.back()=='\n';
    }
    if(receiver!=SRT_INVALID_SOCK)srt_close(receiver);
    JNI_METHOD(stopVideo)(env,cls,handle);return ok;
}
