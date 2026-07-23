#include "libsodium_ut_misuse.h"

#include "unity.h"
//#include "aether/tele.h"

namespace misuse
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

    #ifdef HAVE_CATCHABLE_ABRT
    # include <signal.h>
    #ifndef _WIN32
    # include <unistd.h>
    #endif

    static void
    sigabrt_handler_15(int sig)
    {
        (void) sig;
        _exit(0);
    }

    # ifndef SODIUM_LIBRARY_MINIMAL
    static void
    sigabrt_handler_14(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_15);
        TEST_ASSERT(crypto_box_curve25519xchacha20poly1305_easy
               (guard_page, guard_page, crypto_stream_xchacha20_MESSAGEBYTES_MAX - 1,
                guard_page, guard_page, guard_page) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_13(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_14);
        TEST_ASSERT(crypto_box_curve25519xchacha20poly1305_easy_afternm
               (guard_page, guard_page, crypto_stream_xchacha20_MESSAGEBYTES_MAX - 1,
                guard_page, guard_page) == -1);
        _exit(1);
    }
    # endif

    static void
    sigabrt_handler_12(int sig)
    {
        (void) sig;
    # ifdef SODIUM_LIBRARY_MINIMAL
        signal(SIGABRT, sigabrt_handler_15);
    # else
        signal(SIGABRT, sigabrt_handler_13);
    # endif
        TEST_ASSERT(crypto_pwhash_str_alg((char *) guard_page,
                                     "", 0U, 1U, 1U, -1) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_11(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_12);
        TEST_ASSERT(crypto_box_easy(guard_page, guard_page,
                               crypto_stream_xsalsa20_MESSAGEBYTES_MAX,
                               guard_page, guard_page, guard_page) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_10(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_11);
        TEST_ASSERT(crypto_box_easy_afternm(guard_page, guard_page,
                                       crypto_stream_xsalsa20_MESSAGEBYTES_MAX,
                                       guard_page, guard_page) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_9(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_10);
        TEST_ASSERT(sodium_base642bin(guard_page, 1, (const char *) guard_page, 1,
                                 NULL, NULL, NULL, -1) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_8(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_9);
        TEST_ASSERT(sodium_bin2base64((char *) guard_page, 1, guard_page, 1,
                                 sodium_base64_VARIANT_ORIGINAL) == NULL);
        _exit(1);
    }

    static void
    sigabrt_handler_7(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_8);
        TEST_ASSERT(sodium_bin2base64((char *) guard_page, 1,
                                 guard_page, 1, -1) == NULL);
        _exit(1);
    }

    static void
    sigabrt_handler_6(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_7);
        TEST_ASSERT(sodium_pad(NULL, guard_page, SIZE_MAX, 16, 1) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_5(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_6);
        TEST_ASSERT(crypto_aead_xchacha20poly1305_ietf_encrypt(guard_page, NULL, NULL, UINT64_MAX,
                                                          NULL, 0, NULL,
                                                          guard_page, guard_page) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_4(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_5);
        TEST_ASSERT(crypto_aead_chacha20poly1305_ietf_encrypt(guard_page, NULL, NULL, UINT64_MAX,
                                                         NULL, 0, NULL,
                                                         guard_page, guard_page) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_3(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_4);
        TEST_ASSERT(crypto_aead_chacha20poly1305_encrypt(guard_page, NULL, NULL, UINT64_MAX,
                                                    NULL, 0, NULL,
                                                    guard_page, guard_page) == -1);
        _exit(1);
    }

    static void
    sigabrt_handler_2(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_3);
    #if SIZE_MAX > 0x4000000000ULL
        randombytes_buf_deterministic(guard_page, 0x4000000001ULL, guard_page);
    #else
        abort();
    #endif
        _exit(1);
    }

    static void
    sigabrt_handler_1(int sig)
    {
        (void) sig;
        signal(SIGABRT, sigabrt_handler_2);
        TEST_ASSERT(crypto_kx_server_session_keys(NULL, NULL, guard_page, guard_page,
                                             guard_page) == -1);
        _exit(1);
    }

    int
    test(void)
    {
        signal(SIGABRT, sigabrt_handler_1);
        TEST_ASSERT(crypto_kx_client_session_keys(NULL, NULL, guard_page, guard_page,
                                             guard_page) == -1);
        return 1;
    }
    #else
    int
    test(void)
    {
        return 0;
    }
    #endif

    void _libsodium_ut_misuse()
    {
        test();
    }
} // namespace
