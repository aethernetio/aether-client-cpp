#include "libsodium_ut_onetimeauth.h"

#include "unity.h"
//#include "aether/tele.h"

namespace onetimeauth
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
        crypto_onetimeauth_state st;
        int                      i;

        TEST_ASSERT(crypto_onetimeauth_statebytes() == sizeof st);
        crypto_onetimeauth(a, c, 131, rs);
        for (i = 0; i < 16; ++i) {
            DebugPrint(",0x%02x", (unsigned int) a[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }

        memset(a, 0, sizeof a);
        crypto_onetimeauth_init(&st, rs);
        crypto_onetimeauth_update(&st, c, 100);
        crypto_onetimeauth_update(&st, c, 0);
        crypto_onetimeauth_update(&st, c + 100, 31);
        crypto_onetimeauth_final(&st, a);
        for (i = 0; i < 16; ++i) {
            DebugPrint(",0x%02x", (unsigned int) a[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }

        TEST_ASSERT(crypto_onetimeauth_bytes() > 0U);
        TEST_ASSERT(crypto_onetimeauth_keybytes() > 0U);
        TEST_ASSERT(strcmp(crypto_onetimeauth_primitive(), "poly1305") == 0);
        TEST_ASSERT(crypto_onetimeauth_poly1305_bytes() == crypto_onetimeauth_bytes());
        TEST_ASSERT(crypto_onetimeauth_poly1305_keybytes() ==
               crypto_onetimeauth_keybytes());
        TEST_ASSERT(crypto_onetimeauth_statebytes() > 0);
        TEST_ASSERT(crypto_onetimeauth_statebytes() == crypto_onetimeauth_poly1305_statebytes());

        return 0;
    }

    void _libsodium_ut_onetimeauth()
    {
        test();
    }
} // namespace
