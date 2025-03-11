///
///\file main.cpp
///\brief AetherNet library example
/// Unit tests for the bcrypt library
///
/// This example code is in the Public Domain (or CC0 licensed, at your option.)
///
/// Unless required by applicable law or agreed to in writing, this
/// software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
/// CONDITIONS OF ANY KIND, either express or implied.
///
///\author Dmitriy Kiryanov dmitriyk@aethernet.io
///\version 1.0.0
///\date  10.04.2024
///

#include "libbcrypt_unit_tests.h"

#include <time.h>
#include <string.h>

#include "third_party/libbcrypt/bcrypt.h"

#include "unity.h"

#define DEBUG_OUT 1
#define LOG(TAG, ...) {\
 fprintf(stderr, "file location%s\nfunction name:%s\nline:%d\n", __FILE__, __FUNCTION__, __LINE__);\
 fprintf(stderr, "date:%s\ntime:%s\n",__DATE__, __TIME__);\
 fprintf(stderr, "%s", TAG);\
 fprintf(stderr, "\r\n");\
 fprintf(stderr, __VA_ARGS__);\
 fprintf(stderr, "\n");\
}

static const char *TAG = "BCRYPT UNITY";

#if DEBUG_OUT==1
    #define DebugPrint(format, ...) LOG(TAG, format, ##__VA_ARGS__)
#elif DEBUG_OUT==2
    #define DebugPrint(format, ...) AE_TELED_DEBUG(TAG, format, ##__VA_ARGS__)
#elif DEBUG_OUT==3
    #define DebugPrint(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
#endif

///
///\brief _libbcrypt_unit_tests_test1 Unit test for libbcrypt.
///\param[in] void
///\return void
///
void _libbcrypt_unit_tests_test_1()
{
    clock_t before;
	clock_t after;
	char salt[BCRYPT_HASHSIZE];
	char hash[BCRYPT_HASHSIZE];
	int ret;

    const char pass[] = "hi,mom";
	const char hash1[] = "$2a$10$VEVmGHy4F4XQMJ3eOZJAUeb.MedU0W10pTPCuf53eHdKJPiSE8sMK";
	const char hash2[] = "$2a$10$3F0BVk5t8/aoS.3ddaB3l.fxg5qvafQ9NybxcpXLzMeAt.nVWn.NO";

    ret = bcrypt_gensalt(12, salt);
	TEST_ASSERT(ret == 0);
    DebugPrint("Generated salt: %s", salt);
	before = clock();
	ret = bcrypt_hashpw("testtesttest", salt, hash);
	TEST_ASSERT(ret == 0);
	after = clock();
    DebugPrint("Hashed password: %s", hash);
    DebugPrint("Time taken: %lf seconds", (double)(after - before) / CLOCKS_PER_SEC);

    ret = bcrypt_hashpw(pass, hash1, hash);
	TEST_ASSERT(ret == 0);
    DebugPrint("Time taken: %s seconds", (strcmp(hash1, hash) == 0)?"OK":"FAIL");
	ret = bcrypt_hashpw(pass, hash2, hash);
	TEST_ASSERT(ret == 0);
    DebugPrint("Second hash check: %s", (strcmp(hash2, hash) == 0)?"OK":"FAIL");

	before = clock();
	ret = (bcrypt_checkpw(pass, hash1) == 0);
    TEST_ASSERT(ret == 1);
	after = clock();
    DebugPrint("First hash check with bcrypt_checkpw: %s", ret?"OK":"FAIL");
    DebugPrint("Time taken: %lf seconds", (double)(after - before) / CLOCKS_PER_SEC);

	before = clock();
	ret = (bcrypt_checkpw(pass, hash2) == 0);
    TEST_ASSERT(ret == 1);
	after = clock();
    DebugPrint("Second hash check with bcrypt_checkpw: %s", ret?"OK":"FAIL");
    DebugPrint("Time taken: %lf seconds", (double)(after - before) / CLOCKS_PER_SEC);
}

///
///\brief libbcrypt_unit_tests_runner Unit test runner for libbcrypt.
///\param[in] void
///\return void
///
int libbcrypt_unit_tests_runner()
{
	UNITY_BEGIN();

    DebugPrint( "Starting libbcrypt unit tests.");
	RUN_TEST(_libbcrypt_unit_tests_test_1);

	return UNITY_END(); // stop unit testing
}
