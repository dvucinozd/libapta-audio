// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",             \
                         __FILE__, __LINE__, #condition);                     \
            return 1;                                                        \
        }                                                                    \
    } while (0)

struct allocator_state_t {
    uint32_t allocate_calls;
    uint32_t outstanding;
};

union aligned_workspace_t {
    std::max_align_t alignment;
    uint8_t bytes[65536];
};

static void *APTA_CALL test_allocate(
    void *user_data,
    size_t size,
    size_t alignment,
    apta_memory_flags_t flags)
{
    auto *state = static_cast<allocator_state_t *>(user_data);
    (void)alignment;
    (void)flags;

    state->allocate_calls += 1u;
    void *memory = std::malloc(size);
    if (memory != nullptr) {
        state->outstanding += 1u;
    }
    return memory;
}

static void APTA_CALL test_deallocate(void *user_data, void *memory)
{
    auto *state = static_cast<allocator_state_t *>(user_data);
    if (memory != nullptr) {
        std::free(memory);
        state->outstanding -= 1u;
    }
}

static void reader_loop(
    const apta_session_t *session,
    std::atomic<bool> *stop,
    std::atomic<uint32_t> *failures)
{
    apta_generation_t previous_generation = 0u;

    while (!stop->load(std::memory_order_acquire)) {
        const apta_result_t *result = apta_session_acquire_result(session);
        if (result == nullptr) {
            failures->fetch_add(1u, std::memory_order_relaxed);
            continue;
        }

        apta_result_info_t info;
        apta_metadata_view_t metadata;
        apta_result_info_init(&info);
        apta_metadata_view_init(&metadata);

        if (apta_result_get_info(result, &info) != APTA_STATUS_OK ||
            info.generation == 0u ||
            info.generation < previous_generation) {
            failures->fetch_add(1u, std::memory_order_relaxed);
        } else {
            previous_generation = info.generation;
        }

        apta_status_t metadata_status =
            apta_result_get_metadata(result, &metadata);
        if (metadata_status != APTA_STATUS_OK &&
            metadata_status != APTA_STATUS_NOT_AVAILABLE) {
            failures->fetch_add(1u, std::memory_order_relaxed);
        }
        if (metadata_status == APTA_STATUS_OK &&
            (metadata.producer_name.size != 1u ||
             metadata.producer_name.data == nullptr)) {
            failures->fetch_add(1u, std::memory_order_relaxed);
        }

        std::this_thread::yield();
        apta_result_release(result);
    }
}

int main()
{
    constexpr uint32_t kReaderCount = 4u;
    constexpr uint32_t kPublicationCount = 200u;

    allocator_state_t allocator_state{0u, 0u};
    aligned_workspace_t workspace{};
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_memory_requirements_t requirements;
    apta_context_t *context = nullptr;
    apta_session_t *session = nullptr;
    const apta_result_t *held_initial = nullptr;
    const apta_result_t *final_result = nullptr;
    std::atomic<bool> stop(false);
    std::atomic<uint32_t> failures(0u);
    std::atomic<uint32_t> exhaustion_count(0u);
    std::vector<std::thread> readers;
    char producer = 'A';

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = 1024u;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    session_config.flags = APTA_SESSION_FLAG_BOUNDED_RESULT_SLOTS;

    apta_memory_requirements_init(&requirements);
    CHECK(apta_query_memory_requirements(
              &session_config,
              &requirements) == APTA_STATUS_OK);

    apta_context_config_init(&context_config);
    context_config.allocator.user_data = &allocator_state;
    context_config.allocator.allocate = test_allocate;
    context_config.allocator.deallocate = test_deallocate;
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    context_config.memory_limit_bytes = requirements.minimum_bytes;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    session_config.static_workspace = workspace.bytes;
    session_config.static_workspace_size = sizeof(workspace.bytes);
    CHECK(apta_session_create(context, &session_config, &session) ==
          APTA_STATUS_OK);
    CHECK(allocator_state.allocate_calls == 2u);

    held_initial = apta_session_acquire_result(session);
    CHECK(held_initial != nullptr);

    apta_metadata_t metadata;
    apta_metadata_init(&metadata);
    metadata.producer_name.data = &producer;
    metadata.producer_name.size = 1u;
    metadata.flags = APTA_METADATA_FLAG_PRODUCER_NAME_PRESENT;
    CHECK(apta_session_set_metadata(session, &metadata) == APTA_STATUS_OK);

    producer = 'B';
    CHECK(apta_session_set_metadata(session, &metadata) ==
          APTA_ERROR_RESULT_SLOTS_EXHAUSTED);
    exhaustion_count.fetch_add(1u, std::memory_order_relaxed);
    apta_result_release(held_initial);
    held_initial = nullptr;

    for (uint32_t index = 0u; index < kReaderCount; ++index) {
        readers.emplace_back(reader_loop, session, &stop, &failures);
    }

    for (uint32_t publication = 0u;
         publication < kPublicationCount;
         ++publication) {
        apta_status_t status;
        uint32_t attempts = 0u;

        producer = static_cast<char>('A' + (publication % 26u));
        do {
            status = apta_session_set_metadata(session, &metadata);
            if (status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED) {
                exhaustion_count.fetch_add(1u, std::memory_order_relaxed);
                std::this_thread::yield();
            }
            attempts += 1u;
        } while (status == APTA_ERROR_RESULT_SLOTS_EXHAUSTED &&
                 attempts < 1000000u);

        CHECK(status == APTA_STATUS_OK);
    }

    stop.store(true, std::memory_order_release);
    for (std::thread &reader : readers) {
        reader.join();
    }

    CHECK(failures.load(std::memory_order_acquire) == 0u);
    CHECK(exhaustion_count.load(std::memory_order_acquire) != 0u);
    CHECK(allocator_state.allocate_calls == 2u);

    final_result = apta_session_acquire_result(session);
    CHECK(final_result != nullptr);
    CHECK(apta_result_get_generation(final_result) ==
          static_cast<apta_generation_t>(2u + kPublicationCount));

    apta_metadata_view_t final_metadata;
    apta_metadata_view_init(&final_metadata);
    CHECK(apta_result_get_metadata(final_result, &final_metadata) ==
          APTA_STATUS_OK);
    CHECK(final_metadata.producer_name.size == 1u);
    CHECK(final_metadata.producer_name.data[0] == producer);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    session = nullptr;
    std::memset(workspace.bytes, 0xA5, sizeof(workspace.bytes));
    CHECK(apta_result_get_generation(final_result) ==
          static_cast<apta_generation_t>(2u + kPublicationCount));
    CHECK(apta_context_destroy(context) == APTA_ERROR_BUSY);

    apta_result_release(final_result);
    final_result = nullptr;
    CHECK(allocator_state.outstanding == 1u);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    CHECK(allocator_state.outstanding == 0u);
    return 0;
}
