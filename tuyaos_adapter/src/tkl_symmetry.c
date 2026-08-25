/**
 * @file tkl_symmetry.c
 * @brief SiWx917 AES adapter: software ECB/CBC, hardware AES-GCM
 * @version 1.0
 * @date 2026-08-10
 * @copyright Copyright (c) Tuya Inc.
 */
#include "tkl_symmetry.h"
#include "tkl_memory.h"
#include "sl_tuya_log.h"

#include "mbedtls/aes.h"
#include "sl_si91x_crypto.h"
#include "sl_si91x_gcm.h"
#include "sl_si91x_wrap.h"
#include "sl_si91x_crypto_utility.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */

/* The GCM engine reads/writes ciphertext and tag as one contiguous block, so it
 * needs a scratch buffer. It is allocated per call rather than shared so that
 * concurrent callers cannot corrupt each other, and so the size is not capped.
 * PSRAM is preferred to keep large transient buffers out of internal SRAM.
 *
 * These entry points are not on the TLS path: with ENABLE_PLATFORM_AES the only
 * callers of tal_aes_gcm_encode/decode are the AI image and video paths in
 * src/tuya_ai_service/. Note the NWP caps a crypto request at
 * SL_SI91X_MAX_DATA_SIZE_IN_BYTES (1400) and the header warns the M4 receive
 * buffer has the same limit, so ciphertext||tag above that is rejected by the
 * hardware rather than split here. */
#if defined(CONFIG_SPIRAM)
#define GCM_SCRATCH_MALLOC(size) tkl_system_psram_malloc(size)
#define GCM_SCRATCH_FREE(ptr)    tkl_system_psram_free(ptr)
#else
#define GCM_SCRATCH_MALLOC(size) tkl_system_malloc(size)
#define GCM_SCRATCH_FREE(ptr)    tkl_system_free(ptr)
#endif

#define GCM_NONCE_LEN_REQUIRED 16
#define GCM_TAG_LEN_MAX        16

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Create and initialize AES context
 * @param[out] ctx aes handle
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_aes_create_init(TKL_SYMMETRY_HANDLE *ctx)
{
    mbedtls_aes_context *mbedtls_aes_ctx;

    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }

    mbedtls_aes_ctx = (mbedtls_aes_context *)tkl_system_malloc(sizeof(mbedtls_aes_context));
    if (mbedtls_aes_ctx == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    mbedtls_aes_init(mbedtls_aes_ctx);
    *ctx = mbedtls_aes_ctx;
    return OPRT_OK;
}

/**
 * @brief Free AES context
 * @param[in] ctx aes handle
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_aes_free(TKL_SYMMETRY_HANDLE ctx)
{
    if (ctx != NULL) {
        mbedtls_aes_free((mbedtls_aes_context *)ctx);
        tkl_system_free(ctx);
    }
    return OPRT_OK;
}

/**
 * @brief Set AES encryption key
 * @param[in] ctx aes handle
 * @param[in] key key bytes
 * @param[in] keybits key size in bits
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_aes_setkey_enc(TKL_SYMMETRY_HANDLE ctx, const uint8_t *key, uint32_t keybits)
{
    if (ctx == NULL || key == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (mbedtls_aes_setkey_enc((mbedtls_aes_context *)ctx, key, keybits) != 0) {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief Set AES decryption key
 * @param[in] ctx aes handle
 * @param[in] key key bytes
 * @param[in] keybits key size in bits
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_aes_setkey_dec(TKL_SYMMETRY_HANDLE ctx, const uint8_t *key, uint32_t keybits)
{
    if (ctx == NULL || key == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (mbedtls_aes_setkey_dec((mbedtls_aes_context *)ctx, key, keybits) != 0) {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief AES-ECB crypt
 * @param[in] ctx aes handle
 * @param[in] mode encrypt/decrypt
 * @param[in] length input length
 * @param[in] input input buffer
 * @param[out] output output buffer
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_aes_crypt_ecb(TKL_SYMMETRY_HANDLE ctx, int32_t mode, size_t length, const uint8_t *input,
                              uint8_t *output)
{
    uint32_t i;

    if (ctx == NULL || input == NULL || output == NULL || (length % 16) != 0) {
        return OPRT_INVALID_PARM;
    }

    for (i = 0; i < length; i += 16) {
        if (mbedtls_aes_crypt_ecb((mbedtls_aes_context *)ctx, mode, input + i, output + i) != 0) {
            return OPRT_COM_ERROR;
        }
    }
    return OPRT_OK;
}

/**
 * @brief AES-CBC crypt
 * @param[in] ctx aes handle
 * @param[in] mode encrypt/decrypt
 * @param[in] length input length
 * @param[in,out] iv IV
 * @param[in] input input buffer
 * @param[out] output output buffer
 * @return OPRT_OK on success
 */
OPERATE_RET tkl_aes_crypt_cbc(TKL_SYMMETRY_HANDLE ctx, int32_t mode, size_t length, uint8_t iv[16],
                              const uint8_t *input, uint8_t *output)
{
    if (ctx == NULL || input == NULL || output == NULL || iv == NULL || (length % 16) != 0) {
        return OPRT_INVALID_PARM;
    }
    if (mbedtls_aes_crypt_cbc((mbedtls_aes_context *)ctx, mode, length, iv, input, output) != 0) {
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief Map key length to Si91x GCM key size
 * @param[in] key_len key length in bytes
 * @param[out] key_size hardware key size
 * @return OPRT_OK on success
 */
static OPERATE_RET __si91x_gcm_key_size(uint32_t key_len, sl_si91x_gcm_key_size_t *key_size)
{
    if (key_size == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (key_len == 16) {
        *key_size = SL_SI91X_GCM_KEY_SIZE_128;
        return OPRT_OK;
    }
    if (key_len == 32) {
        *key_size = SL_SI91X_GCM_KEY_SIZE_256;
        return OPRT_OK;
    }
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief AES-GCM encrypt via Si91x hardware
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED if HW path cannot handle params
 */
OPERATE_RET tkl_aes_gcm_encode(const uint8_t *key, uint32_t key_len, const uint8_t *nonce, uint32_t nonce_len,
                               const uint8_t *ad, uint32_t ad_len, const uint8_t *input, uint32_t input_len,
                               uint8_t *output, uint32_t *output_len, uint8_t *tag, uint32_t tag_len)
{
    sl_si91x_gcm_key_size_t key_size;
    sl_si91x_gcm_config_t config_gcm;
    sl_status_t status;
    uint8_t *scratch;

    if (key == NULL || input == NULL || output == NULL || output_len == NULL || tag == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (nonce == NULL || nonce_len != GCM_NONCE_LEN_REQUIRED || input_len == 0 ||
        input_len >= SL_SI91X_MAX_DATA_SIZE_IN_BYTES || tag_len == 0 || tag_len > GCM_TAG_LEN_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (__si91x_gcm_key_size(key_len, &key_size) != OPRT_OK) {
        return OPRT_NOT_SUPPORTED;
    }

    /* Engine writes ciphertext||tag contiguously; the caller keeps them apart. */
    scratch = (uint8_t *)GCM_SCRATCH_MALLOC(input_len + tag_len);
    if (scratch == NULL) {
        /* There is no software fallback to reach: ENABLE_PLATFORM_AES compiles
           the mbedtls tkl_aes_gcm_* out entirely, and tal_aes_gcm_* forwards
           straight here. The caller gets the error. */
        return OPRT_NOT_SUPPORTED;
    }

    memset(&config_gcm, 0, sizeof(config_gcm));
    config_gcm.encrypt_decrypt = SL_SI91X_GCM_ENCRYPT;
    config_gcm.dma_use = SL_SI91X_GCM_DMA_ENABLE;
    config_gcm.msg = (uint8_t *)input;
    config_gcm.msg_length = input_len;
    config_gcm.nonce = (uint8_t *)nonce;
    config_gcm.nonce_length = nonce_len;
    config_gcm.ad = (uint8_t *)ad;
    config_gcm.ad_length = ad_len;
    config_gcm.gcm_mode = SL_SI91X_GCM_MODE;
    config_gcm.key_config.b0.key_size = key_size;
    config_gcm.key_config.b0.key_type = SL_SI91X_TRANSPARENT_KEY;
    config_gcm.key_config.b0.key_slot = 0;
    memcpy(config_gcm.key_config.b0.key_buffer, key, key_len);

    status = sl_si91x_gcm(&config_gcm, scratch);
    if (status != SL_STATUS_OK) {
        GCM_SCRATCH_FREE(scratch);
        TKL_LOGE("Hardware GCM encryption length %u tag %u failed 0x%lX", (unsigned)input_len, (unsigned)tag_len,
                 (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    memcpy(output, scratch, input_len);
    memcpy(tag, scratch + input_len, tag_len);
    GCM_SCRATCH_FREE(scratch);

    *output_len = input_len;
    return OPRT_OK;
}

/**
 * @brief AES-GCM decrypt via Si91x hardware
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED if HW path cannot handle params
 */
OPERATE_RET tkl_aes_gcm_decode(const uint8_t *key, uint32_t key_len, const uint8_t *nonce, uint32_t nonce_len,
                               const uint8_t *ad, uint32_t ad_len, const uint8_t *input, uint32_t input_len,
                               uint8_t *output, uint32_t *output_len, uint8_t *tag, uint32_t tag_len)
{
    sl_si91x_gcm_key_size_t key_size;
    sl_si91x_gcm_config_t config_gcm;
    sl_status_t status;
    uint8_t *scratch;

    if (key == NULL || input == NULL || output == NULL || output_len == NULL || tag == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (nonce == NULL || nonce_len != GCM_NONCE_LEN_REQUIRED || input_len == 0 ||
        input_len >= SL_SI91X_MAX_DATA_SIZE_IN_BYTES || tag_len == 0 || tag_len > GCM_TAG_LEN_MAX) {
        return OPRT_NOT_SUPPORTED;
    }
    if (__si91x_gcm_key_size(key_len, &key_size) != OPRT_OK) {
        return OPRT_NOT_SUPPORTED;
    }

    /* Engine expects ciphertext||tag as one message; the caller keeps them apart. */
    scratch = (uint8_t *)GCM_SCRATCH_MALLOC(input_len + tag_len);
    if (scratch == NULL) {
        /* There is no software fallback to reach: ENABLE_PLATFORM_AES compiles
           the mbedtls tkl_aes_gcm_* out entirely, and tal_aes_gcm_* forwards
           straight here. The caller gets the error. */
        return OPRT_NOT_SUPPORTED;
    }

    memset(&config_gcm, 0, sizeof(config_gcm));
    config_gcm.encrypt_decrypt = SL_SI91X_GCM_DECRYPT;
    config_gcm.dma_use = SL_SI91X_GCM_DMA_ENABLE;
    config_gcm.gcm_mode = SL_SI91X_GCM_MODE;
    config_gcm.nonce = (uint8_t *)nonce;
    config_gcm.nonce_length = nonce_len;
    config_gcm.ad = (uint8_t *)ad;
    config_gcm.ad_length = ad_len;
    config_gcm.key_config.b0.key_size = key_size;
    config_gcm.key_config.b0.key_type = SL_SI91X_TRANSPARENT_KEY;
    config_gcm.key_config.b0.key_slot = 0;
    memcpy(config_gcm.key_config.b0.key_buffer, key, key_len);

    memcpy(scratch, input, input_len);
    memcpy(scratch + input_len, tag, tag_len);
    config_gcm.msg = scratch;
    config_gcm.msg_length = input_len + tag_len;

    status = sl_si91x_gcm(&config_gcm, output);
    GCM_SCRATCH_FREE(scratch);

    if (status != SL_STATUS_OK) {
        TKL_LOGE("Hardware GCM decryption length %u tag %u failed 0x%lX", (unsigned)input_len, (unsigned)tag_len,
                 (unsigned long)status);
        return OPRT_COM_ERROR;
    }

    *output_len = input_len;
    return OPRT_OK;
}
