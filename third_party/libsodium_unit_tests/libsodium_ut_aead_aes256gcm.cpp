#include "libsodium_ut_aead_aes256gcm.h"

#include "unity.h"
//#include "aether/tele.h"

namespace aead_aes256gcm
{
    #define DEBUG_OUT 1
    #define LOG(TAG, ...) {\
     fprintf(stderr, "file location%s\nfunction name:%s\nline:%d\n", __FILE__, __FUNCTION__, __LINE__);\
     fprintf(stderr, "date:%s\ntime:%s\n",__DATE__, __TIME__);\
     fprintf(stderr, "%s", TAG);\
     fprintf(stderr, "\r\n");\
     fprintf(stderr, __VA_ARGS__);\
     fprintf(stderr, "\n");\
    }

    static const char *TAG = "SODIUM UNITY";

    #if DEBUG_OUT==1
        #define DebugPrint(format, ...) LOG(TAG, format, ##__VA_ARGS__)
    #elif DEBUG_OUT==2
        #define DebugPrint(format, ...) AE_TELED_DEBUG(TAG, format, ##__VA_ARGS__)
    #elif DEBUG_OUT==3
        #define DebugPrint(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
    #endif

    static int tv(void)
    {
        unsigned char      *ad;
        unsigned char      *ciphertext;
        unsigned char      *ciphertext2;
        unsigned char      *decrypted;
        unsigned char      *detached_ciphertext;
        unsigned char      *expected_ciphertext;
        unsigned char      *key;
        unsigned char      *message;
        unsigned char      *mac;
        unsigned char      *nonce;
        char               *hex;
        unsigned long long  found_ciphertext_len;
        unsigned long long  found_mac_len;
        unsigned long long  found_message_len;
        size_t              ad_len;
        size_t              ciphertext_len;
        size_t              detached_ciphertext_len;
        size_t              i = 0U;
        size_t              message_len;
        int                 res;

        key = (unsigned char *) sodium_malloc(crypto_aead_aes256gcm_KEYBYTES);
        nonce = (unsigned char *) sodium_malloc(crypto_aead_aes256gcm_NPUBBYTES);
        mac = (unsigned char *) sodium_malloc(crypto_aead_aes256gcm_ABYTES);

        do {
            TEST_ASSERT(strlen(tests_aead_aes256gcm[i].key_hex) == 2 * crypto_aead_aes256gcm_KEYBYTES);
            sodium_hex2bin(key, crypto_aead_aes256gcm_KEYBYTES,
                           tests_aead_aes256gcm[i].key_hex, strlen(tests_aead_aes256gcm[i].key_hex),
                           NULL, NULL, NULL);
            TEST_ASSERT(strlen(tests_aead_aes256gcm[i].nonce_hex) == 2 * crypto_aead_aes256gcm_NPUBBYTES);
            sodium_hex2bin(nonce, crypto_aead_aes256gcm_NPUBBYTES,
                           tests_aead_aes256gcm[i].nonce_hex, strlen(tests_aead_aes256gcm[i].nonce_hex),
                           NULL, NULL, NULL);
            message_len = strlen(tests_aead_aes256gcm[i].message_hex) / 2;
            message = (unsigned char *) sodium_malloc(message_len);
            sodium_hex2bin(message, message_len,
                           tests_aead_aes256gcm[i].message_hex, strlen(tests_aead_aes256gcm[i].message_hex),
                           NULL, NULL, NULL);
            ad_len = strlen(tests_aead_aes256gcm[i].ad_hex) / 2;
            ad = (unsigned char *) sodium_malloc(ad_len);
            sodium_hex2bin(ad, ad_len,
                           tests_aead_aes256gcm[i].ad_hex, strlen(tests_aead_aes256gcm[i].ad_hex),
                           NULL, NULL, NULL);
            ciphertext_len = message_len + crypto_aead_aes256gcm_ABYTES;
            detached_ciphertext_len = message_len;
            expected_ciphertext = (unsigned char *) sodium_malloc(ciphertext_len);
            TEST_ASSERT(strlen(tests_aead_aes256gcm[i].ciphertext_hex) == 2 * message_len);
            sodium_hex2bin(expected_ciphertext, message_len,
                           tests_aead_aes256gcm[i].ciphertext_hex, strlen(tests_aead_aes256gcm[i].ciphertext_hex),
                           NULL, NULL, NULL);
            TEST_ASSERT(strlen(tests_aead_aes256gcm[i].mac_hex) == 2 * crypto_aead_aes256gcm_ABYTES);
            sodium_hex2bin(expected_ciphertext + message_len, crypto_aead_aes256gcm_ABYTES,
                           tests_aead_aes256gcm[i].mac_hex, strlen(tests_aead_aes256gcm[i].mac_hex),
                           NULL, NULL, NULL);
            ciphertext = (unsigned char *) sodium_malloc(ciphertext_len);
            detached_ciphertext = (unsigned char *) sodium_malloc(detached_ciphertext_len);

            res = crypto_aead_aes256gcm_encrypt_detached(detached_ciphertext, mac,
                                                         &found_mac_len,
                                                         message, message_len,
                                                         ad, ad_len, NULL, nonce, key);
            TEST_ASSERT(found_mac_len == crypto_aead_aes256gcm_ABYTES);
            if (memcmp(detached_ciphertext, expected_ciphertext,
                       detached_ciphertext_len) != 0 ||
                memcmp(mac, expected_ciphertext + message_len,
                       crypto_aead_aes256gcm_ABYTES) != 0) {
                DebugPrint("Detached encryption of test vector #%u failed (res=%d)", (unsigned int)i, res);
                hex = (char *) sodium_malloc((size_t) ciphertext_len * 2 + 1);
                sodium_bin2hex(hex, (size_t) detached_ciphertext_len * 2 + 1,
                               detached_ciphertext, detached_ciphertext_len);
                DebugPrint("Computed: [%s]", hex);
                sodium_bin2hex(hex, (size_t) detached_ciphertext_len * 2 + 1,
                               expected_ciphertext, detached_ciphertext_len);
                DebugPrint("Expected: [%s]", hex);
                sodium_bin2hex(hex, (size_t) found_mac_len * 2 + 1,
                               mac, found_mac_len);
                DebugPrint("Computed mac: [%s]", hex);
                sodium_bin2hex(hex, (size_t) found_mac_len * 2 + 1,
                               expected_ciphertext + message_len, found_mac_len);
                DebugPrint("Expected mac: [%s]", hex);
                sodium_free(hex);
            }

            res = crypto_aead_aes256gcm_encrypt(ciphertext, &found_ciphertext_len,
                                                message, message_len,
                                                ad, ad_len, NULL, nonce, key);

            TEST_ASSERT((size_t) found_ciphertext_len == ciphertext_len);
            if (memcmp(ciphertext, expected_ciphertext, ciphertext_len) != 0) {
                DebugPrint("Encryption of test vector #%u failed (res=%d)", (unsigned int)i, res);
                hex = (char *) sodium_malloc((size_t) found_ciphertext_len * 2 + 1);
                sodium_bin2hex(hex, (size_t) found_ciphertext_len * 2 + 1,
                               ciphertext, ciphertext_len);
                DebugPrint("Computed: [%s]", hex);
                sodium_bin2hex(hex, (size_t) ciphertext_len * 2 + 1,
                               expected_ciphertext, ciphertext_len);
                DebugPrint("Expected: [%s]", hex);
                sodium_free(hex);
            }

            decrypted = (unsigned char *) sodium_malloc(message_len);
            found_message_len = 1;
            if (crypto_aead_aes256gcm_decrypt(decrypted, &found_message_len,
                                              NULL, ciphertext,
                                              randombytes_uniform((uint32_t) ciphertext_len),
                                              ad, ad_len, nonce, key) != -1) {
                DebugPrint("Verification of test vector #%u after truncation succeeded", (unsigned int)i);
            }
            if (found_message_len != 0) {
                DebugPrint("Message length should have been set to zero after a failure");
            }
            if (crypto_aead_aes256gcm_decrypt(decrypted, &found_message_len,
                                              NULL, guard_page,
                                              randombytes_uniform(crypto_aead_aes256gcm_ABYTES),
                                              ad, ad_len, nonce, key) != -1) {
                DebugPrint("Verification of test vector #%u with a truncated tag failed", (unsigned int) i);
            }
            if (i == 0 && crypto_aead_aes256gcm_decrypt(NULL, NULL,
                                                        NULL, ciphertext, ciphertext_len,
                                                        ad, ad_len, nonce, key) != 0) {
                DebugPrint("Verification of test vector #%u's tag failed", (unsigned int) i);
            }
            if (crypto_aead_aes256gcm_decrypt(decrypted, &found_message_len,
                                              NULL, ciphertext, ciphertext_len,
                                              ad, ad_len, nonce, key) != 0) {
                DebugPrint("Verification of test vector #%u failed", (unsigned int) i);
            }
            TEST_ASSERT((size_t) found_message_len == message_len);
            if (memcmp(decrypted, message, message_len) != 0) {
                DebugPrint("Incorrect decryption of test vector #%u", (unsigned int) i);
            }
            memset(decrypted, 0xd0, message_len);
            if (crypto_aead_aes256gcm_decrypt_detached(decrypted,
                                                       NULL, detached_ciphertext,
                                                       detached_ciphertext_len,
                                                       mac, ad, ad_len, nonce, key) != 0) {
                DebugPrint("Detached verification of test vector #%u failed", (unsigned int) i);
            }
            if (memcmp(decrypted, message, message_len) != 0) {
                DebugPrint("Incorrect decryption of test vector #%u", (unsigned int) i);
            }

            ciphertext2 = (unsigned char *) sodium_malloc(ciphertext_len);
            crypto_aead_aes256gcm_encrypt(ciphertext, &found_ciphertext_len, message,
                                          message_len, ad, ad_len, NULL, nonce, key);
            TEST_ASSERT(found_ciphertext_len == ciphertext_len);
            memcpy(ciphertext2, message, message_len);
            crypto_aead_aes256gcm_encrypt(ciphertext2, &found_ciphertext_len,
                                          ciphertext2, message_len, ad, ad_len, NULL,
                                          nonce, key);
            TEST_ASSERT(found_ciphertext_len == ciphertext_len);
            TEST_ASSERT(memcmp(ciphertext, ciphertext2, ciphertext_len) == 0);
            if (crypto_aead_aes256gcm_decrypt(ciphertext2, &found_message_len, NULL,
                                              ciphertext2, ciphertext_len, ad, ad_len,
                                              nonce, key) != 0) {
                DebugPrint("In-place decryption of vector #%u failed", (unsigned int) i);
            }
            TEST_ASSERT(found_message_len == message_len);
            TEST_ASSERT(memcmp(ciphertext2, message, message_len) == 0);
            if (crypto_aead_aes256gcm_decrypt(message, &found_message_len, NULL,
                                              ciphertext, ciphertext_len, ad, ad_len,
                                              nonce, key) != 0) {
                DebugPrint("Decryption of vector #%u failed", (unsigned int) i);
            }
            assert(found_message_len == message_len);
            assert(memcmp(ciphertext2, message, message_len) == 0);

            sodium_free(ciphertext2);
            sodium_free(message);
            sodium_free(ad);
            sodium_free(expected_ciphertext);
            sodium_free(ciphertext);
            sodium_free(decrypted);
            sodium_free(detached_ciphertext);
        } while (++i < (sizeof tests_aead_aes256gcm) / (sizeof tests_aead_aes256gcm[0]));

        sodium_free(key);
        sodium_free(mac);
        sodium_free(nonce);

        return 0;
    }

    static int
    tv2(void)
    {
        unsigned char *ciphertext;
        unsigned char *message;
        unsigned char *message2;
        unsigned char *nonce;
        unsigned char *key;
        size_t         message_len;
        size_t         ciphertext_len;
        int            i;

        for (i = 0; i < 250; i++) {
            message_len = randombytes_uniform(1000);
            ciphertext_len = message_len + crypto_aead_aes256gcm_ABYTES;
            message = (unsigned char *) sodium_malloc(message_len);
            message2 = (unsigned char *) sodium_malloc(message_len);
            ciphertext = (unsigned char *) sodium_malloc(ciphertext_len);
            nonce = (unsigned char *) sodium_malloc(crypto_aead_aes256gcm_NPUBBYTES);
            key = (unsigned char *) sodium_malloc(crypto_aead_aes256gcm_KEYBYTES);

            crypto_aead_aes256gcm_keygen(key);
            randombytes_buf(nonce, crypto_aead_aes256gcm_NPUBBYTES);
            randombytes_buf(message, message_len);
            crypto_aead_aes256gcm_encrypt(ciphertext, NULL, message, message_len,
                                          NULL, 0, NULL, nonce, key);
            if (crypto_aead_aes256gcm_decrypt(message2, NULL, NULL,
                                              ciphertext, ciphertext_len,
                                              NULL, 0, nonce, key) != 0) {
                DebugPrint("Decryption of random ciphertext failed");
            }
            TEST_ASSERT(message_len == 0 || memcmp(message, message2, message_len) == 0);
            sodium_free(key);
            sodium_free(nonce);
            sodium_free(ciphertext);
            sodium_free(message2);
            sodium_free(message);
        }

        return 0;
    }

    ///
    ///\brief _libsodium_unit_aead_aegis256 Unit test for libsodium.
    ///\param[in] void
    ///\return void
    ///
    void _libsodium_ut_aead_aes256gcm()
    {
        if (crypto_aead_aes256gcm_is_available()) {
            tv();
            tv2();
        }
        assert(crypto_aead_aes256gcm_keybytes() == crypto_aead_aes256gcm_KEYBYTES);
        assert(crypto_aead_aes256gcm_nsecbytes() == crypto_aead_aes256gcm_NSECBYTES);
        assert(crypto_aead_aes256gcm_npubbytes() == crypto_aead_aes256gcm_NPUBBYTES);
        assert(crypto_aead_aes256gcm_abytes() == crypto_aead_aes256gcm_ABYTES);
        assert(crypto_aead_aes256gcm_statebytes() >= sizeof(crypto_aead_aes256gcm_state));
        assert(crypto_aead_aes256gcm_messagebytes_max() == crypto_aead_aes256gcm_MESSAGEBYTES_MAX);
    }

} //namespace aead_aes256gcm
