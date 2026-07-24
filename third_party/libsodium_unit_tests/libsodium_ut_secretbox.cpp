#include "libsodium_ut_secretbox.h"

#include "unity.h"
//#include "aether/tele.h"

namespace secretbox
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
        int i;

        crypto_secretbox(c, m, 163, nonce, firstkey);
        for (i = 16; i < 163; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }
        DebugPrint("\n");

        memcpy(c, m, 163);
        crypto_secretbox(c, c, 163, nonce, firstkey);
        for (i = 16; i < 163; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }
        DebugPrint("\n");

        TEST_ASSERT(crypto_secretbox(c, c, 31, nonce, firstkey) == -1);
        TEST_ASSERT(crypto_secretbox(c, c, 12, nonce, firstkey) == -1);
        TEST_ASSERT(crypto_secretbox(c, c, 1, nonce, firstkey) == -1);
        TEST_ASSERT(crypto_secretbox(c, c, 0, nonce, firstkey) == -1);

        TEST_ASSERT(crypto_secretbox_keybytes() > 0U);
        TEST_ASSERT(crypto_secretbox_noncebytes() > 0U);
        TEST_ASSERT(crypto_secretbox_zerobytes() > 0U);
        TEST_ASSERT(crypto_secretbox_boxzerobytes() > 0U);
        TEST_ASSERT(crypto_secretbox_macbytes() > 0U);
        TEST_ASSERT(crypto_secretbox_messagebytes_max() > 0U);
        TEST_ASSERT(strcmp(crypto_secretbox_primitive(), "xsalsa20poly1305") == 0);
        TEST_ASSERT(crypto_secretbox_keybytes() ==
               crypto_secretbox_xsalsa20poly1305_keybytes());
        TEST_ASSERT(crypto_secretbox_noncebytes() ==
               crypto_secretbox_xsalsa20poly1305_noncebytes());
        TEST_ASSERT(crypto_secretbox_zerobytes() ==
               crypto_secretbox_xsalsa20poly1305_zerobytes());
        TEST_ASSERT(crypto_secretbox_boxzerobytes() ==
               crypto_secretbox_xsalsa20poly1305_boxzerobytes());
        TEST_ASSERT(crypto_secretbox_macbytes() ==
               crypto_secretbox_xsalsa20poly1305_macbytes());
        TEST_ASSERT(crypto_secretbox_messagebytes_max() ==
               crypto_secretbox_xsalsa20poly1305_messagebytes_max());

        return 0;
    }

    void _libsodium_ut_secretbox()
    {
        test();
    }
} // namespace
