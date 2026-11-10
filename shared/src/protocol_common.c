#include "protocol_common.h"
#include <string.h>

void green2_protocol_clear(void *memory,size_t length)
{
    volatile green2_u8 *p=memory;while(length--)*p++=0;
}
int green2_protocol_nira(const green2_u8 *frame,size_t length,green2_u8 nonce[8],green2_u8 tag[8])
{
    if(!frame||!nonce||!tag||length<30)return -1;
    size_t pos=30;
    while(pos+2<=length){size_t n=frame[pos+1];if(n>length-pos-2)return -1;
        const green2_u8 *value=frame+pos+2;
        if(frame[pos]==221&&n>=4&&!memcmp(value,"\x50\x6f\x9a\x13",4)){
            size_t at=4;
            while(at+3<=n){green2_u8 id=value[at];size_t size=value[at+1]|((size_t)value[at+2]<<8);at+=3;if(size>n-at)return -1;
                if(id==GREEN2_NAN_ATTR_NIRA&&size==17&&value[at]==GREEN2_NIRA_CIPHER_128){memcpy(nonce,value+at+1,8);memcpy(tag,value+at+9,8);return 0;}at+=size;}
        }pos+=2+n;
    }return -1;
}
int green2_cache_delta(const void *current,size_t length,const void *previous,size_t previous_length,struct green2_nik_cache_file *changed)
{
    if(!current||!previous||!changed||length!=sizeof(*changed)||previous_length!=length)return -1;
    struct green2_nik_cache_file now,before;memcpy(&now,current,sizeof(now));memcpy(&before,previous,sizeof(before));
    if(now.version!=1||before.version!=1||memcmp(now.own_nik,before.own_nik,16)){
        green2_protocol_clear(&now,sizeof(now));green2_protocol_clear(&before,sizeof(before));return -1;
    }
    for(size_t i=0;i<8;i++)if(!memcmp(&now.entries[i],&before.entries[i],sizeof(now.entries[i])))memset(&now.entries[i],0,sizeof(now.entries[i]));
    memcpy(changed,&now,sizeof(now));green2_protocol_clear(&now,sizeof(now));green2_protocol_clear(&before,sizeof(before));return 0;
}
