#include "libsodium_ut_sodium_utils2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace sodium_utils2
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
        void         *buf;
        size_t        size;
        unsigned int  i;

    #ifdef BENCHMARKS
        return 0;
    #endif

        if (sodium_malloc(SIZE_MAX - 1U) != NULL) {
            return 1;
        }
        if (sodium_malloc(0U) == NULL) {
            return 1;
        }
        if (sodium_allocarray(SIZE_MAX / 2U + 1U, SIZE_MAX / 2U) != NULL) {
            return 1;
        }
        sodium_free(sodium_allocarray(0U, 0U));
        sodium_free(sodium_allocarray(0U, 1U));
        sodium_free(sodium_allocarray(1U, 0U));

        buf = sodium_allocarray(1000U, 50U);
        memset(buf, 0, 50000U);
        sodium_free(buf);

        sodium_free(sodium_malloc(0U));
        sodium_free(NULL);
        for (i = 0U; i < 10000U; i++) {
            size = 1U + randombytes_uniform(100000U);
            buf  = sodium_malloc(size);
            TEST_ASSERT(buf != NULL);
            memset(buf, i, size);
            sodium_mprotect_noaccess(buf);
            sodium_free(buf);
        }
        DebugPrint("OK\n");
    #ifdef SIG_DFL
    # ifdef SIGPROT
        signal(SIGPROT, segv_handler);
    # endif
    # ifdef SIGSEGV
        signal(SIGSEGV, segv_handler);
    # endif
    # ifdef SIGBUS
        signal(SIGBUS, segv_handler);
    # endif
    # ifdef SIGABRT
        signal(SIGABRT, segv_handler);
    # endif
    #endif
        size = 1U + randombytes_uniform(100000U);
        buf  = sodium_malloc(size);
        TEST_ASSERT(buf != NULL);

    /* old versions of asan emit a warning because they don't support mlock*() */
    #ifndef __SANITIZE_ADDRESS__
        sodium_mprotect_readonly(buf);
        sodium_mprotect_readwrite(buf);
    #endif

    #if defined(HAVE_CATCHABLE_SEGV) && !defined(__EMSCRIPTEN__) && !defined(__SANITIZE_ADDRESS__)
        sodium_memzero(((unsigned char *) buf) + size, 1U);
        sodium_mprotect_noaccess(buf);
        sodium_free(buf);
        DebugPrint("Overflow not caught\n");
    #else
        segv_handler(0);
    #endif
        return 0;
    }

    void _libsodium_ut_sodium_utils2()
    {
        test();
    }
} // namespace
