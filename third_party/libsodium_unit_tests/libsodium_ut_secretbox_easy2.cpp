#include "libsodium_ut_secretbox_easy2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace secretbox_easy2
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
        unsigned char *m;
        unsigned char *m2;
        unsigned char *c;
        unsigned char *nonce;
        unsigned char *k;
        unsigned char *mac;
        size_t         mlen;
        size_t         i;

        mlen  = (size_t) randombytes_uniform((uint32_t) 10000) + 1U;
        m     = (unsigned char *) sodium_malloc(mlen);
        m2    = (unsigned char *) sodium_malloc(mlen);
        c     = (unsigned char *) sodium_malloc(crypto_secretbox_MACBYTES + mlen);
        nonce = (unsigned char *) sodium_malloc(crypto_secretbox_NONCEBYTES);
        k     = (unsigned char *) sodium_malloc(crypto_secretbox_KEYBYTES);
        mac   = (unsigned char *) sodium_malloc(crypto_secretbox_MACBYTES);
        crypto_secretbox_keygen(k);
        randombytes_buf(m, mlen);
        randombytes_buf(nonce, crypto_secretbox_NONCEBYTES);
        crypto_secretbox_easy(c, m, (unsigned long long) mlen, nonce, k);
        if (crypto_secretbox_open_easy(
                m2, c, (unsigned long long) mlen + crypto_secretbox_MACBYTES, nonce,
                k) != 0) {
            DebugPrint("crypto_secretbox_open_easy() failed\n");
        }
        DebugPrint("%d\n", memcmp(m, m2, mlen));

        for (i = 0; i < mlen + crypto_secretbox_MACBYTES - 1; i++) {
            if (crypto_secretbox_open_easy(m2, c, (unsigned long long) i, nonce,
                                           k) == 0) {
                DebugPrint("short open() should have failed\n");
                return 1;
            }
        }
        crypto_secretbox_detached(c, mac, m, (unsigned long long) mlen, nonce, k);
        if (crypto_secretbox_open_detached(NULL, c, mac, (unsigned long long) mlen,
                                           nonce, k) != 0) {
            DebugPrint("crypto_secretbox_open_detached() with a NULL message pointer failed\n");
        }
        if (crypto_secretbox_open_detached(m2, c, mac, (unsigned long long) mlen,
                                           nonce, k) != 0) {
            DebugPrint("crypto_secretbox_open_detached() failed\n");
        }
        DebugPrint("%d\n", memcmp(m, m2, mlen));

        memcpy(c, m, mlen);
        crypto_secretbox_easy(c, c, (unsigned long long) mlen, nonce, k);
        DebugPrint("%d\n", memcmp(m, c, mlen) == 0);
        DebugPrint("%d\n", memcmp(m, c + crypto_secretbox_MACBYTES, mlen) == 0);
        if (crypto_secretbox_open_easy(
                c, c, (unsigned long long) mlen + crypto_secretbox_MACBYTES, nonce,
                k) != 0) {
            DebugPrint("crypto_secretbox_open_easy() failed\n");
        }
        DebugPrint("%d\n", memcmp(m, c, mlen));

        sodium_free(m);
        sodium_free(m2);
        sodium_free(c);
        sodium_free(nonce);
        sodium_free(k);
        sodium_free(mac);

        return 0;
    }

    void _libsodium_ut_secretbox_easy2()
    {
        test();
    }
} // namespace
