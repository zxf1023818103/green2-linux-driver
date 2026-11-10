/* Shared wire definitions: no Android Binder, hostap, libnl or OS handles. */
#ifndef GREEN2_PROTOCOL_COMMON_H
#define GREEN2_PROTOCOL_COMMON_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef __u8 green2_u8;typedef __u16 green2_u16;typedef __u32 green2_u32;
#else
#include <stddef.h>
#include <stdint.h>
typedef uint8_t green2_u8;typedef uint16_t green2_u16;typedef uint32_t green2_u32;
#endif
#define GREEN2_REALTEK_OUI 0x00e04c
#define GREEN2_PASN_SET_KEY 0x1909
#define GREEN2_PASN_START_LEN 645
#define GREEN2_PASN_RX_LEN 598
#define GREEN2_AUTH_MAX 512
#define GREEN2_ATTR_MAX 64
#define GREEN2_PASS_MAX 16
#define GREEN2_NIK_CACHE_LEN 500
#define GREEN2_NAN_ATTR_NIRA 0x2b
#define GREEN2_NIRA_CIPHER_128 0
#pragma pack(push, 1)
struct green2_pasn_start_event {
	green2_u8 deinit, role, pairing_type, peer[6], bssid[6];
	green2_u8 auth_method, nik_caching, pass_len, pass[GREEN2_PASS_MAX];
	green2_u16 frame_len;
	green2_u8 frame[GREEN2_AUTH_MAX];
	green2_u8 attrs_len, attrs[GREEN2_ATTR_MAX], own_pmkid[16], peer_pmkid[16];
};

struct green2_pasn_rx_event {
	green2_u8 seq, pairing_type, pass_len, pass[GREEN2_PASS_MAX];
	green2_u16 frame_len;
	green2_u8 frame[GREEN2_AUTH_MAX];
	green2_u8 attrs_len, attrs[GREEN2_ATTR_MAX];
};

#pragma pack(pop)
struct green2_nik_cache_entry {
	green2_u32 lifetime, start_ms;
	green2_u8 role, auth, peer_inst, nik[16], pmkid[16], pass_len, pass[16];
};

struct green2_nik_cache_file {
	green2_u8 version, reserved[3], own_nik[16];
	struct green2_nik_cache_entry entries[8];
};


#if defined(__cplusplus)
static_assert(sizeof(struct green2_pasn_start_event)==645,"PASN start ABI");
static_assert(sizeof(struct green2_pasn_rx_event)==598,"PASN RX ABI");
static_assert(sizeof(struct green2_nik_cache_file)==500,"NIK cache ABI");
#else
_Static_assert(sizeof(struct green2_pasn_start_event)==645,"PASN start ABI");
_Static_assert(sizeof(struct green2_pasn_rx_event)==598,"PASN RX ABI");
_Static_assert(sizeof(struct green2_nik_cache_file)==500,"NIK cache ABI");
#endif
#ifndef __KERNEL__
#ifdef __cplusplus
extern "C" {
#endif
int green2_protocol_nira(const green2_u8 *frame,size_t length,green2_u8 nonce[8],green2_u8 tag[8]);
int green2_cache_delta(const void *current,size_t length,const void *previous,size_t previous_length,struct green2_nik_cache_file *changed);
void green2_protocol_clear(void *memory,size_t length);
#ifdef __cplusplus
}
#endif
#endif
#endif
