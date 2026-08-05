// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",             \
                         __FILE__, __LINE__, #condition);                     \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int test_cancellation_from_another_thread()
{
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_context_t *context = nullptr;
    apta_session_t *session = nullptr;
    std::atomic<bool> start(false);
    std::atomic<apta_status_t> worker_status(APTA_STATUS_OK);

    apta_context_config_init(&context_config);
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.requested_features = 0u;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    std::thread worker([&]() {
        apta_work_budget_t budget;
        apta_work_budget_init(&budget);
        budget.maximum_steps = 1u;

        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        worker_status.store(
            apta_session_process(session, &budget, nullptr),
            std::memory_order_release);
    });

    apta_session_request_cancel(session);
    start.store(true, std::memory_order_release);
    worker.join();

    CHECK(apta_session_is_cancel_requested(session) == 1u);
    CHECK(worker_status.load(std::memory_order_acquire) == APTA_ERROR_CANCELLED);
    CHECK(apta_session_get_state(session) == APTA_SESSION_CANCELLED);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

struct pull_concurrency_state_t {
    int16_t samples[512];
    std::atomic<unsigned int> read_calls;
    std::atomic<unsigned int> active_callbacks;
    std::atomic<unsigned int> maximum_active_callbacks;
    std::atomic<bool> allow_callback_return;
};

static uint64_t APTA_CALL pull_get_total_frames(void *user_data)
{
    (void)user_data;
    return 512u;
}

static apta_status_t APTA_CALL pull_read_frames(
    void *user_data,
    apta_source_frame_t first_frame,
    uint32_t requested_frames,
    apta_pcm_block_t *block_out)
{
    auto *state = static_cast<pull_concurrency_state_t *>(user_data);
    const unsigned int active =
        state->active_callbacks.fetch_add(1u, std::memory_order_acq_rel) + 1u;
    unsigned int observed = state->maximum_active_callbacks.load(
        std::memory_order_acquire);

    while (observed < active &&
           !state->maximum_active_callbacks.compare_exchange_weak(
               observed,
               active,
               std::memory_order_acq_rel,
               std::memory_order_acquire)) {
    }
    state->read_calls.fetch_add(1u, std::memory_order_release);

    while (!state->allow_callback_return.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    if (first_frame >= 512u) {
        state->active_callbacks.fetch_sub(1u, std::memory_order_acq_rel);
        return APTA_STATUS_END_OF_INPUT;
    }

    block_out->data = &state->samples[first_frame];
    block_out->first_frame = first_frame;
    block_out->frame_count = std::min<uint32_t>(
        requested_frames,
        static_cast<uint32_t>(512u - first_frame));
    state->active_callbacks.fetch_sub(1u, std::memory_order_acq_rel);
    return APTA_STATUS_OK;
}

static void APTA_CALL pull_release_frames(
    void *user_data,
    apta_pcm_block_t *block)
{
    (void)user_data;
    if (block != nullptr) {
        block->data = nullptr;
    }
}

static int test_pull_callback_is_serialized_by_process_guard()
{
    pull_concurrency_state_t state = {};
    apta_context_config_t context_config;
    apta_session_config_t session_config;
    apta_pcm_source_t source;
    apta_work_budget_t budget;
    apta_context_t *context = nullptr;
    apta_session_t *session = nullptr;
    std::atomic<bool> start(false);
    std::atomic<bool> second_started(false);
    std::atomic<bool> second_done(false);
    std::atomic<apta_status_t> first_status(APTA_ERROR_INTERNAL);
    std::atomic<apta_status_t> second_status(APTA_ERROR_INTERNAL);

    state.read_calls.store(0u, std::memory_order_relaxed);
    state.active_callbacks.store(0u, std::memory_order_relaxed);
    state.maximum_active_callbacks.store(0u, std::memory_order_relaxed);
    state.allow_callback_return.store(false, std::memory_order_relaxed);

    apta_context_config_init(&context_config);
    context_config.requested_capabilities = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_context_create(&context_config, &context) == APTA_STATUS_OK);

    apta_session_config_init(&session_config);
    session_config.input_mode = APTA_INPUT_MODE_PULL;
    session_config.source_sample_rate = 48000u;
    session_config.channel_count = 1u;
    session_config.sample_format = APTA_SAMPLE_S16_NATIVE_INTERLEAVED;
    session_config.channel_layout = APTA_CHANNEL_LAYOUT_MONO;
    session_config.requested_features = APTA_FEATURE_WAVEFORM_OVERVIEW;
    CHECK(apta_session_create(context, &session_config, &session) == APTA_STATUS_OK);

    apta_pcm_source_init(&source);
    source.user_data = &state;
    source.read_frames = pull_read_frames;
    source.release_frames = pull_release_frames;
    source.get_total_frames = pull_get_total_frames;
    CHECK(apta_session_set_source(session, &source) == APTA_STATUS_OK);

    apta_work_budget_init(&budget);
    budget.maximum_input_frames = 256u;
    budget.maximum_steps = 1u;

    std::thread first([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        first_status.store(
            apta_session_process(session, &budget, nullptr),
            std::memory_order_release);
    });

    start.store(true, std::memory_order_release);
    while (state.read_calls.load(std::memory_order_acquire) == 0u) {
        std::this_thread::yield();
    }

    std::thread second([&]() {
        second_started.store(true, std::memory_order_release);
        second_status.store(
            apta_session_process(session, &budget, nullptr),
            std::memory_order_release);
        second_done.store(true, std::memory_order_release);
    });

    while (!second_started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    for (unsigned int spin = 0u;
         spin < 1000000u &&
         !second_done.load(std::memory_order_acquire) &&
         state.read_calls.load(std::memory_order_acquire) < 2u;
         ++spin) {
        std::this_thread::yield();
    }

    state.allow_callback_return.store(true, std::memory_order_release);
    first.join();
    second.join();

    CHECK(first_status.load(std::memory_order_acquire) >= 0);
    CHECK(second_status.load(std::memory_order_acquire) == APTA_ERROR_BUSY);
    CHECK(state.read_calls.load(std::memory_order_acquire) == 1u);
    CHECK(state.maximum_active_callbacks.load(std::memory_order_acquire) == 1u);

    CHECK(apta_session_destroy(session) == APTA_STATUS_OK);
    CHECK(apta_context_destroy(context) == APTA_STATUS_OK);
    return 0;
}

int main()
{
    CHECK(test_cancellation_from_another_thread() == 0);
    CHECK(test_pull_callback_is_serialized_by_process_guard() == 0);
    return 0;
}
