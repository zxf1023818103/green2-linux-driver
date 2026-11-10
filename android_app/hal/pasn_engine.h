// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "green2_pasn.h"
#ifdef __cplusplus
extern "C" {
#endif
struct green2_pasn *green2_pasn_create(const char *interface_name,unsigned frequency);
#ifdef __cplusplus
}
#endif
