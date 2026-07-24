#include "libsodium_ut_generichash3.h"

#include "unity.h"
//#include "aether/tele.h"

namespace generichash3
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

    int
    test(void)
    {
    #define MAXLEN 64
        crypto_generichash_blake2b_state st;
        unsigned char salt[crypto_generichash_blake2b_SALTBYTES]
            = { '5', 'b', '6', 'b', '4', '1', 'e', 'd',
                '9', 'b', '3', '4', '3', 'f', 'e', '0' };
        unsigned char personal[crypto_generichash_blake2b_PERSONALBYTES]
            = { '5', '1', '2', '6', 'f', 'b', '2', 'a',
                '3', '7', '4', '0', '0', 'd', '2', 'a' };
        unsigned char in[MAXLEN];
        unsigned char out[crypto_generichash_blake2b_BYTES_MAX];
        unsigned char k[crypto_generichash_blake2b_KEYBYTES_MAX];
        size_t        h;
        size_t        i;
        size_t        j;

        TEST_ASSERT(crypto_generichash_blake2b_statebytes() >= sizeof st);
        for (h = 0; h < crypto_generichash_blake2b_KEYBYTES_MAX; ++h) {
            k[h] = (unsigned char) h;
        }

        for (i = 0; i < MAXLEN; ++i) {
            in[i] = (unsigned char) i;
            crypto_generichash_blake2b_init_salt_personal(
                &st, k, 1 + i % crypto_generichash_blake2b_KEYBYTES_MAX,
                1 + i % crypto_generichash_blake2b_BYTES_MAX, salt, personal);
            crypto_generichash_blake2b_update(&st, in, (unsigned long long) i);
            crypto_generichash_blake2b_final(
                &st, out, 1 + i % crypto_generichash_blake2b_BYTES_MAX);
            for (j = 0; j < 1 + i % crypto_generichash_blake2b_BYTES_MAX; ++j) {
                DebugPrint("%02x", (unsigned int) out[j]);
            }
            DebugPrint("\n");
        }

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_init_salt_personal(
            &st, k, 0U, crypto_generichash_blake2b_BYTES_MAX, salt, personal);
        crypto_generichash_blake2b_update(&st, in, MAXLEN);
        crypto_generichash_blake2b_final(&st, out,
                                         crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_init_salt_personal(
            &st, NULL, 1U, crypto_generichash_blake2b_BYTES_MAX, salt, personal);
        crypto_generichash_blake2b_update(&st, in, MAXLEN);
        crypto_generichash_blake2b_final(&st, out,
                                         crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_init_salt_personal(
            &st, k, crypto_generichash_blake2b_KEYBYTES_MAX,
        crypto_generichash_blake2b_BYTES_MAX, NULL, personal);
        crypto_generichash_blake2b_update(&st, in, MAXLEN);
        crypto_generichash_blake2b_final(&st, out,
                                         crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_init_salt_personal(
            &st, k, crypto_generichash_blake2b_KEYBYTES_MAX,
            crypto_generichash_blake2b_BYTES_MAX, salt, NULL);
        crypto_generichash_blake2b_update(&st, in, MAXLEN);
        crypto_generichash_blake2b_final(
            &st, out, crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_salt_personal(
            out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
            k, 0U, salt, personal);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_salt_personal(
            out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
            NULL, 0U, salt, personal);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_salt_personal(
            out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
            k, crypto_generichash_blake2b_KEYBYTES_MAX, salt, personal);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_salt_personal(
            out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
            k, crypto_generichash_blake2b_KEYBYTES_MAX, NULL, personal);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        memset(out, 0, sizeof out);
        crypto_generichash_blake2b_salt_personal(
            out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
            k, crypto_generichash_blake2b_KEYBYTES_MAX, salt, NULL);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        TEST_ASSERT(crypto_generichash_blake2b_salt_personal
               (guard_page, 0,
                in, (unsigned long long) sizeof in,
                k, sizeof k, NULL, NULL) == -1);
        TEST_ASSERT(crypto_generichash_blake2b_salt_personal
               (guard_page, crypto_generichash_BYTES_MAX + 1,
                in, (unsigned long long) sizeof in,
                k, sizeof k, NULL, NULL) == -1);
        TEST_ASSERT(crypto_generichash_blake2b_salt_personal
               (guard_page, (unsigned long long) sizeof in,
                in, (unsigned long long) sizeof in,
                k, crypto_generichash_KEYBYTES_MAX + 1, NULL, NULL) == -1);

        crypto_generichash_blake2b_init_salt_personal(&st, NULL, 0U, crypto_generichash_BYTES,
                                                      NULL, personal);
        crypto_generichash_blake2b_update(&st, in, MAXLEN);
        crypto_generichash_blake2b_final(&st, out, crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        crypto_generichash_blake2b_init_salt_personal(&st, NULL, 0U, crypto_generichash_BYTES,
                                                      salt, NULL);
        crypto_generichash_blake2b_update(&st, in, MAXLEN);
        crypto_generichash_blake2b_final(&st, out, crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
            DebugPrint("%02x", (unsigned int) out[j]);
        }
        DebugPrint("\n");

        TEST_ASSERT(crypto_generichash_blake2b_init_salt_personal
               (&st, k, sizeof k, 0, NULL, NULL) == -1);
        TEST_ASSERT(crypto_generichash_blake2b_init_salt_personal
               (&st, k, sizeof k, crypto_generichash_blake2b_BYTES_MAX + 1, NULL, NULL) == -1);
        TEST_ASSERT(crypto_generichash_blake2b_init_salt_personal
               (&st, k, crypto_generichash_blake2b_KEYBYTES_MAX + 1, sizeof out, NULL, NULL) == -1);

        TEST_ASSERT(crypto_generichash_blake2b_init_salt_personal(&st, k, sizeof k, crypto_generichash_BYTES,
                                                             NULL, personal) == 0);
        TEST_ASSERT(crypto_generichash_blake2b_init_salt_personal(&st, k, sizeof k, crypto_generichash_BYTES,
                                                             salt, NULL) == 0);
        return 0;
    }

    void _libsodium_ut_generichash3()
    {
        test();
    }
} // namespace
