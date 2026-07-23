#include "libsodium_ut_secretbox_easy.h"

#include "unity.h"
//#include "aether/tele.h"

namespace secretbox_easy
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
        unsigned char *c;
        unsigned char *mac;
        size_t         i;

        c   = (unsigned char *) sodium_malloc(131 + crypto_secretbox_MACBYTES + 1);
        mac = (unsigned char *) sodium_malloc(crypto_secretbox_MACBYTES);
        TEST_ASSERT(c != NULL && mac != NULL);

        crypto_secretbox_easy(c, m, 131, nonce, firstkey);
        for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        crypto_secretbox_detached(c, mac, m, 131, nonce, firstkey);
        for (i = 0; i < crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) mac[i]);
        }
        for (i = 0; i < 131; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        /* Same test, with c and m overlapping */

        memcpy(c + 1, m, 131);
        crypto_secretbox_easy(c, c + 1, 131, nonce, firstkey);
        for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        memcpy(c, m, 131);
        crypto_secretbox_easy(c + 1, c, 131, nonce, firstkey);
        for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i + 1]);
        }
        DebugPrint("\n");

        memcpy(c, m, 131);
        crypto_secretbox_easy(c, c, 131, nonce, firstkey);
        for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        TEST_ASSERT(crypto_secretbox_easy(c, m, 0, nonce, firstkey) == 0);

        /* Null message */

        crypto_secretbox_easy(c, c, 0, nonce, firstkey);
        for (i = 0; i < crypto_secretbox_MACBYTES + 1; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");
        if (crypto_secretbox_open_easy(c, c, crypto_secretbox_MACBYTES, nonce,
                                       firstkey) != 0) {
            DebugPrint("Null crypto_secretbox_open_easy() failed\n");
        }
        for (i = 0; i < crypto_secretbox_MACBYTES + 1; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");
        c[randombytes_uniform(crypto_secretbox_MACBYTES)]++;
        if (crypto_secretbox_open_easy(c, c, crypto_secretbox_MACBYTES, nonce,
                                       firstkey) != -1) {
            DebugPrint("Null tampered crypto_secretbox_open_easy() failed\n");
        }

        /* No overlap, but buffers are next to each other */

        memset(c, 0, 131 + crypto_secretbox_MACBYTES + 1);
        memcpy(c, m, 20);
        crypto_secretbox_easy(c, c + 10, 10, nonce, firstkey);
        for (i = 0; i < 10 + crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        memset(c, 0, 131 + crypto_secretbox_MACBYTES + 1);
        memcpy(c, m, 20);
        crypto_secretbox_easy(c + 10, c, 10, nonce, firstkey);
        for (i = 0; i < 10 + crypto_secretbox_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        sodium_free(mac);
        sodium_free(c);

        return 0;
    }

    void _libsodium_ut_secretbox_easy()
    {
        test();
    }
} // namespace
