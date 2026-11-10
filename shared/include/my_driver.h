/* Public Green2 WHC hardware identifiers, shared by Linux and Android. */
#ifndef GREEN2_MY_DRIVER_H
#define GREEN2_MY_DRIVER_H
#include "protocol_common.h"
#define GREEN2_WHC_USB_VENDOR_ID 0x0bda
#define GREEN2_WHC_USB_PRODUCT_NIC 0x8006
#define GREEN2_WHC_USB_PRODUCT_IOT 0xf851
/* Control/discovery use cfg80211/nl80211 and the existing Realtek vendor ABI.
 * No additional device ioctl or Android-specific kernel interface is added.
 */
#endif
