/* Experimental Green2 NAN PASN responder for pasn_vendor_events input
 * (also accepts the equivalent debug wpa_supplicant event format).
 * Keeps the PASN state through Auth3 and installs the derived keys.
 */
#include <net/if.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include "green2_pasn.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/nl80211.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>

#include "utils/includes.h"
#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_defs.h"
#include "common/nan_defs.h"
#include "common/sae.h"
#include "common/wpa_common.h"
#include "crypto/sha256.h"
#include "pasn/pasn_common.h"

#define PASN_START_LEN 645
#define PASN_RX_LEN 598
#define AUTH_MAX 512
#define ATTR_MAX 64
#undef PASS_MAX
#define PASS_MAX 16
#define REALTEK_OUI 0x00e04c
#define NAN_SUBCMD_PASN_SET_KEY 0x1909
#define PAIRING_SETUP 1
#define PAIRING_VERIFICATION 2

struct pasn_start_event {
	u8 deinit, role, pairing_type, peer[6], bssid[6];
	u8 auth_method, nik_caching, pass_len, pass[PASS_MAX];
	u16 frame_len;
	u8 frame[AUTH_MAX];
	u8 attrs_len, attrs[ATTR_MAX], own_pmkid[16], peer_pmkid[16];
} __attribute__((packed));

struct pasn_rx_event {
	u8 seq, pairing_type, pass_len, pass[PASS_MAX];
	u16 frame_len;
	u8 frame[AUTH_MAX];
	u8 attrs_len, attrs[ATTR_MAX];
} __attribute__((packed));

struct nik_cache_entry {
	u32 lifetime, start_ms;
	u8 role, auth, peer_inst, nik[16], pmkid[16], pass_len, pass[16];
};

struct nik_cache_file {
	u8 version, reserved[3], own_nik[16];
	struct nik_cache_entry entries[8];
};

_Static_assert(sizeof(struct pasn_start_event) == PASN_START_LEN,
	       "PASN start event ABI changed");
_Static_assert(sizeof(struct pasn_rx_event) == PASN_RX_LEN,
	       "PASN rx event ABI changed");
_Static_assert(sizeof(struct nik_cache_file) == 500,
	       "Green2 NIK cache ABI changed");

struct bridge {
    char *nik_cache_path, *npk_path, *result_path;
    void *context;
    int (*read_store)(void *,enum green2_pasn_store,void *,size_t);
    int (*write_store)(void *,enum green2_pasn_store,const void *,size_t);
    void (*log)(void *,int,const char *);
    void (*notify_keys_installed)(void *,uint8_t,const uint8_t[6]);
	struct nl_sock *nl;
	int family;
	unsigned int ifindex;
	unsigned int freq;
	struct rsn_pmksa_cache *pmksa;
	struct pasn_data *pasn;
	struct sae_pt *pt;
	u8 own[6], peer[6], bssid[6];
	u8 pairing_type;
	u8 verified_peer_pmkid[16];
	bool peer_pmkid_valid;
	unsigned int auth2_sent;
	u8 auth1_frame[AUTH_MAX], auth2_frame[AUTH_MAX];
	size_t auth1_len, auth2_len;
	struct timespec auth1_started;
	bool keys_installed;
};

static void pasn_log(struct bridge *b,int level,const char *format,...)
{
    char message[512];va_list args;va_start(args,format);
    vsnprintf(message,sizeof(message),format,args);va_end(args);
    if(b->log)b->log(b->context,level,message);
    else {FILE *stream=level?stderr:stdout;fputs(message,stream);fflush(stream);}
}
static const char *store_path(struct bridge *b,enum green2_pasn_store store)
{
    return store==GREEN2_PASN_NIK_CACHE?b->nik_cache_path:b->npk_path;
}
static int read_store(struct bridge *b,enum green2_pasn_store store,void *data,size_t size)
{
    if(b->read_store)return b->read_store(b->context,store,data,size);
    const char *path=store_path(b,store);if(!path||!*path)return -1;
    FILE *file=fopen(path,"rb");if(!file)return -1;
    int result=fread(data,1,size,file)==size&&fgetc(file)==EOF?0:-1;
    if(fclose(file))result=-1;return result;
}
static int write_store(struct bridge *b,enum green2_pasn_store store,const void *data,size_t size)
{
    if(b->write_store)return b->write_store(b->context,store,data,size);
    const char *path=store_path(b,store);if(!path||!*path)return -1;
    char name[PATH_MAX];if(snprintf(name,sizeof(name),"%s.XXXXXX",path)>=(int)sizeof(name))return -1;
    int fd=mkstemp(name);if(fd<0)return -1;
    int result=write(fd,data,size)==(ssize_t)size?0:-1;
    if(!result&&fsync(fd))result=-1;if(close(fd))result=-1;
    if(!result&&rename(name,path))result=-1;if(result)unlink(name);return result;
}
static int store_npk(struct bridge *b)
{
    u8 record[36];if(pasn_get_pmk_len(b->pasn)!=32)return -1;
    memcpy(record,"NPK1",4);memcpy(record+4,pasn_get_pmk(b->pasn),32);
    int result=write_store(b,GREEN2_PASN_NPK,record,sizeof(record));
    forced_memzero(record,sizeof(record));return result;
}

static int prepare_verification(struct bridge *b, const u8 *frame,
				 size_t frame_len, const u8 own_pmkid[16])
{
	struct nik_cache_file cache = {0};
	struct wpa_ie_data rsn = {0};
	u8 record[4 + 32] = {0}, message[3 + 6 + 8], digest[32];
	const u8 *nonce = NULL, *tag = NULL;
	size_t pos = 30;
	int i, match = -1, ret = -1;
	const char *failure = "malformed verification frame";

	if (frame_len < pos)
		goto done;
	while (pos + 2 <= frame_len) {
		u8 id = frame[pos], len = frame[pos + 1];
		const u8 *value = frame + pos + 2;
		size_t end = pos + 2 + len;
		if (end > frame_len) goto done;
		if (id == WLAN_EID_RSN &&
		    wpa_parse_wpa_ie_rsn(frame + pos, 2 + len, &rsn))
			goto done;
		if (id == WLAN_EID_VENDOR_SPECIFIC && len >= 4 &&
		    !memcmp(value, "\x50\x6f\x9a\x13", 4)) {
			size_t ap = 4;
			while (ap + 3 <= len) {
				u8 aid = value[ap];
				u16 alen = value[ap + 1] | (value[ap + 2] << 8);
				ap += 3;
				if (ap + alen > len) goto done;
				if (aid == NAN_ATTR_NIRA && alen == 17 &&
				    value[ap] == NAN_NIRA_CIPHER_VER_128) {
					nonce = value + ap + 1;
					tag = value + ap + 9;
				}
				ap += alen;
			}
		}
		pos = end;
	}
	if (!nonce || !tag || rsn.num_pmkid != 1 || !rsn.pmkid ||
	    memcmp(rsn.pmkid, nonce, 8) || memcmp(rsn.pmkid + 8, tag, 8))
		goto done;
	failure = "NIK snapshot unavailable";
    if(read_store(b,GREEN2_PASN_NIK_CACHE,&cache,sizeof(cache)))goto done;
    failure="NIK snapshot incomplete or invalid";
    if(cache.version!=1)goto done;
	memcpy(message, "NIR", 3);
	memcpy(message + 3, b->peer, 6);
	memcpy(message + 9, nonce, 8);
	for (i = 0; i < 8; i++) {
		if (hmac_sha256(cache.entries[i].nik, 16,
				message, sizeof(message), digest) == 0 &&
		    os_memcmp_const(digest, tag, 8) == 0) {
			match = i;
			break;
		}
	}
	failure = "NIRA does not match cached identity";
	if (match < 0) goto done;
	failure = "NPK snapshot unavailable";
    if(read_store(b,GREEN2_PASN_NPK,record,sizeof(record)))goto done;
    failure="NPK snapshot incomplete or invalid";
    if(memcmp(record,"NPK1",4))goto done;
	failure = "PMKSA insertion failed";
	if (pasn_responder_pmksa_cache_add(b->pmksa, b->own, b->peer,
				    record + 4, 32, rsn.pmkid,
				    WPA_KEY_MGMT_SAE))
		goto done;
	pasn_set_custom_pmkid(b->pasn, own_pmkid);
	memcpy(b->verified_peer_pmkid, rsn.pmkid,
	       sizeof(b->verified_peer_pmkid));
	b->peer_pmkid_valid = true;
	pasn_log(b,0,"Validated NIRA and loaded NPK for verification peer="
	       MACSTR "\n", MAC2STR(b->peer));

	ret = 0;
done:
	if (ret) pasn_log(b,1, "Verification rejected: %s\n", failure);
	forced_memzero(&cache, sizeof(cache));
	forced_memzero(record, sizeof(record));
	forced_memzero(digest, sizeof(digest));
	return ret;
}

/* hostap requires this callback whenever a custom outgoing PMKID is set.
 * Accept only the incoming PMKID whose NIRA was authenticated for this peer
 * by prepare_verification(); never accept an arbitrary cached-key request.
 */
static int validate_custom_pmkid(void *ctx, const u8 *addr, const u8 *pmkid)
{
	struct bridge *b = ctx;

	if (!addr || !pmkid || b->pairing_type != PAIRING_VERIFICATION ||
	    !b->peer_pmkid_valid || memcmp(addr, b->peer, sizeof(b->peer)))
		return -1;
	return os_memcmp_const(pmkid, b->verified_peer_pmkid,
			      sizeof(b->verified_peer_pmkid)) ? -1 : 0;
}

/* libpasn.so includes AP PMKSA objects. No AP/RADIUS state exists here. */
struct hostapd_data;
struct sta_info;
struct vlan_description;
struct radius_class_data;
int ap_sta_set_vlan(struct hostapd_data *hapd, struct sta_info *sta,
		    struct vlan_description *desc)
{
	(void) hapd; (void) sta; (void) desc;
	return -1;
}
void radius_free_class(struct radius_class_data *data)
{
	(void) data;
}
int radius_copy_class(struct radius_class_data *dst,
		      const struct radius_class_data *src)
{
	(void) dst; (void) src;
	return -1;
}

static int hex_line(const char *line, u8 *out, size_t len)
{
	const char *p = strstr(line, "hexdump(len=");
	size_t n = 0;
	unsigned int byte;

	if (!p || !(p = strstr(p, "): ")))
		return -1;
	p += 3;
	while (*p && n < len) {
		while (*p == ' ' || *p == '\t') p++;
		if (sscanf(p, "%2x", &byte) != 1) break;
		out[n++] = byte;
		if (!p[0] || !p[1]) break;
		p += 2;
		if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0')
			return -1;
	}
	return n == len ? 0 : -1;
}

static int send_mgmt(void *ctx, const u8 *frame, size_t len, int noack,
		     unsigned int freq, unsigned int wait)
{
	struct bridge *b = ctx;
	struct nl_msg *msg;
	int ret;

	(void) noack; (void) freq; (void) wait;
	if (len < 30 || len > AUTH_MAX || frame[0] != 0xb0 || frame[1] != 0 ||
	    frame[24] != 7 || frame[26] != 2 || frame[28] != 0 ||
	    frame[29] != 0 || memcmp(frame + 4, b->peer, 6) ||
	    memcmp(frame + 10, b->own, 6)) {
		pasn_log(b,1, "refusing invalid Auth2\n");
		return -1;
	}
	msg = nlmsg_alloc();
	if (!msg)
		return -1;
	if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, b->family, 0, 0,
			 NL80211_CMD_FRAME, 0) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, b->ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, b->freq) ||
	    nla_put(msg, NL80211_ATTR_FRAME, len, frame)) {
		nlmsg_free(msg);
		return -1;
	}
	ret = nl_send_auto(b->nl, msg);
	if (ret >= 0) ret = nl_wait_for_ack(b->nl);
	nlmsg_free(msg);
	if (ret < 0) {
		pasn_log(b,1, "Auth2 rejected by nl80211: %s\n", nl_geterror(ret));
		return -1;
	}
	b->auth2_sent++;
	if (frame != b->auth2_frame)
		memcpy(b->auth2_frame, frame, len);
	b->auth2_len = len;
	pasn_log(b,0,"Auth2 submitted: %zu bytes, peer=" MACSTR "\n",
	       len, MAC2STR(b->peer));

	return 0;
}

/* Firmware ABI recovered from rtw_cfgvendor_nan_pasn_setkeys in the matching
 * Green2 lib_wifi_whc_dev.a: TK[16] | KDK[32] | peer NMI[6] | ND-PMK[32].
 * rtw_nan_kdk_to_kek and rtw_nan_kdk_to_pmk consume the field at offset 16.
 */
static int install_keys(struct bridge *b)
{
	u8 payload[16 + 32 + 6 + 32] = {0};
	u8 nmi_pair[12];
	struct nl_msg *msg = NULL;
	int ret = -1;

	if (b->pasn->ptk.tk_len != 16 || b->pasn->ptk.kdk_len != 32)
		return -1;
	memcpy(payload, b->pasn->ptk.tk, 16);
	memcpy(payload + 16, b->pasn->ptk.kdk, 32);
	memcpy(nmi_pair, b->peer, 6); /* iPhone is pairing initiator */
	memcpy(nmi_pair + 6, b->own, 6);
	if (sha256_prf(b->pasn->ptk.kdk, 32, "NDP PMK Derivation",
		       nmi_pair, sizeof(nmi_pair), payload + 54, 32))
		goto done;
	memcpy(payload + 48, b->peer, 6);
	msg = nlmsg_alloc();
	if (!msg || !genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, b->family,
				 0, 0, NL80211_CMD_VENDOR, 0) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, b->ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, REALTEK_OUI) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			NAN_SUBCMD_PASN_SET_KEY) ||
	    nla_put(msg, NL80211_ATTR_VENDOR_DATA,
		    sizeof(payload), payload)) {
		pasn_log(b,1, "could not construct PASN set-key command\n");
		goto done;
	}
	ret = nl_send_auto(b->nl, msg);
	if (ret >= 0) ret = nl_wait_for_ack(b->nl);
	if (ret < 0)
		pasn_log(b,1, "PASN set-key rejected: %s\n", nl_geterror(ret));
	else {
		const char *path = b->result_path;
		char tmp[PATH_MAX];
		int fd, write_ok, close_ok;

		pasn_log(b,0,"PASN keys submitted for peer=" MACSTR "\n",
		       MAC2STR(b->peer));

		if (path && *path) {
			if (snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path) >=
			    (int) sizeof(tmp) || (fd = mkstemp(tmp)) < 0) {
				pasn_log(b,1, "PASN completion notification failed\n");
			} else {
				u8 result[1 + 6];
				result[0] = b->pairing_type;
				memcpy(result + 1, b->peer, 6);
				write_ok = write(fd, result, sizeof(result)) ==
					   sizeof(result);
				close_ok = close(fd) == 0;
				if (!write_ok || !close_ok || rename(tmp, path) < 0) {
					unlink(tmp);
					pasn_log(b,1, "PASN completion notification failed\n");
				} else {
					pasn_log(b,0,"PASN completion notification queued for peer="
					       MACSTR "\n", MAC2STR(b->peer));

				}
			}
		}
	}
done:
	nlmsg_free(msg);
	forced_memzero(payload, sizeof(payload));
	forced_memzero(nmi_pair, sizeof(nmi_pair));
	return ret < 0 ? -1 : 0;
}

static void reset_session(struct bridge *b)
{
	if (b->pasn) pasn_data_deinit(b->pasn);
	if (b->pt) sae_deinit_pt(b->pt);
	b->pasn = NULL;
	b->pt = NULL;
	b->auth2_sent = 0;
	b->auth1_len = b->auth2_len = 0;
	b->keys_installed = false;
	memset(b->auth1_frame, 0, sizeof(b->auth1_frame));
	memset(b->auth2_frame, 0, sizeof(b->auth2_frame));
	b->pairing_type = 0;
	b->peer_pmkid_valid = false;
	memset(b->verified_peer_pmkid, 0, sizeof(b->verified_peer_pmkid));
	memset(b->own, 0, sizeof(b->own));
	memset(b->peer, 0, sizeof(b->peer));
	memset(b->bssid, 0, sizeof(b->bssid));
}

/* After a previous verification, Green2 may label a new PIN Auth1 as
 * verification. Only recognize an unambiguous SAE setup on the wire: no
 * cached PMKID/NIRA, SAE commit, matching PASN parameters and bootstrap IE.
 * This selects the handshake; libpasn still authenticates the PIN and MIC.
 */
static bool auth1_is_sae_setup(const u8 *frame, size_t len,
			       const u8 **npba, size_t *npba_len)
{
	bool rsn_seen = false, wrapped_seen = false, params_seen = false;
	bool bootstrap_seen = false;
	size_t pos = offsetof(struct ieee80211_mgmt, u.auth.variable);
	struct wpa_ie_data rsn;
	struct wpa_pasn_params_data params;

	if (!frame || len < pos)
		return false;
	while (pos < len) {
		const u8 *ie = frame + pos;
		const u8 *data = ie + 2;
		size_t n;
		if (len - pos < 2 || (n = ie[1]) > len - pos - 2)
			return false;
		if (ie[0] == WLAN_EID_RSN) {
			if (rsn_seen || wpa_parse_wpa_ie_rsn(ie, n + 2, &rsn) ||
			    rsn.key_mgmt != WPA_KEY_MGMT_SAE || rsn.num_pmkid ||
			    rsn.pairwise_cipher != WPA_CIPHER_CCMP)
				return false;
			rsn_seen = true;
		} else if (ie[0] == WLAN_EID_EXTENSION && n) {
			if (data[0] == WLAN_EID_EXT_WRAPPED_DATA) {
				if (wrapped_seen || n < 9 ||
				    WPA_GET_LE16(data + 1) != WLAN_AUTH_SAE ||
				    WPA_GET_LE16(data + 3) != 1 ||
				    WPA_GET_LE16(data + 5) != WLAN_STATUS_SAE_HASH_TO_ELEMENT ||
				    WPA_GET_LE16(data + 7) != 19)
					return false;
				wrapped_seen = true;
			} else if (data[0] == WLAN_EID_EXT_PASN_PARAMS) {
				if (params_seen || n > 253 ||
				    wpa_pasn_parse_parameter_ie(ie, n + 2, false, &params) ||
				    params.wrapped_data_format != WPA_PASN_WRAPPED_DATA_SAE ||
				    params.group != 19 || !params.pubkey_len)
					return false;
				params_seen = true;
			}
		} else if (ie[0] == WLAN_EID_VENDOR_SPECIFIC && n >= 4 &&
			   !memcmp(data, "\x50\x6f\x9a\x13", 4)) {
			size_t a = 4;
			while (a < n) {
				u16 size;
				if (n - a < 3 || (size = WPA_GET_LE16(data + a + 1)) > n - a - 3)
					return false;
				if (data[a] == NAN_ATTR_NIRA)
					return false;
				if (data[a] == NAN_ATTR_NPBA) {
					if (bootstrap_seen || size < 5)
						return false;
					bootstrap_seen = true;
					if (npba) *npba = data + a;
					if (npba_len) *npba_len = 3 + size;
				}
				a += 3 + size;
			}
		}
		pos += 2 + n;
	}
	return rsn_seen && wrapped_seen && params_seen && bootstrap_seen;
}

/* The stale firmware state affects the Auth2 attributes as well as the event
 * type. Keep its local CSIA (including publish IDs), replace verification NIRA
 * with local DCEA and echo the setup NPBA. hostap's nan_bootstrap_rx_request()
 * saves the initiator's NPBA for nan_pairing_prepare_pasn_elems() in this role.
 * Build separately so a malformed/oversized event is never partially changed.
 */
static int correct_setup_event(struct pasn_start_event *e)
{
	u8 attrs[ATTR_MAX], dcea[] = { NAN_ATTR_DCEA, 2, 0, 0,
		NAN_DEV_CAPA_EXT_INFO_1_PAIRING_SETUP };
	const u8 *npba = NULL;
	size_t npba_len = 0, pos = 0, used = 0;
	bool csia_seen = false, dcea_seen = false;

	if (e->frame_len > AUTH_MAX || e->attrs_len > ATTR_MAX)
		return -1;
	if (e->pairing_type != PAIRING_VERIFICATION ||
	    !auth1_is_sae_setup(e->frame, e->frame_len, &npba, &npba_len))
		return 0;
	if (e->nik_caching)
		dcea[4] |= NAN_DEV_CAPA_EXT_INFO_1_NPK_NIK_CACHING;
	while (pos < e->attrs_len) {
		const u8 *a = e->attrs + pos;
		size_t n;
		if (e->attrs_len - pos < 3 ||
		    (n = WPA_GET_LE16(a + 1)) > e->attrs_len - pos - 3)
			return -1;
		if (a[0] == NAN_ATTR_CSIA) {
			if (csia_seen || n < 3 || !(n & 1))
				return -1;
			csia_seen = true;
		}
		if (a[0] == NAN_ATTR_DCEA) {
			if (dcea_seen || n != 2)
				return -1;
			dcea_seen = true;
			dcea[3] = a[3]; /* local regulatory capability, if supplied */
		} else if (a[0] != NAN_ATTR_NIRA && a[0] != NAN_ATTR_NPBA) {
			memcpy(attrs + used, a, n + 3);
			used += n + 3; /* subset of the already bounded event */
		}
		pos += n + 3;
	}
	if (!csia_seen || sizeof(dcea) + npba_len > sizeof(attrs) - used)
		return -1;
	memcpy(attrs + used, dcea, sizeof(dcea));
	used += sizeof(dcea);
	memcpy(attrs + used, npba, npba_len);
	used += npba_len;
	memcpy(e->attrs, attrs, used);
	e->attrs_len = used;
	e->pairing_type = PAIRING_SETUP;
	return 1;
}

static bool auth1_is_retry(const struct bridge *b,
			   const struct pasn_start_event *e)
{
	struct timespec now;
	if (!b->pasn || !b->auth2_len || b->auth1_len != e->frame_len ||
	    e->frame_len < 30 || e->frame_len > AUTH_MAX ||
	    b->pairing_type != e->pairing_type ||
	    memcmp(b->auth1_frame + 4, e->frame + 4, 18) ||
	    memcmp(b->auth1_frame + 24, e->frame + 24, e->frame_len - 24) ||
	    clock_gettime(CLOCK_MONOTONIC, &now))
		return false;
	/* Ignore only MAC retry/sequence/duration changes. A changed SAE commit,
	 * PASN public key, bootstrap token or peer always starts a new exchange. */
	return now.tv_sec >= b->auth1_started.tv_sec &&
	       now.tv_sec - b->auth1_started.tv_sec < 10;
}

static int on_start(struct bridge *b, const char *line)
{
	struct pasn_start_event e;
	const struct ieee80211_mgmt *auth;
	char password[PASS_MAX + 1] = {0};
	u8 extra[2 + 4 + ATTR_MAX];
	u8 rsnxe[4];
	int ret = -1;

	if (hex_line(line, (u8 *) &e, sizeof(e)))
		return -1;
	if (e.deinit || e.role != 2 || e.auth_method != 1 ||
	    !e.pass_len || e.pass_len > PASS_MAX ||
	    e.frame_len < offsetof(struct ieee80211_mgmt, u.auth.variable) ||
	    e.frame_len > AUTH_MAX || e.attrs_len > ATTR_MAX)
		goto done;
	auth = (const struct ieee80211_mgmt *) e.frame;
	if (memcmp(auth->sa, e.peer, 6) ||
	    memcmp(auth->bssid, e.bssid, 6) ||
	    le_to_host16(auth->u.auth.auth_alg) != WLAN_AUTH_PASN ||
	    le_to_host16(auth->u.auth.auth_transaction) != 1)
		goto done;
	ret = correct_setup_event(&e);
	if (ret < 0) goto done;
	if (ret > 0)
		pasn_log(b,1, "Corrected stale verification event and Auth2 attributes to SAE setup\n");
	if (auth1_is_retry(b, &e)) {
		pasn_log(b,0,"Auth1 retransmission: retaining PASN handshake%s\n",
		       b->keys_installed ? " (already completed)" : ", resending Auth2");
		ret = b->keys_installed ? 0 :
			send_mgmt(b, b->auth2_frame, b->auth2_len, 0, b->freq, 0);
		goto done;
	}
	reset_session(b);
	ret = -1;
	if (clock_gettime(CLOCK_MONOTONIC, &b->auth1_started))
		goto done;
	b->auth1_len = e.frame_len;
	memcpy(b->auth1_frame, e.frame, e.frame_len);
	memcpy(b->own, auth->da, 6);
	memcpy(b->peer, e.peer, 6);
	memcpy(b->bssid, e.bssid, 6);
	b->pairing_type = e.pairing_type;
	memcpy(password, e.pass, e.pass_len);
	b->pasn = pasn_data_init();
	if (!b->pasn) goto done;
	pasn_set_responder_pmksa(b->pasn, b->pmksa);
	pasn_set_own_addr(b->pasn, b->own);
	pasn_set_peer_addr(b->pasn, b->peer);
	pasn_set_bssid(b->pasn, b->bssid);
	b->pasn->group = 19;
	b->pasn->pasn_groups = calloc(2, sizeof(int));
	if (!b->pasn->pasn_groups) goto done;
	b->pasn->pasn_groups[0] = 19;
	pasn_set_cipher(b->pasn, WPA_CIPHER_CCMP);
	pasn_set_akmp(b->pasn, WPA_KEY_MGMT_SAE);
	pasn_set_rsn_pairwise(b->pasn, WPA_CIPHER_CCMP);
	pasn_set_wpa_key_mgmt(b->pasn, WPA_KEY_MGMT_SAE);
	pasn_enable_kdk_derivation(b->pasn);
	if (e.pairing_type == PAIRING_SETUP) {
		b->pt = sae_derive_pt(b->pasn->pasn_groups,
				      (const u8 *) NAN_PASN_SSID,
				      strlen(NAN_PASN_SSID),
				      (const u8 *) password, e.pass_len,
				      NULL, 0);
		if (!b->pt || pasn_set_pt(b->pasn, b->pt)) goto done;
	} else if (e.pairing_type == PAIRING_VERIFICATION) {
		if (prepare_verification(b, e.frame, e.frame_len,
					 e.own_pmkid)) {
			pasn_log(b,1, "Verification NIRA/NPK unavailable\n");
			goto done;
		}
	} else {
		goto done;
	}
	rsnxe[0] = WLAN_EID_RSNX;
	rsnxe[1] = 2;
	rsnxe[2] = 1 | (1 << WLAN_RSNX_CAPAB_SAE_H2E);
	rsnxe[3] = 0;
	pasn_set_rsnxe_ie(b->pasn, rsnxe);
	extra[0] = WLAN_EID_VENDOR_SPECIFIC;
	extra[1] = 4 + e.attrs_len;
	extra[2] = 0x50; extra[3] = 0x6f;
	extra[4] = 0x9a; extra[5] = 0x13;
	memcpy(extra + 6, e.attrs, e.attrs_len);
	if (pasn_set_extra_ies(b->pasn, extra, 6 + e.attrs_len)) goto done;
	pasn_register_callbacks(b->pasn, b, send_mgmt, validate_custom_pmkid,
				NULL, NULL);
	ret = handle_auth_pasn_1(b->pasn, b->own, b->peer, auth,
				 e.frame_len, false);
	if (!b->auth2_sent) ret = -1;
done:
	memset(password, 0, sizeof(password));
	memset(&e, 0, sizeof(e));
	if (ret) {
		pasn_log(b,1, "Auth1 processing failed\n");
		reset_session(b);
	}
	return ret;
}

static int on_rx(struct bridge *b, const char *line)
{
	struct pasn_rx_event e;
	const struct ieee80211_mgmt *auth;
	int ret;

	if (!b->pasn || !b->auth2_sent ||
	    hex_line(line, (u8 *) &e, sizeof(e)))
		return -1;
	if (e.seq != 3 || e.frame_len < 30 || e.frame_len > AUTH_MAX)
		return -1;
	auth = (const struct ieee80211_mgmt *) e.frame;
	if (memcmp(auth->da, b->own, 6) || memcmp(auth->sa, b->peer, 6) ||
	    memcmp(auth->bssid, b->bssid, 6) ||
	    le_to_host16(auth->u.auth.auth_alg) != WLAN_AUTH_PASN ||
	    le_to_host16(auth->u.auth.auth_transaction) != 3)
		return -1;
	if (b->keys_installed) {
		pasn_log(b,0,"Ignoring Auth3 after keys already installed\n");
		memset(&e, 0, sizeof(e));
		return 0;
	}
	ret = handle_auth_pasn_3(b->pasn, b->own, b->peer, auth, e.frame_len);
	pasn_log(b,0,"Auth3 verification %s: peer=" MACSTR ", TK=%zu, KDK=%zu\n",
	       ret ? "FAILED" : "OK", MAC2STR(b->peer),
	       b->pasn->ptk.tk_len, b->pasn->ptk.kdk_len);

	if (!ret)
		ret = install_keys(b);
	if (!ret)
		b->keys_installed = true;
	if (!ret && b->pairing_type == PAIRING_SETUP) {
		if (store_npk(b))
			pasn_log(b,1, "Could not retain NPK for pairing verification\n");
		else
			pasn_log(b,0,"NPK retained for pairing verification\n");

	}
	if(!ret&&b->notify_keys_installed)
		b->notify_keys_installed(b->context,b->pairing_type,b->peer);
	memset(&e, 0, sizeof(e));
	return ret;
}


struct green2_pasn { struct bridge bridge; };

static int cache_identity_selftest(void);
int green2_pasn_crypto_selftest(void)
{
    const int groups[] = {19, 0};
    if (os_program_init()) return -1;
    struct sae_pt *point=sae_derive_pt(groups,(const u8 *)NAN_PASN_SSID,
        strlen(NAN_PASN_SSID),(const u8 *)"123456",6,NULL,0);
    if (!point) { os_program_deinit(); return -1; }
    sae_deinit_pt(point); os_program_deinit(); return cache_identity_selftest();
}

static char *encode_vendor_event(const uint8_t *data, size_t size)
{
    char *line = malloc(32 + size * 3);
    if (!line) return NULL;
    size_t at = snprintf(line, 32, "hexdump(len=%zu):", size);
    for (size_t i=0;i<size;i++) at += snprintf(line+at, 4, " %02x", data[i]);
    return line;
}

struct green2_pasn *green2_pasn_create_config(const struct green2_pasn_config *config)
{
    if(!config||!config->interface_name||config->frequency<2400||config->frequency>7125)return NULL;
    struct green2_pasn *engine=calloc(1,sizeof(*engine));if(!engine)return NULL;
    struct bridge *b=&engine->bridge;
    b->ifindex=if_nametoindex(config->interface_name);b->freq=config->frequency;
    if(!b->ifindex){free(engine);return NULL;}
    b->context=config->context;b->read_store=config->read_store;b->write_store=config->write_store;
    b->log=config->log;b->notify_keys_installed=config->keys_installed;
    if(config->nik_cache_path)b->nik_cache_path=strdup(config->nik_cache_path);
    if(config->npk_path)b->npk_path=strdup(config->npk_path);
    if(config->result_path)b->result_path=strdup(config->result_path);
    if((config->nik_cache_path&&!b->nik_cache_path)||(config->npk_path&&!b->npk_path)||
       (config->result_path&&!b->result_path))goto fail_paths;
    if(os_program_init())goto fail_paths;
    if(eloop_init()){os_program_deinit();goto fail_paths;}
    wpa_debug_level=MSG_WARNING;b->pmksa=pasn_responder_pmksa_cache_init();b->nl=nl_socket_alloc();
    if(!b->pmksa||!b->nl||genl_connect(b->nl)<0||(b->family=genl_ctrl_resolve(b->nl,"nl80211"))<0){
        green2_pasn_destroy(engine);return NULL;
    }
    return engine;
fail_paths:
    free(b->nik_cache_path);free(b->npk_path);free(b->result_path);free(engine);return NULL;
}
void green2_pasn_destroy(struct green2_pasn *engine)
{
    if (!engine) return;
    reset_session(&engine->bridge);
    if (engine->bridge.pmksa) pasn_responder_pmksa_cache_deinit(engine->bridge.pmksa);
    if (engine->bridge.nl) nl_socket_free(engine->bridge.nl);
    eloop_destroy(); os_program_deinit();
    free(engine->bridge.nik_cache_path);free(engine->bridge.npk_path);free(engine->bridge.result_path);
    forced_memzero(engine, sizeof(*engine)); free(engine);
}
int green2_pasn_start(struct green2_pasn *engine,const uint8_t *data,size_t size,
                     const char *password,size_t password_length)
{
    if (!engine || size!=PASN_START_LEN || password_length>PASS_MAX) return -1;
    struct pasn_start_event event;
    memcpy(&event,data,sizeof(event));
    if (password && password_length) {
        memset(event.pass,0,sizeof(event.pass));
        memcpy(event.pass,password,password_length);event.pass_len=password_length;
    }
    // Verification uses the authenticated NPK. The legacy parser still requires
    // a nonempty password field before selecting its verification branch.
    if(event.pairing_type==PAIRING_VERIFICATION&&!event.pass_len){event.pass_len=1;event.pass[0]=0;}
    char *line=encode_vendor_event((const uint8_t *)&event,sizeof(event));
    forced_memzero(&event,sizeof(event));
    if (!line) return -1;
    int result=on_start(&engine->bridge,line);
    forced_memzero(line,strlen(line));free(line);return result;
}
int green2_pasn_receive(struct green2_pasn *engine,const uint8_t *event,size_t length)
{
    if (!engine || length!=PASN_RX_LEN) return -1;
    char *line=encode_vendor_event(event,length);if(!line)return -1;
    int result=on_rx(&engine->bridge,line);
    forced_memzero(line,strlen(line));free(line);return result;
}
int green2_pasn_get_keys(struct green2_pasn *engine,uint8_t peer[6],uint8_t npk[32],uint8_t *type)
{
    if (!engine || !engine->bridge.keys_installed ||
        pasn_get_pmk_len(engine->bridge.pasn)!=32) return -1;
    memcpy(peer,engine->bridge.peer,6);
    memcpy(npk,pasn_get_pmk(engine->bridge.pasn),32);
    *type=engine->bridge.pairing_type;return 0;
}
static int frame_nira(const uint8_t *frame,size_t length,uint8_t nonce[8],uint8_t tag[8])
{
    size_t pos=30;
    while(pos+2<=length){size_t n=frame[pos+1];if(n>length-pos-2)return -1;
        const uint8_t *value=frame+pos+2;
        if(frame[pos]==221&&n>=4&&!memcmp(value,"\x50\x6f\x9a\x13",4)){
            size_t at=4;
            while(at+3<=n){uint8_t id=value[at];size_t size=value[at+1]|((size_t)value[at+2]<<8);at+=3;if(size>n-at)return -1;
                if(id==NAN_ATTR_NIRA&&size==17&&value[at]==NAN_NIRA_CIPHER_VER_128){memcpy(nonce,value+at+1,8);memcpy(tag,value+at+9,8);return 0;}at+=size;}
        }pos+=2+n;
    }return -1;
}
int green2_pasn_event_nira(const uint8_t *event,size_t length,uint8_t nonce[8],uint8_t tag[8])
{
    if(length!=PASN_START_LEN)return -1;struct pasn_start_event e;memcpy(&e,event,sizeof(e));
    int result=e.frame_len<=AUTH_MAX?frame_nira(e.frame,e.frame_len,nonce,tag):-1;
    forced_memzero(&e,sizeof(e));return result;
}
int green2_pasn_identity(struct green2_pasn *engine,const uint8_t *bytes,size_t length,uint8_t local[16],uint8_t peer[16])
{
    if(!engine||length!=sizeof(struct nik_cache_file)||bytes[0]!=1)return -1;
    struct nik_cache_file cache;memcpy(&cache,bytes,sizeof(cache));
    uint8_t nonce[8]={0},tag[8]={0},digest[32],message[17];int selected=-1,count=0;
    int verification=engine->bridge.pairing_type==PAIRING_VERIFICATION;
    int has_nira=!frame_nira(engine->bridge.auth1_frame,engine->bridge.auth1_len,nonce,tag);
    if(verification&&!has_nira){forced_memzero(&cache,sizeof(cache));return -1;}
    memcpy(message,"NIR",3);memcpy(message+3,engine->bridge.peer,6);memcpy(message+9,nonce,8);
    for(int i=0;i<8;i++){
        // Firmware uses lifetime=0 for populated, persistent cache entries.
        // Empty slots are identified by role and identity, not by lifetime.
        int present=0;for(size_t j=0;j<16;j++)present|=cache.entries[i].nik[j];
        if(!present||(cache.entries[i].role!=1&&cache.entries[i].role!=2))continue;
        if(verification&&(hmac_sha256(cache.entries[i].nik,16,message,sizeof(message),digest)||os_memcmp_const(digest,tag,8)))continue;
        selected=i;count++;
    }
    if(count==1){memcpy(local,cache.own_nik,16);memcpy(peer,cache.entries[selected].nik,16);}
    forced_memzero(&cache,sizeof(cache));forced_memzero(digest,sizeof(digest));
    return count==1?0:-1;
}
// Select only entries changed by this setup, never a preloaded peer identity.
int green2_pasn_setup_identity(struct green2_pasn *engine,const uint8_t *bytes,size_t length,
                              const uint8_t *previous,size_t previous_length,
                              uint8_t local[16],uint8_t peer[16])
{
    if(!engine||engine->bridge.pairing_type!=PAIRING_SETUP||!bytes||!previous||
       length!=sizeof(struct nik_cache_file)||previous_length!=length||bytes[0]!=1||previous[0]!=1||
       memcmp(bytes+4,previous+4,16))return -1;
    struct nik_cache_file current,before;memcpy(&current,bytes,length);memcpy(&before,previous,length);
    for(size_t i=0;i<8;i++)if(!memcmp(&current.entries[i],&before.entries[i],sizeof(current.entries[i])))
        memset(&current.entries[i],0,sizeof(current.entries[i]));
    int result=green2_pasn_identity(engine,(const uint8_t*)&current,sizeof(current),local,peer);
    forced_memzero(&current,sizeof(current));forced_memzero(&before,sizeof(before));return result;
}
// Classify the authenticated protocol on the wire before asking Android for
// setup credentials. Firmware can retain a stale verification event type.
int green2_pasn_prepare_event(uint8_t *bytes,size_t length)
{
    if(!bytes||length!=sizeof(struct pasn_start_event))return -1;
    struct pasn_start_event event;memcpy(&event,bytes,length);
    int result=correct_setup_event(&event);
    if(result>=0)memcpy(bytes,&event,length);
    forced_memzero(&event,sizeof(event));return result;
}
int green2_pasn_verification_key(struct green2_pasn *engine,const uint8_t npk[32])
{
    if(!engine||!npk)return -1;uint8_t record[36];memcpy(record,"NPK1",4);memcpy(record+4,npk,32);
    int result=write_store(&engine->bridge,GREEN2_PASN_NPK,record,sizeof(record));
    forced_memzero(record,sizeof(record));return result;
}

static int cache_identity_selftest(void)
{
    struct green2_pasn engine={0};struct nik_cache_file cache={0};uint8_t local[16],peer[16];
    cache.version=1;memset(cache.own_nik,0x21,16);engine.bridge.pairing_type=PAIRING_SETUP;
    if(!green2_pasn_identity(&engine,(const uint8_t*)&cache,sizeof(cache),local,peer))return -1;
    cache.entries[0].role=2;cache.entries[0].auth=1;cache.entries[0].peer_inst=1;
    memset(cache.entries[0].nik,0x31,16);memset(cache.entries[0].pmkid,0x41,16);
    if(green2_pasn_identity(&engine,(const uint8_t*)&cache,sizeof(cache),local,peer)||memcmp(peer,cache.entries[0].nik,16)||memcmp(local,cache.own_nik,16))return -1;
    cache.entries[1]=cache.entries[0];cache.entries[1].nik[0]^=1;
    if(!green2_pasn_identity(&engine,(const uint8_t*)&cache,sizeof(cache),local,peer))return -1;
    struct nik_cache_file previous=cache;
    memset(&previous.entries[1],0,sizeof(previous.entries[1]));
    if(green2_pasn_setup_identity(&engine,(const uint8_t*)&cache,sizeof(cache),(const uint8_t*)&previous,sizeof(previous),local,peer)||memcmp(peer,cache.entries[1].nik,16))return -1;
    if(!green2_pasn_setup_identity(&engine,(const uint8_t*)&cache,sizeof(cache),(const uint8_t*)&cache,sizeof(cache),local,peer))return -1;
    previous.own_nik[0]^=1;
    if(!green2_pasn_setup_identity(&engine,(const uint8_t*)&cache,sizeof(cache),(const uint8_t*)&previous,sizeof(previous),local,peer))return -1;
    forced_memzero(&previous,sizeof(previous));
    memset(&cache.entries[1],0,sizeof(cache.entries[1]));engine.bridge.pairing_type=PAIRING_VERIFICATION;
    if(!green2_pasn_identity(&engine,(const uint8_t*)&cache,sizeof(cache),local,peer))return -1;
    uint8_t *frame=engine.bridge.auth1_frame;frame[30]=221;frame[31]=24;memcpy(frame+32,"\x50\x6f\x9a\x13",4);
    frame[36]=NAN_ATTR_NIRA;frame[37]=17;frame[39]=NAN_NIRA_CIPHER_VER_128;memset(frame+40,0x44,8);engine.bridge.auth1_len=56;
    memset(engine.bridge.peer,0x43,6);uint8_t message[17],digest[32];memcpy(message,"NIR",3);memcpy(message+3,engine.bridge.peer,6);memcpy(message+9,frame+40,8);
    if(hmac_sha256(cache.entries[0].nik,16,message,sizeof(message),digest))return -1;
    memcpy(frame+48,digest,8);
    if(green2_pasn_identity(&engine,(const uint8_t*)&cache,sizeof(cache),local,peer))return -1;
    frame[48]^=1;
    if(!green2_pasn_identity(&engine,(const uint8_t*)&cache,sizeof(cache),local,peer))return -1;
    forced_memzero(&engine,sizeof(engine));forced_memzero(&cache,sizeof(cache));forced_memzero(digest,sizeof(digest));return 0;
}
