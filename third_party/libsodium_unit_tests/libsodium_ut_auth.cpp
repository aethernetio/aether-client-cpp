#include "libsodium_ut_auth.h"

#include "unity.h"
//#include "aether/tele.h"

namespace auth
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
        crypto_auth_hmacsha512_state st;
        crypto_auth_hmacsha256_state st256;
        crypto_auth_hmacsha512256_state st512_256;
        size_t                       i;

        TEST_ASSERT(crypto_auth_hmacsha512_statebytes() ==
               sizeof(crypto_auth_hmacsha512_state));
        crypto_auth(a, c, sizeof c - 1U, key);
        for (i = 0; i < sizeof a; ++i) {
            DebugPrint(",0x%02x", (unsigned int) a[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }
        DebugPrint("\n");

        crypto_auth_hmacsha512_init(&st, key, sizeof key);
        crypto_auth_hmacsha512_update(&st, c, 1U);
        crypto_auth_hmacsha512_update(&st, c, sizeof c - 2U);
        crypto_auth_hmacsha512_final(&st, a2);
        for (i = 0; i < sizeof a2; ++i) {
            DebugPrint(",0x%02x", (unsigned int) a2[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }
        DebugPrint("\n");

        crypto_auth_hmacsha512_init(&st, key2, sizeof key2);
        crypto_auth_hmacsha512_update(&st, c, 1U);
        crypto_auth_hmacsha512_update(&st, c, sizeof c - 2U);
        crypto_auth_hmacsha512_final(&st, a2);
        for (i = 0; i < sizeof a2; ++i) {
            DebugPrint(",0x%02x", (unsigned int) a2[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }

        memset(a2, 0, sizeof a2);
        crypto_auth_hmacsha256_init(&st256, key2, sizeof key2);
        crypto_auth_hmacsha256_update(&st256, guard_page, 0U);
        crypto_auth_hmacsha256_update(&st256, c, 1U);
        crypto_auth_hmacsha256_update(&st256, c, sizeof c - 2U);
        crypto_auth_hmacsha256_final(&st256, a2);
        for (i = 0; i < sizeof a2; ++i) {
            DebugPrint(",0x%02x", (unsigned int) a2[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }

        /* Empty message tests: HMAC-SHA512 */
        memset(a2, 0, sizeof a2);
        crypto_auth_hmacsha512_init(&st, key, sizeof key);
        crypto_auth_hmacsha512_final(&st, a2);

        memset(a3, 0, sizeof a3);
        crypto_auth_hmacsha512_init(&st, key, sizeof key);
        crypto_auth_hmacsha512_update(&st, a2, 0U);
        crypto_auth_hmacsha512_final(&st, a3);
        TEST_ASSERT(sodium_memcmp(a2, a3, sizeof a2) == 0);

        memset(a3, 0, sizeof a3);
        crypto_auth_hmacsha512_init(&st, key, sizeof key);
        crypto_auth_hmacsha512_update(&st, guard_page, 0U);
        crypto_auth_hmacsha512_final(&st, a3);
        TEST_ASSERT(sodium_memcmp(a2, a3, sizeof a2) == 0);

        /* Empty message tests: HMAC-SHA512-256 */
        memset(a2, 0, sizeof a2);
        crypto_auth_hmacsha512256_init(&st512_256, key, sizeof key);
        crypto_auth_hmacsha512256_final(&st512_256, a2);

        memset(a3, 0, sizeof a3);
        crypto_auth_hmacsha512256_init(&st512_256, key, sizeof key);
        crypto_auth_hmacsha512256_update(&st512_256, a2, 0U);
        crypto_auth_hmacsha512256_final(&st512_256, a3);
        TEST_ASSERT(sodium_memcmp(a2, a3, sizeof a2) == 0);

        memset(a3, 0, sizeof a3);
        crypto_auth_hmacsha512256_init(&st512_256, key, sizeof key);
        crypto_auth_hmacsha512256_update(&st512_256, guard_page, 0U);
        crypto_auth_hmacsha512256_final(&st512_256, a3);
        TEST_ASSERT(sodium_memcmp(a2, a3, sizeof a2) == 0);

        /* Empty message tests: HMAC-SHA256 */

        memset(a2, 0, sizeof a2);
        crypto_auth_hmacsha256_init(&st256, key, sizeof key);
        crypto_auth_hmacsha256_final(&st256, a2);

        memset(a3, 0, sizeof a3);
        crypto_auth_hmacsha256_init(&st256, key, sizeof key);
        crypto_auth_hmacsha256_update(&st256, a2, 0U);
        crypto_auth_hmacsha256_final(&st256, a3);
        TEST_ASSERT(sodium_memcmp(a2, a3, sizeof a2) == 0);

        memset(a3, 0, sizeof a3);
        crypto_auth_hmacsha256_init(&st256, key, sizeof key);
        crypto_auth_hmacsha256_update(&st256, guard_page, 0U);
        crypto_auth_hmacsha256_final(&st256, a3);
        TEST_ASSERT(sodium_memcmp(a2, a3, sizeof a2) == 0);

        /* --- */

        TEST_ASSERT(crypto_auth_bytes() > 0U);
        TEST_ASSERT(crypto_auth_keybytes() > 0U);
        TEST_ASSERT(strcmp(crypto_auth_primitive(), "hmacsha512256") == 0);
        TEST_ASSERT(crypto_auth_hmacsha256_bytes() > 0U);
        TEST_ASSERT(crypto_auth_hmacsha256_keybytes() > 0U);
        TEST_ASSERT(crypto_auth_hmacsha512_bytes() > 0U);
        TEST_ASSERT(crypto_auth_hmacsha512_keybytes() > 0U);
        TEST_ASSERT(crypto_auth_hmacsha512256_bytes() == crypto_auth_bytes());
        TEST_ASSERT(crypto_auth_hmacsha512256_keybytes() == crypto_auth_keybytes());
        TEST_ASSERT(crypto_auth_hmacsha512256_statebytes() >=
               crypto_auth_hmacsha512256_keybytes());
        TEST_ASSERT(crypto_auth_hmacsha256_statebytes() ==
               sizeof(crypto_auth_hmacsha256_state));
        TEST_ASSERT(crypto_auth_hmacsha512_statebytes() ==
               sizeof(crypto_auth_hmacsha512_state));

        return 0;
    }

void _libsodium_ut_auth(void)
    {
        test();
    }
} //namespace auth
