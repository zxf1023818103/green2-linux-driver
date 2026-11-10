/* Shared Green2 Linux/Android PASN API. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct green2_pasn;
int green2_pasn_crypto_selftest(void);
enum green2_pasn_store { GREEN2_PASN_NIK_CACHE, GREEN2_PASN_NPK };
/* Callbacks return 0 only for a complete read/write. NULL uses atomic files.
 * Paths are copied at create time; callback context must outlive the engine.
 * One engine per process: the pinned hostap eloop/crypto state is global.
 * Serialize all calls. No callback receives plaintext handshake keys in logs.
 */
struct green2_pasn_config {
    const char *interface_name;
    unsigned frequency;
    const char *nik_cache_path;
    const char *npk_path;
    const char *result_path; /* Optional legacy 7-byte completion file. */
    void *context;
    int (*read_store)(void *,enum green2_pasn_store,void *,size_t);
    int (*write_store)(void *,enum green2_pasn_store,const void *,size_t);
    void (*log)(void *,int level,const char *message);
    void (*keys_installed)(void *,uint8_t pairing_type,const uint8_t peer[6]);
};
struct green2_pasn *green2_pasn_create_config(const struct green2_pasn_config *config);
void green2_pasn_destroy(struct green2_pasn *engine);
int green2_pasn_start(struct green2_pasn *engine, const uint8_t *event, size_t length,
                     const char *password, size_t password_length);
int green2_pasn_receive(struct green2_pasn *engine, const uint8_t *event, size_t length);
int green2_pasn_get_keys(struct green2_pasn *engine, uint8_t peer[6], uint8_t npk[32],
                        uint8_t *request_type);
int green2_pasn_event_nira(const uint8_t *event,size_t length,uint8_t nonce[8],uint8_t tag[8]);
int green2_pasn_identity(struct green2_pasn *engine,const uint8_t *cache,size_t length,
                        uint8_t local_nik[16],uint8_t peer_nik[16]);
int green2_pasn_setup_identity(struct green2_pasn *engine,const uint8_t *cache,size_t length,
                              const uint8_t *previous,size_t previous_length,
                              uint8_t local_nik[16],uint8_t peer_nik[16]);
int green2_pasn_prepare_event(uint8_t *event,size_t length);
int green2_pasn_verification_key(struct green2_pasn *engine,const uint8_t npk[32]);
#ifdef __cplusplus
}
#endif
