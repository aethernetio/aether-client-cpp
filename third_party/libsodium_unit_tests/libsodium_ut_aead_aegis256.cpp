#include "libsodium_ut_aead_aegis256.h"

#include "unity.h"
//#include "aether/tele.h"

namespace aead_aegis256
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

    static int
    tv(void)
    {
        unsigned char     *ad;
        unsigned char     *ciphertext;
        unsigned char     *decrypted;
        unsigned char     *detached_ciphertext;
        unsigned char     *expected_ciphertext;
        unsigned char     *key;
        unsigned char     *message;
        unsigned char     *mac;
        unsigned char     *nonce;
        char              *hex;
        unsigned long long found_ciphertext_len;
        unsigned long long found_mac_len;
        unsigned long long found_message_len;
        size_t             ad_len;
        size_t             ciphertext_len;
        size_t             detached_ciphertext_len;
        size_t             i = 0U;
        size_t             message_len;

        key   = (unsigned char *) sodium_malloc(crypto_aead_aegis256_KEYBYTES);
        nonce = (unsigned char *) sodium_malloc(crypto_aead_aegis256_NPUBBYTES);
        mac   = (unsigned char *) sodium_malloc(crypto_aead_aegis256_ABYTES);

        do {
            TEST_ASSERT(strlen(tests_aead_aegis256[i].key_hex) == 2 * crypto_aead_aegis256_KEYBYTES);
            sodium_hex2bin(key, crypto_aead_aegis256_KEYBYTES, tests_aead_aegis256[i].key_hex,
                           strlen(tests_aead_aegis256[i].key_hex), NULL, NULL, NULL);
            TEST_ASSERT(strlen(tests_aead_aegis256[i].nonce_hex) == 2 * crypto_aead_aegis256_NPUBBYTES);
            sodium_hex2bin(nonce, crypto_aead_aegis256_NPUBBYTES, tests_aead_aegis256[i].nonce_hex,
                           strlen(tests_aead_aegis256[i].nonce_hex), NULL, NULL, NULL);
            message_len = strlen(tests_aead_aegis256[i].message_hex) / 2;
            message     = (unsigned char *) sodium_malloc(message_len);
            sodium_hex2bin(message, message_len, tests_aead_aegis256[i].message_hex, strlen(tests_aead_aegis256[i].message_hex),
                           NULL, NULL, NULL);
            ad_len = strlen(tests_aead_aegis256[i].ad_hex) / 2;
            ad     = (unsigned char *) sodium_malloc(ad_len);
            sodium_hex2bin(ad, ad_len, tests_aead_aegis256[i].ad_hex, strlen(tests_aead_aegis256[i].ad_hex), NULL, NULL, NULL);
            ciphertext_len          = message_len + crypto_aead_aegis256_ABYTES;
            detached_ciphertext_len = message_len;
            expected_ciphertext     = (unsigned char *) sodium_malloc(ciphertext_len);
            TEST_ASSERT(strlen(tests_aead_aegis256[i].ciphertext_hex) == 2 * message_len);
            sodium_hex2bin(expected_ciphertext, message_len, tests_aead_aegis256[i].ciphertext_hex,
                           strlen(tests_aead_aegis256[i].ciphertext_hex), NULL, NULL, NULL);
            TEST_ASSERT(strlen(tests_aead_aegis256[i].mac_hex) == 2 * crypto_aead_aegis256_ABYTES);
            sodium_hex2bin(expected_ciphertext + message_len, crypto_aead_aegis256_ABYTES,
                           tests_aead_aegis256[i].mac_hex, strlen(tests_aead_aegis256[i].mac_hex), NULL, NULL, NULL);
            ciphertext          = (unsigned char *) sodium_malloc(ciphertext_len);
            detached_ciphertext = (unsigned char *) sodium_malloc(detached_ciphertext_len);

            crypto_aead_aegis256_encrypt_detached(detached_ciphertext, mac, &found_mac_len, message,
                                                  message_len, ad, ad_len, NULL, nonce, key);
            TEST_ASSERT(found_mac_len == crypto_aead_aegis256_ABYTES);
            if (memcmp(detached_ciphertext, expected_ciphertext, detached_ciphertext_len) != 0 ||
                memcmp(mac, expected_ciphertext + message_len, crypto_aead_aegis256_ABYTES) != 0) {
                DebugPrint("Detached encryption of test vector #%u failed", (unsigned int)i);
                hex = (char *) sodium_malloc((size_t) ciphertext_len * 2 + 1);
                sodium_bin2hex(hex, (size_t) ciphertext_len * 2 + 1, ciphertext, ciphertext_len);
                DebugPrint("Computed: [%s]", hex);
                sodium_free(hex);
            }

            crypto_aead_aegis256_encrypt(ciphertext, &found_ciphertext_len, message, message_len, ad,
                                         ad_len, NULL, nonce, key);

            TEST_ASSERT((size_t) found_ciphertext_len == ciphertext_len);
            if (memcmp(ciphertext, expected_ciphertext, ciphertext_len) != 0) {
                DebugPrint("Encryption of test vector #%u failed", (unsigned int)i);
                hex = (char *) sodium_malloc((size_t) found_ciphertext_len * 2 + 1);
                sodium_bin2hex(hex, (size_t) found_ciphertext_len * 2 + 1, ciphertext, ciphertext_len);
                DebugPrint("Computed: [%s]", hex);
                sodium_free(hex);
            }

            decrypted         = (unsigned char *) sodium_malloc(message_len);
            found_message_len = 1;
            if (crypto_aead_aegis256_decrypt(decrypted, &found_message_len, NULL, ciphertext,
                                             randombytes_uniform((uint32_t) ciphertext_len), ad, ad_len,
                                             nonce, key) != -1) {
                DebugPrint("Verification of test vector #%u after truncation succeeded", (unsigned int)i);
            }
            if (found_message_len != 0) {
                DebugPrint("Message length should have been set to zero after a failure");
            }
            if (crypto_aead_aegis256_decrypt(decrypted, &found_message_len, NULL, guard_page,
                                             randombytes_uniform(crypto_aead_aegis256_ABYTES), ad,
                                             ad_len, nonce, key) != -1) {
                DebugPrint("Verification of test vector #%u with a truncated tag failed", (unsigned int) i);
            }
            if (i == 0 && crypto_aead_aegis256_decrypt(NULL, NULL, NULL, ciphertext, ciphertext_len, ad,
                                                       ad_len, nonce, key) != 0) {
                DebugPrint("Verification of test vector #%u's tag failed", (unsigned int)i);
            }
            if (crypto_aead_aegis256_decrypt(decrypted, &found_message_len, NULL, ciphertext,
                                             ciphertext_len, ad, ad_len, nonce, key) != 0) {
                DebugPrint("Verification of test vector #%u failed", (unsigned int)i);
            }
            TEST_ASSERT((size_t) found_message_len == message_len);
            if (memcmp(decrypted, message, message_len) != 0) {
                DebugPrint("Verification of test vector #%u failed", (unsigned int)i);
            }
            memset(decrypted, 0xd0, message_len);
            if (crypto_aead_aegis256_decrypt_detached(decrypted, NULL, detached_ciphertext,
                                                      detached_ciphertext_len, mac, ad, ad_len, nonce,
                                                      key) != 0) {
                DebugPrint("Detached verification of test vector #%u failed", (unsigned int)i);
            }
            if (memcmp(decrypted, message, message_len) != 0) {
                DebugPrint("Incorrect decryption of test vector #%u", (unsigned int)i);
            }

            sodium_free(message);
            sodium_free(ad);
            sodium_free(expected_ciphertext);
            sodium_free(ciphertext);
            sodium_free(decrypted);
            sodium_free(detached_ciphertext);
        } while (++i < (sizeof tests_aead_aegis256) / (sizeof tests_aead_aegis256[0]));

        sodium_free(key);
        sodium_free(mac);
        sodium_free(nonce);

        return 0;
    }

    ///
    ///\brief _libsodium_unit_aead_aegis256 Unit test for libsodium.
    ///\param[in] void
    ///\return void
    ///
    void _libsodium_ut_aead_aegis256()
    {
        tv();
        assert(crypto_aead_aegis256_keybytes() == crypto_aead_aegis256_KEYBYTES);
        assert(crypto_aead_aegis256_nsecbytes() == crypto_aead_aegis256_NSECBYTES);
        assert(crypto_aead_aegis256_npubbytes() == crypto_aead_aegis256_NPUBBYTES);
        assert(crypto_aead_aegis256_abytes() == crypto_aead_aegis256_ABYTES);
        assert(crypto_aead_aegis256_messagebytes_max() == crypto_aead_aegis256_MESSAGEBYTES_MAX);
    }
} //namespace aead_aegis256
