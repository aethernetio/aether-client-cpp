#include "libsodium_ut_aead_chacha20poly13052.h"

#include "unity.h"
//#include "aether/tele.h"

namespace aead_chacha20poly13052
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
        unsigned char *ad;
        unsigned char *decrypted;
        unsigned char *detached_ciphertext;
        unsigned char *key;
        unsigned char *message;
        unsigned char *mac;
        unsigned char *nonce;
        size_t         ad_len;
        size_t         detached_ciphertext_len;
        size_t         message_len;
        unsigned int   i;

        key = (unsigned char *) sodium_malloc(
            crypto_aead_chacha20poly1305_ietf_KEYBYTES);
        nonce = (unsigned char *) sodium_malloc(
            crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
        mac = (unsigned char *) sodium_malloc(
            crypto_aead_chacha20poly1305_ietf_ABYTES);

        for (i = 0U; i < (sizeof tests_aead_chacha20poly13052) / (sizeof tests_aead_chacha20poly13052[0]); i++) {
            TEST_ASSERT(strlen(tests_aead_chacha20poly13052[i].key_hex) ==
                   2 * crypto_aead_chacha20poly1305_ietf_KEYBYTES);
            sodium_hex2bin(key, crypto_aead_chacha20poly1305_ietf_KEYBYTES,
                           tests_aead_chacha20poly13052[i].key_hex, strlen(tests_aead_chacha20poly13052[i].key_hex), NULL, NULL,
                           NULL);

            TEST_ASSERT(strlen(tests_aead_chacha20poly13052[i].nonce_hex) ==
                   2 * crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
            sodium_hex2bin(nonce, crypto_aead_chacha20poly1305_ietf_NPUBBYTES,
                           tests_aead_chacha20poly13052[i].nonce_hex, strlen(tests_aead_chacha20poly13052[i].nonce_hex), NULL,
                           NULL, NULL);

            message_len = strlen(tests_aead_chacha20poly13052[i].message_hex) / 2;
            message     = (unsigned char *) sodium_malloc(message_len);
            sodium_hex2bin(message, message_len, tests_aead_chacha20poly13052[i].message_hex,
                           strlen(tests_aead_chacha20poly13052[i].message_hex), NULL, NULL, NULL);

            ad_len = strlen(tests_aead_chacha20poly13052[i].ad_hex) / 2;
            ad     = (unsigned char *) sodium_malloc(ad_len);
            sodium_hex2bin(ad, ad_len, tests_aead_chacha20poly13052[i].ad_hex, strlen(tests_aead_chacha20poly13052[i].ad_hex),
                           NULL, NULL, NULL);

            detached_ciphertext_len = message_len;
            TEST_ASSERT(strlen(tests_aead_chacha20poly13052[i].detached_ciphertext_hex) == 2 * message_len);
            TEST_ASSERT(strlen(tests_aead_chacha20poly13052[i].mac_hex) ==
                   2 * crypto_aead_chacha20poly1305_ietf_ABYTES);
            sodium_hex2bin(mac, crypto_aead_chacha20poly1305_ietf_ABYTES,
                           tests_aead_chacha20poly13052[i].mac_hex, strlen(tests_aead_chacha20poly13052[i].mac_hex), NULL, NULL,
                           NULL);

            detached_ciphertext =
                (unsigned char *) sodium_malloc(detached_ciphertext_len);
            sodium_hex2bin(detached_ciphertext, detached_ciphertext_len,
                           tests_aead_chacha20poly13052[i].detached_ciphertext_hex,
                           strlen(tests_aead_chacha20poly13052[i].detached_ciphertext_hex), NULL, NULL,
                           NULL);

            decrypted = (unsigned char *) sodium_malloc(message_len);
            if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(
                    decrypted, NULL, detached_ciphertext, detached_ciphertext_len,
                    mac, ad, ad_len, nonce, key) == 0) {
                if (strcmp(tests_aead_chacha20poly13052[i].outcome, "valid") != 0) {
                    DebugPrint("*** test case %u succeeded, was supposed to be %s", i, tests_aead_chacha20poly13052[i].outcome);
                }
                if (memcmp(decrypted, message, message_len) != 0) {
                    DebugPrint("Incorrect decryption of test vector #%u", (unsigned int) i);
                }
            } else {
                if (strcmp(tests_aead_chacha20poly13052[i].outcome, "invalid") != 0) {
                    DebugPrint("*** test case %u failed, was supposed to be %s", i, tests_aead_chacha20poly13052[i].outcome);
                }
            }

            sodium_free(message);
            sodium_free(ad);
            sodium_free(decrypted);
            sodium_free(detached_ciphertext);
        }

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
    void _libsodium_ut_aead_chacha20poly13052()
    {
        tv();
    }
} //namespace aead_chacha20poly13052
