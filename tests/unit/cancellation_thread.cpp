// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <apta/apta.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                   \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",             \
                         __FILE__, __LINE__, #condition);                     \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main()
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
