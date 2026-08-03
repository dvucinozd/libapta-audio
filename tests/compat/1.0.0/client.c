// SPDX-License-Identifier: Apache-2.0
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) && defined(APTA_SHARED)
#include <windows.h>
#pragma comment(lib, "delayimp.lib")
#pragma comment(linker, "/delayload:apta.dll")
#endif

#include <apta/apta.h>

#define REQUIRE(condition) do { if (!(condition)) return __LINE__; } while (0)

static void stage(const char *name)
{
    (void)fprintf(stderr, "APTA_ABI_STAGE %s\n", name);
    (void)fflush(stderr);
}

static int configure_shared_library_search_path(void)
{
#if defined(_WIN32) && defined(APTA_SHARED)
    static const char release_suffix[] = "\\Release";
    char module_path[MAX_PATH];
    char *separator;
    DWORD length;
    size_t base_length;

    /*
     * A Visual Studio multi-config build places this executable in
     * tests/Release and apta.dll in the sibling Release directory. Delay-load
     * lets the test establish that directory before the first imported APTA
     * function is resolved. Static builds and non-Windows platforms do not
     * enter this path.
     */
    length = GetModuleFileNameA(NULL, module_path, (DWORD)sizeof(module_path));
    if (length == 0u || length >= (DWORD)sizeof(module_path)) {
        return 0;
    }

    separator = strrchr(module_path, '\\');
    if (separator == NULL) {
        return 0;
    }
    *separator = '\0'; /* tests/Release */

    separator = strrchr(module_path, '\\');
    if (separator == NULL) {
        return 0;
    }
    *separator = '\0'; /* tests */

    separator = strrchr(module_path, '\\');
    if (separator == NULL) {
        return 0;
    }
    *separator = '\0'; /* build-windows-shared */

    base_length = strlen(module_path);
    if (base_length + sizeof(release_suffix) > sizeof(module_path)) {
        return 0;
    }
    memcpy(
        module_path + base_length,
        release_suffix,
        sizeof(release_suffix));
    return SetDllDirectoryA(module_path) != 0;
#else
    return 1;
#endif
}

int main(void)
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = NULL;
    apta_session_t *session = NULL;
    const apta_result_t *result = NULL;
    apta_result_info_t result_info;
    apta_source_info_t source_info;

    stage("00 loader_path begin");
    REQUIRE(configure_shared_library_search_path());
    stage("00 loader_path end");

    stage("01 context_config_init begin");
    apta_context_config_init(&context_config);
    stage("01 context_config_init end");
    context_config.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 123u);

    stage("02 context_create begin");
    REQUIRE(apta_context_create(&context_config, &context) == APTA_STATUS_OK);
    stage("02 context_create end");

    stage("03 session_config_init begin");
    apta_session_config_init(&session_config);
    stage("03 session_config_init end");
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.source_fingerprint_kind =
        APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256;
    session_config.source_fingerprint[0] = 0x10u;
    session_config.source_fingerprint[31] = 0x01u;

    stage("04 session_create begin");
    REQUIRE(apta_session_create(context, &session_config, &session) ==
            APTA_STATUS_OK);
    stage("04 session_create end");

    stage("05 acquire_result begin");
    result = apta_session_acquire_result(session);
    REQUIRE(result != NULL);
    stage("05 acquire_result end");

    stage("06 result_info_init begin");
    apta_result_info_init(&result_info);
    stage("06 result_info_init end");
    result_info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 999u);

    stage("07 result_get_info begin");
    REQUIRE(apta_result_get_info(result, &result_info) == APTA_STATUS_OK);
    stage("07 result_get_info end");
    REQUIRE(result_info.producer_api_version == APTA_API_VERSION);

    stage("08 source_info_init begin");
    apta_source_info_init(&source_info);
    stage("08 source_info_init end");
    source_info.api_version = APTA_API_VERSION_ENCODE(1u, 0u, 7u);

    stage("09 result_get_source_info begin");
    REQUIRE(apta_result_get_source_info(result, &source_info) == APTA_STATUS_OK);
    stage("09 result_get_source_info end");
    REQUIRE(source_info.sample_rate == 48000u);
    REQUIRE(source_info.channel_count == 1u);
    REQUIRE(source_info.fingerprint_kind ==
            APTA_SOURCE_FINGERPRINT_APPLICATION_OPAQUE_256);
    REQUIRE(source_info.fingerprint[0] == 0x10u);
    REQUIRE(source_info.fingerprint[31] == 0x01u);

    stage("10 result_release begin");
    apta_result_release(result);
    stage("10 result_release end");

    stage("11 session_destroy begin");
    REQUIRE(apta_session_destroy(session) == APTA_STATUS_OK);
    stage("11 session_destroy end");

    stage("12 context_destroy begin");
    REQUIRE(apta_context_destroy(context) == APTA_STATUS_OK);
    stage("12 context_destroy end");
    return 0;
}
