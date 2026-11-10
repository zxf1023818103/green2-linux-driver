// SPDX-License-Identifier: Apache-2.0
#include "pasn_engine.h"
#include <android/log.h>
static void android_pasn_log(void *context,int level,const char *message)
{
    (void)context;
    __android_log_write(level?ANDROID_LOG_WARN:ANDROID_LOG_INFO,"Green2PASN",message);
}
struct green2_pasn *green2_pasn_create(const char *name,unsigned frequency)
{
    struct green2_pasn_config config={.interface_name=name,.frequency=frequency,
        .nik_cache_path="/data/vendor/wifi/green2/nik_cache",
        .npk_path="/data/vendor/wifi/green2/pasn_npk",.log=android_pasn_log};
    return green2_pasn_create_config(&config);
}
