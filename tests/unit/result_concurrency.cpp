// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <cstdint>
#include <cstdio>
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
        apta_result_info_init(&info);
        if (apta_result_get_info(result, &info) != APTA_STATUS_OK ||
            info.generation == 0u ||
            info.generation < previous_generation) {
            failures->fetch_add(1u, std::memory_order_relaxed);
        } else {
            previous_generation = info.generation;
        }

        apta_result_release(result);
        std::this_thread::yield();
    }
}

int main()
{
    constexpr uint32_t kBlockFrames = 4096u;
    constexpr uint32_t kBlockCount = 16u;
    constexpr uint32_t kTotalFrames = kBlockFrames * kBlockCount;
    constexpr uint32_t kReaderCount = 4u;

    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = nullptr;
    apta_session_t *session = nullptr;
    std::vector<int16_t> pcm(kBlockFrames);
    std::atomic<bool> stop(false);
    std::atomic<uint32_t> failures(0u);
    std::vector<std::thread> readers;

    for (uint32_t index = 0u; index < kBlockFrames; ++index) {
        pcm[index] = (index & 1u) == 0u ? INT16_MIN : INT16_MAX;
    }

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.total_frames = kTotalFrames;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    for (uint32_t index = 0u; index < kReaderCount; ++index) {
        readers.emplace_back(reader_loop, session, &stop, &failures);
    }

    for (uint32_t block_index = 0u; block_index < kBlockCount; ++block_index) {
        apta_pcm_block_t block;
        apta_work_budget_t budget;
        uint32_t accepted = 0u;
        apta_status_t status;

        apta_pcm_block_init(&block);
        block.data = pcm.data();
        block.first_frame = static_cast<apta_source_frame_t>(block_index) *
                            kBlockFrames;
        block.frame_count = kBlockFrames;

        status = apta_session_push_pcm(session, &block, &accepted);
        CHECK(status == APTA_STATUS_OK);
        CHECK(accepted == kBlockFrames);

        apta_work_budget_init(&budget);
        budget.maximum_input_frames = kBlockFrames;
        budget.maximum_steps = 16u;

        status = apta_session_process(session, &budget, nullptr);
        CHECK(status == APTA_STATUS_OK);
    }

    CHECK(apta_session_signal_end_of_input(session, kTotalFrames) ==
          APTA_STATUS_OK);

    {
        apta_work_budget_t budget;
        apta_work_budget_init(&budget);
        budget.maximum_input_frames = kBlockFrames;
        budget.maximum_steps = 16u;
        CHECK(apta_session_process(session, &budget, nullptr) ==
              APTA_STATUS_END_OF_INPUT);
    }

    stop.store(true, std::memory_order_release);
    for (std::thread &reader : readers) {
        reader.join();
    }

    CHECK(failures.load(std::memory_order_acquire) == 0u);
    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}
