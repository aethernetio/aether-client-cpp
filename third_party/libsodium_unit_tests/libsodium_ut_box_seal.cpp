#include "libsodium_ut_box_seal.h"

#include "unity.h"
//#include "aether/tele.h"

namespace box_seal
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

    static
    void tv1(void)
    {
        unsigned char  pk[crypto_box_PUBLICKEYBYTES];
        unsigned char  sk[crypto_box_SECRETKEYBYTES];
        unsigned char *c;
        unsigned char *m;
        unsigned char *m2;
        size_t         m_len;
        size_t         c_len;

        crypto_box_keypair(pk, sk);
        m_len = (size_t) randombytes_uniform(1000);
        c_len = crypto_box_SEALBYTES + m_len;
        m     = (unsigned char *) sodium_malloc(m_len);
        m2    = (unsigned char *) sodium_malloc(m_len);
        c     = (unsigned char *) sodium_malloc(c_len);
        randombytes_buf(m, m_len);
        if (crypto_box_seal(c, m, m_len, pk) != 0) {
            DebugPrint("crypto_box_seal() failure\n");
            return;
        }
        if (crypto_box_seal_open(m2, c, c_len, pk, sk) != 0) {
            DebugPrint("crypto_box_seal_open() failure\n");
            return;
        }
        DebugPrint("%d\n", memcmp(m, m2, m_len));

        DebugPrint("%d\n", crypto_box_seal_open(m, c, 0U, pk, sk));
        DebugPrint("%d\n", crypto_box_seal_open(m, c, c_len - 1U, pk, sk));
        DebugPrint("%d\n", crypto_box_seal_open(m, c, c_len, sk, pk));

        sodium_free(c);
        sodium_free(m);
        sodium_free(m2);

        TEST_ASSERT(crypto_box_sealbytes() == crypto_box_SEALBYTES);
    }

    static
    void tv2(void)
    {
        unsigned char  pk[crypto_box_PUBLICKEYBYTES];
        unsigned char  sk[crypto_box_SECRETKEYBYTES];
        unsigned char *cm;
        unsigned char *m2;
        size_t         m_len;
        size_t         cm_len;

        crypto_box_keypair(pk, sk);
        m_len = (size_t) randombytes_uniform(1000);
        cm_len = crypto_box_SEALBYTES + m_len;
        m2    = (unsigned char *) sodium_malloc(m_len);
        cm    = (unsigned char *) sodium_malloc(cm_len);
        randombytes_buf(cm, m_len);
        if (crypto_box_seal(cm, cm, m_len, pk) != 0) {
            DebugPrint("crypto_box_seal() failure\n");
            return;
        }
        if (crypto_box_seal_open(m2, cm, cm_len, pk, sk) != 0) {
            DebugPrint("crypto_box_seal_open() failure\n");
            return;
        }
        TEST_ASSERT(m_len == 0 || memcmp(cm, m2, m_len) != 0);
        sodium_free(cm);
        sodium_free(m2);
    }

    #ifndef SODIUM_LIBRARY_MINIMAL
    static
    void tv3(void)
    {
        unsigned char  pk[crypto_box_curve25519xchacha20poly1305_PUBLICKEYBYTES];
        unsigned char  sk[crypto_box_curve25519xchacha20poly1305_SECRETKEYBYTES];
        unsigned char *c;
        unsigned char *m;
        unsigned char *m2;
        size_t         m_len;
        size_t         c_len;

        crypto_box_curve25519xchacha20poly1305_keypair(pk, sk);
        m_len = (size_t) randombytes_uniform(1000);
        c_len = crypto_box_curve25519xchacha20poly1305_SEALBYTES + m_len;
        m     = (unsigned char *) sodium_malloc(m_len);
        m2    = (unsigned char *) sodium_malloc(m_len);
        c     = (unsigned char *) sodium_malloc(c_len);
        randombytes_buf(m, m_len);
        if (crypto_box_curve25519xchacha20poly1305_seal(c, m, m_len, pk) != 0) {
            DebugPrint("crypto_box_curve25519xchacha20poly1305_seal() failure\n");
            return;
        }
        if (crypto_box_curve25519xchacha20poly1305_seal_open(m2, c, c_len, pk, sk) != 0) {
            DebugPrint("crypto_box_curve25519xchacha20poly1305_seal_open() failure\n");
            return;
        }
        DebugPrint("%d\n", memcmp(m, m2, m_len));

        DebugPrint("%d\n", crypto_box_curve25519xchacha20poly1305_seal_open(m, c, 0U, pk, sk));
        DebugPrint("%d\n", crypto_box_curve25519xchacha20poly1305_seal_open(m, c, c_len - 1U, pk, sk));
        DebugPrint("%d\n", crypto_box_curve25519xchacha20poly1305_seal_open(m, c, c_len, sk, pk));

        sodium_free(c);
        sodium_free(m);
        sodium_free(m2);

        TEST_ASSERT(crypto_box_curve25519xchacha20poly1305_sealbytes() ==
               crypto_box_curve25519xchacha20poly1305_SEALBYTES);
    }

    static
    void tv4(void)
    {
        unsigned char  pk[crypto_box_curve25519xchacha20poly1305_PUBLICKEYBYTES];
        unsigned char  sk[crypto_box_curve25519xchacha20poly1305_SECRETKEYBYTES];
        unsigned char *cm;
        unsigned char *m2;
        size_t         m_len;
        size_t         cm_len;

        crypto_box_curve25519xchacha20poly1305_keypair(pk, sk);
        m_len = (size_t) randombytes_uniform(1000);
        cm_len = crypto_box_curve25519xchacha20poly1305_SEALBYTES + m_len;
        m2    = (unsigned char *) sodium_malloc(m_len);
        cm    = (unsigned char *) sodium_malloc(cm_len);
        randombytes_buf(cm, m_len);
        if (crypto_box_curve25519xchacha20poly1305_seal(cm, cm, m_len, pk) != 0) {
            DebugPrint("crypto_box_curve25519xchacha20poly1305_seal() failure\n");
            return;
        }
        if (crypto_box_curve25519xchacha20poly1305_seal_open(m2, cm, cm_len, pk, sk) != 0) {
            DebugPrint("crypto_box_curve25519xchacha20poly1305_seal_open() failure\n");
            return;
        }
        TEST_ASSERT(m_len == 0 || memcmp(cm, m2, m_len) != 0);
        sodium_free(cm);
        sodium_free(m2);
    }

    #else

    static
    void tv3(void)
    {
        DebugPrint("0\n-1\n-1\n-1\n");
    }

    static
    void tv4(void)
    { }
    #endif

    void _libsodium_ut_box_seal()
    {
        tv1();
        tv2();
        tv3();
        tv4();
    }
} // namespace box_seal
