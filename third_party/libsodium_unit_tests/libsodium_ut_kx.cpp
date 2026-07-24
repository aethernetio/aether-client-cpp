#include "libsodium_ut_kx.h"

#include "unity.h"
//#include "aether/tele.h"

namespace kx
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

    static void
    tv_kx(void)
    {
        unsigned char *seed;
        unsigned char *client_pk, *client_sk;
        unsigned char *client_rx, *client_tx;
        unsigned char *server_pk, *server_sk;
        unsigned char *server_rx, *server_tx;
        char           hex[65];
        int            i;

        seed = (unsigned char *) sodium_malloc(crypto_kx_SEEDBYTES);
        for (i = 0; i < crypto_kx_SEEDBYTES; i++) {
            seed[i] = (unsigned char) i;
        }
        client_pk = (unsigned char *) sodium_malloc(crypto_kx_PUBLICKEYBYTES);
        client_sk = (unsigned char *) sodium_malloc(crypto_kx_SECRETKEYBYTES);
        crypto_kx_seed_keypair(client_pk, client_sk, seed);

        sodium_bin2hex(hex, sizeof hex, client_pk, crypto_kx_PUBLICKEYBYTES);
        DebugPrint("client_pk: [%s]\n", hex);
        sodium_bin2hex(hex, sizeof hex, client_sk, crypto_kx_SECRETKEYBYTES);
        DebugPrint("client_sk: [%s]\n", hex);

        server_pk = (unsigned char *) sodium_malloc(crypto_kx_PUBLICKEYBYTES);
        server_sk = (unsigned char *) sodium_malloc(crypto_kx_SECRETKEYBYTES);
        crypto_kx_keypair(server_pk, server_sk);

        client_rx = (unsigned char *) sodium_malloc(crypto_kx_SESSIONKEYBYTES);
        client_tx = (unsigned char *) sodium_malloc(crypto_kx_SESSIONKEYBYTES);

        TEST_ASSERT(crypto_kx_client_session_keys(client_rx, client_tx,
                                             client_pk, client_sk,
                                             small_order_p) == -1);
        if (crypto_kx_client_session_keys(client_rx, client_tx,
                                          client_pk, client_sk, server_pk) != 0) {
            DebugPrint("crypto_kx_client_session_keys() failed\n");
        }

        server_rx = (unsigned char *) sodium_malloc(crypto_kx_SESSIONKEYBYTES);
        server_tx = (unsigned char *) sodium_malloc(crypto_kx_SESSIONKEYBYTES);

        TEST_ASSERT(crypto_kx_server_session_keys(server_rx, server_tx,
                                             server_pk, server_sk,
                                             small_order_p) == -1);
        if (crypto_kx_server_session_keys(server_rx, server_tx,
                                          server_pk, server_sk, client_pk) != 0) {
            DebugPrint("crypto_kx_server_session_keys() failed\n");
        }
        if (memcmp(server_rx, client_tx, crypto_kx_SESSIONKEYBYTES) != 0 ||
            memcmp(server_tx, client_rx, crypto_kx_SESSIONKEYBYTES) != 0) {
            DebugPrint("client session keys != server session keys\n");
        }

        sodium_increment(client_pk, crypto_kx_PUBLICKEYBYTES);
        if (crypto_kx_server_session_keys(server_rx, server_tx,
                                          server_pk, server_sk, client_pk) != 0) {
            DebugPrint("crypto_kx_server_session_keys() failed\n");
        }
        if (memcmp(server_rx, client_tx, crypto_kx_SESSIONKEYBYTES) == 0 &&
            memcmp(server_tx, client_rx, crypto_kx_SESSIONKEYBYTES) == 0) {
            DebugPrint("peer's public key is ignored\n");
        }

        crypto_kx_keypair(client_pk, client_sk);
        if (crypto_kx_server_session_keys(server_rx, server_tx,
                                          server_pk, server_sk, client_pk) != 0) {
            DebugPrint("crypto_kx_server_session_keys() failed\n");
        }
        if (memcmp(server_rx, client_tx, crypto_kx_SESSIONKEYBYTES) == 0 ||
            memcmp(server_tx, client_rx, crypto_kx_SESSIONKEYBYTES) == 0) {
            DebugPrint("session keys are constant\n");
        }

        crypto_kx_seed_keypair(client_pk, client_sk, seed);
        sodium_increment(seed, crypto_kx_SEEDBYTES);
        crypto_kx_seed_keypair(server_pk, server_sk, seed);
        if (crypto_kx_server_session_keys(server_rx, server_tx,
                                          server_pk, server_sk, client_pk) != 0) {
            DebugPrint("crypto_kx_server_session_keys() failed\n");
        }
        sodium_bin2hex(hex, sizeof hex, server_rx, crypto_kx_SESSIONKEYBYTES);
        DebugPrint("server_rx: [%s]\n", hex);
        sodium_bin2hex(hex, sizeof hex, server_tx, crypto_kx_SESSIONKEYBYTES);
        DebugPrint("server_tx: [%s]\n", hex);

        if (crypto_kx_client_session_keys(client_rx, client_tx,
                                          client_pk, client_sk, server_pk) != 0) {
            DebugPrint("crypto_kx_client_session_keys() failed\n");
        }
        sodium_bin2hex(hex, sizeof hex, client_rx, crypto_kx_SESSIONKEYBYTES);
        DebugPrint("client_rx: [%s]\n", hex);
        sodium_bin2hex(hex, sizeof hex, client_tx, crypto_kx_SESSIONKEYBYTES);
        DebugPrint("client_tx: [%s]\n", hex);

        randombytes_buf(client_rx, crypto_kx_SESSIONKEYBYTES);
        randombytes_buf(client_tx, crypto_kx_SESSIONKEYBYTES);
        randombytes_buf(server_rx, crypto_kx_SESSIONKEYBYTES);
        randombytes_buf(server_tx, crypto_kx_SESSIONKEYBYTES);
        if (crypto_kx_client_session_keys(client_rx, NULL,
                                          client_pk, client_sk, server_pk) != 0 ||
            crypto_kx_client_session_keys(NULL, client_tx,
                                          client_pk, client_sk, server_pk) != 0 ||
            crypto_kx_server_session_keys(server_rx, NULL,
                                          server_pk, server_sk, client_pk) != 0 ||
            crypto_kx_server_session_keys(NULL, server_tx,
                                          server_pk, server_sk, client_pk) != 0) {
            DebugPrint("failure when one of the pointers happens to be NULL");
        }
        TEST_ASSERT(memcmp(client_rx, client_tx, crypto_kx_SESSIONKEYBYTES) == 0);
        TEST_ASSERT(memcmp(client_tx, server_rx, crypto_kx_SESSIONKEYBYTES) == 0);
        TEST_ASSERT(memcmp(server_rx, server_tx, crypto_kx_SESSIONKEYBYTES) == 0);

        sodium_free(client_rx);
        sodium_free(client_tx);
        sodium_free(server_rx);
        sodium_free(server_tx);
        sodium_free(server_sk);
        sodium_free(server_pk);
        sodium_free(client_sk);
        sodium_free(client_pk);
        sodium_free(seed);

        TEST_ASSERT(strcmp(crypto_kx_primitive(), crypto_kx_PRIMITIVE) == 0);
        TEST_ASSERT(crypto_kx_publickeybytes() == crypto_kx_PUBLICKEYBYTES);
        TEST_ASSERT(crypto_kx_secretkeybytes() == crypto_kx_SECRETKEYBYTES);
        TEST_ASSERT(crypto_kx_seedbytes() == crypto_kx_SEEDBYTES);
        TEST_ASSERT(crypto_kx_sessionkeybytes() == crypto_kx_SESSIONKEYBYTES);

        DebugPrint("tv_kx: ok\n");
    }

    int
    test(void)
    {
        tv_kx();

        return 0;
    }

    void _libsodium_ut_kx()
    {
        test();
    }
} // namespace
