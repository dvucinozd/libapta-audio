# Adaptive Progressive Track Analysis

## Kompletna implementacijska specifikacija

**Platforma:** ESP32-P4  
**Framework:** ESP-IDF 5.5.4  
**Jezik:** C11  
**Projekt:** Pajoniiir  
**Komponenta:** `apta`  
**Medijski model:** jedan USB mass-storage izvor  
**Playback model:** dva decka  
**Analysis model:** dvije logičke sesije, najviše jedan aktivni background scan  

---

# 1. Cilj implementacije

Adaptive Progressive Track Analysis, skraćeno APTA, omogućuje učitavanje i korištenje audio traka koje nemaju unaprijed pripremljene Rekordbox ANLZ podatke.

Sustav mora omogućiti:

- trenutačno pokretanje reprodukcije;
- lokalni waveform iz već dekodiranog PCM-a;
- progresivno stvaranje overview waveforma;
- brzu provisional procjenu BPM-a;
- lokalni beatgrid oko playheada;
- pozadinsko dovršavanje globalnog beatgrida;
- confidence za BPM i beatgrid;
- zaključavanje već korištenog područja grida;
- trajno cacheiranje rezultata;
- nastavak nepotpune analize nakon ponovnog učitavanja;
- potpuno neometan dual-deck playback.

APTA ne zamjenjuje postojeći Rekordbox ANLZ put. Redoslijed izvora ostaje:

```text
1. valjani Rekordbox ANLZ
2. valjani postojeći metadata cache
3. valjani APTA cache
4. progresivna APTA analiza
5. minimalni playback bez analize
```

---

# 2. Glavna arhitektonska ograničenja

## 2.1. Jedan USB medij

APTA mora pretpostavljati:

```text
jedan USB mass-storage izvor
jedan montirani USB filesystem
jedan globalni media I/O arbiter
```

S istog USB medija mogu se čitati:

- audio datoteka Decka 1;
- audio datoteka Decka 2;
- `export.pdb`;
- ANLZ `.DAT` i `.EXT` podaci;
- audio blokovi potrebni APTA analizi.

Ne postoje:

- paralelni USB storage izvori;
- zaseban APTA USB;
- zaseban USB medij po decku;
- istodobna dva background scan decodera.

## 2.2. Dvije APTA sesije, jedan scan worker

Svaki deck ima vlastito analysis stanje:

```text
Deck 1 → apta_session[0]
Deck 2 → apta_session[1]
```

Ali postoji samo:

```text
jedan apta_service task
jedan background scan decoder
jedna aktivna scan operacija
```

Scheduler bira koja se sesija trenutačno obrađuje.

## 2.3. Audio ima apsolutni prioritet

APTA ne smije uzrokovati:

- PCM underrun;
- kasni output block;
- I2S prekid;
- DSI underrun;
- blokiran LOAD;
- usporen scratch;
- nepravilan loop;
- watchdog reset;
- USB MSC timeout.

Kada audio sustav pokaže pritisak, APTA se mora odmah usporiti ili pauzirati.

---

# 3. Pregled toka podataka

```text
                    Jedan USB medij
                          │
                          ▼
                    media_io_gate
                          │
         ┌────────────────┼────────────────┐
         │                │                │
         ▼                ▼                ▼
 Deck 1 decoder     Deck 2 decoder    APTA scan decoder
         │                │                │
         ├──── PCM ───────┤                │
         │                │                │
         ▼                ▼                ▼
    PCM ring 1       PCM ring 2      Analysis PCM
         │                │                │
         └─────────► audio mixer           │
                          │                │
                          ▼                ▼
                       I2S OUT       Waveform / BPM /
                                     beatgrid / cache
```

Playback decoderi ostaju autoritativni za reprodukciju.

APTA scan decoder:

- koristi zaseban codec context;
- ne mijenja playback position;
- ne dijeli decoder state s deckom;
- radi samo u vremenski ograničenim sliceovima;
- nema pravo zadržavati USB gate tijekom DSP obrade.

---

# 4. Struktura nove komponente

```text
firmware/main-deck-p4/components/apta/
├── CMakeLists.txt
├── Kconfig
├── idf_component.yml
│
├── include/
│   ├── apta.h
│   ├── apta_types.h
│   ├── apta_snapshot.h
│   ├── apta_cache.h
│   └── apta_metrics.h
│
├── apta_service.c
├── apta_session.c
├── apta_scheduler.c
├── apta_pcm_tap.c
├── apta_scan_decoder.c
├── apta_resampler.c
├── apta_waveform.c
├── apta_filterbank.c
├── apta_onset.c
├── apta_tempo.c
├── apta_beatgrid.c
├── apta_confidence.c
├── apta_fingerprint.c
├── apta_cache.c
├── apta_snapshot.c
├── apta_metrics.c
└── apta_internal.h
```

## 4.1. Odgovornosti datoteka

### `apta_service.c`

- inicijalizacija komponente;
- stvaranje taskova;
- obrada naredbi;
- glavni background worker;
- koordinacija dviju sesija.

### `apta_session.c`

- lifecycle pojedine deck sesije;
- state machine;
- track identity;
- cancellation;
- cleanup.

### `apta_scheduler.c`

- izbor sesije za obradu;
- CPU time budget;
- USB I/O budget;
- backpressure;
- pause i resume odluke.

### `apta_pcm_tap.c`

- prima PCM koji je playback decoder već proizveo;
- stvara lokalni waveform;
- downmix i jeftine feature blokove;
- ne pristupa USB-u.

### `apta_scan_decoder.c`

- otvara zaseban decoder context;
- čita blokove s USB-a;
- sekvencijalno skenira audio;
- implementira bounded seek i read.

### `apta_waveform.c`

- overview waveform;
- detail tileovi;
- min/max/RMS;
- progresivno objavljivanje rezultata.

### `apta_filterbank.c`

- low, mid i high energije;
- biquad filteri;
- state po analysis streamu.

### `apta_onset.c`

- Hann prozor;
- FFT;
- spectral flux;
- novelty envelope;
- peak picking.

### `apta_tempo.c`

- tempo kandidati;
- autocorrelation;
- half/double-time odluka;
- provisional i finalni BPM.

### `apta_beatgrid.c`

- phase search;
- lokalni tracker;
- globalni refinement;
- tempo segmenti;
- zaključavanje grida.

### `apta_confidence.c`

- confidence BPM-a;
- confidence faze;
- confidence grida;
- odluka smije li se koristiti Sync.

### `apta_fingerprint.c`

- stabilan identitet audio datoteke;
- invalidacija cachea.

### `apta_cache.c`

- čitanje i zapis APTA cachea;
- sekcije;
- CRC;
- atomic replace;
- checkpoint.

### `apta_snapshot.c`

- immutable snapshot;
- reference counting;
- transakcijska objava.

### `apta_metrics.c`

- CPU vrijeme;
- USB statistika;
- pause razlog;
- memorija;
- cache hitovi;
- vrijeme faza.

---

# 5. Javni API

## 5.1. `apta_types.h`

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APTA_DECK_COUNT 2
#define APTA_FINGERPRINT_SIZE 32

typedef enum {
    APTA_SOURCE_NONE = 0,
    APTA_SOURCE_REKORDBOX,
    APTA_SOURCE_METADATA_CACHE,
    APTA_SOURCE_APTA_CACHE,
    APTA_SOURCE_PROGRESSIVE
} apta_source_t;

typedef enum {
    APTA_STATE_IDLE = 0,
    APTA_STATE_CACHE_LOOKUP,
    APTA_STATE_LOCAL_WAVEFORM,
    APTA_STATE_FAST_SCAN,
    APTA_STATE_PROVISIONAL_READY,
    APTA_STATE_REFINING,
    APTA_STATE_COMPLETE,
    APTA_STATE_PAUSED,
    APTA_STATE_CANCELLED,
    APTA_STATE_ERROR
} apta_state_t;

typedef enum {
    APTA_PAUSE_NONE = 0,
    APTA_PAUSE_AUDIO_RING_LOW,
    APTA_PAUSE_OUTPUT_LATE,
    APTA_PAUSE_DSI_UNDERRUN,
    APTA_PAUSE_MEDIA_BUSY,
    APTA_PAUSE_TRACK_LOAD,
    APTA_PAUSE_SEEK,
    APTA_PAUSE_SCRATCH,
    APTA_PAUSE_LOW_MEMORY,
    APTA_PAUSE_USB_REMOVED,
    APTA_PAUSE_USER
} apta_pause_reason_t;

typedef struct {
    uint8_t bytes[APTA_FINGERPRINT_SIZE];
} apta_fingerprint_t;

typedef struct {
    const char *path;
    uint8_t deck;

    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;

    uint64_t total_frames;
    uint32_t duration_ms;

    uint64_t file_size;
} apta_track_desc_t;

typedef struct {
    uint32_t bpm_x100;

    uint8_t bpm_confidence;
    uint8_t phase_confidence;
    uint8_t grid_confidence;

    bool provisional;
    bool dynamic_tempo;
} apta_tempo_info_t;
```

## 5.2. `apta.h`

```c
#pragma once

#include "apta_types.h"
#include "apta_snapshot.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t apta_init(void);
void apta_deinit(void);

esp_err_t apta_start(
    const apta_track_desc_t *track);

esp_err_t apta_cancel(
    uint8_t deck);

esp_err_t apta_pause(
    uint8_t deck,
    apta_pause_reason_t reason);

esp_err_t apta_resume(
    uint8_t deck);

void apta_notify_transport(
    uint8_t deck,
    bool playing,
    uint64_t source_frame);

void apta_notify_seek(
    uint8_t deck,
    uint64_t source_frame);

void apta_notify_loop(
    uint8_t deck,
    bool active,
    uint64_t first_frame,
    uint64_t last_frame);

void apta_notify_sync_use(
    uint8_t deck,
    uint64_t source_frame);

void apta_notify_media_removed(void);

const apta_snapshot_t *apta_snapshot_acquire(
    uint8_t deck);

void apta_snapshot_release(
    const apta_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
```

---

# 6. State machine

```text
IDLE
  │
  ▼
CACHE_LOOKUP
  │
  ├── valjani cache ───────────────► COMPLETE
  │
  └── cache miss
          │
          ▼
LOCAL_WAVEFORM
          │
          ▼
FAST_SCAN
          │
          ├── dovoljan confidence
          │          ▼
          │   PROVISIONAL_READY
          │          │
          ▼          ▼
        REFINING ───► COMPLETE
```

Iz svih aktivnih stanja dopušteni su prijelazi:

```text
ACTIVE → PAUSED
ACTIVE → CANCELLED
ACTIVE → ERROR
PAUSED → prethodno aktivno stanje
```

## 6.1. Sesija

```c
typedef struct {
    uint8_t deck;

    apta_state_t state;
    apta_state_t resume_state;
    apta_pause_reason_t pause_reason;

    apta_track_desc_t track;
    apta_fingerprint_t fingerprint;

    bool cancel_requested;
    bool playing;
    bool scan_decoder_open;

    uint64_t playhead_frame;
    uint64_t scanned_until_frame;

    uint32_t progress_permille;

    struct apta_working_set *working;
    struct apta_snapshot *published;
} apta_session_t;
```

Track path se ne smije samo spremiti kao vanjski pokazivač. Potrebno ga je duplicirati u session-owned memoriju.

---

# 7. FreeRTOS taskovi

## 7.1. Task model

APTA koristi najviše dva taska:

```text
apta_service_task
apta_cache_task
```

### `apta_service_task`

- jedan jedini analysis worker;
- obrađuje Deck 1 ili Deck 2;
- scan decode;
- waveform;
- FFT;
- BPM;
- beatgrid;
- objava snapshota.

### `apta_cache_task`

- zapis checkpointa;
- zapis finalnog cachea;
- vrlo nizak prioritet;
- ne smije blokirati analysis ili playback.

## 7.2. Predložena inicijalizacija

```c
#define APTA_SERVICE_STACK_BYTES  (24 * 1024)
#define APTA_CACHE_STACK_BYTES    (8 * 1024)

static TaskHandle_t s_service_task;
static TaskHandle_t s_cache_task;

esp_err_t apta_init(void)
{
    BaseType_t ok;

    ok = xTaskCreatePinnedToCore(
        apta_service_task,
        "apta_service",
        APTA_SERVICE_STACK_BYTES,
        NULL,
        CONFIG_APTA_TASK_PRIORITY,
        &s_service_task,
        CONFIG_APTA_TASK_CORE);

    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ok = xTaskCreatePinnedToCore(
        apta_cache_task,
        "apta_cache",
        APTA_CACHE_STACK_BYTES,
        NULL,
        CONFIG_APTA_CACHE_TASK_PRIORITY,
        &s_cache_task,
        CONFIG_APTA_TASK_CORE);

    if (ok != pdPASS) {
        vTaskDelete(s_service_task);
        s_service_task = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
```

APTA task mora imati niži prioritet od:

- audio output taska;
- audio decode taskova;
- LVGL taska;
- kritičnog control taska.

---

# 8. Scheduler

## 8.1. Runtime pressure

APTA scheduler treba čitati postojeću audio i UI telemetriju.

```c
typedef struct {
    bool deck_active[APTA_DECK_COUNT];
    bool deck_playing[APTA_DECK_COUNT];

    uint32_t pcm_ring_frames[APTA_DECK_COUNT];
    uint32_t pcm_ring_capacity[APTA_DECK_COUNT];

    uint32_t pcm_underrun_count[APTA_DECK_COUNT];

    uint32_t output_late_count;
    uint32_t dsi_underrun_count;

    bool scratch_active[APTA_DECK_COUNT];
    bool master_tempo_active[APTA_DECK_COUNT];

    bool track_load_active;
    bool seek_active;
    bool media_connected;
    bool media_busy;

    size_t free_internal_heap;
    size_t largest_internal_block;
    size_t free_psram;
} apta_runtime_pressure_t;
```

## 8.2. Hard-stop uvjeti

APTA se odmah pauzira kada:

- USB medij nije dostupan;
- raste PCM underrun counter;
- raste output-late counter;
- raste DSI underrun counter;
- aktivan je kritični LOAD;
- aktivan je seek;
- oba decka intenzivno scratchaju;
- interna memorija padne ispod sigurnog praga.

## 8.3. Soft-throttle uvjeti

Budžet se smanjuje kada:

- oba decka sviraju;
- oba decka koriste Master Tempo;
- PCM ring padne ispod 50%;
- UI radi veliki waveform refresh;
- `media_io_gate` ima dugo čekanje;
- USB read latencija raste.

## 8.4. Početni vremenski budžeti

```text
oba decka sviraju:       2 ms / 20 ms
jedan deck svira:        5 ms / 20 ms
nijedan deck ne svira:  15 ms / 20 ms
kritični LOAD ili seek:  0 ms
dual scratch:            0–1 ms / 50 ms
```

Vrijednosti moraju biti Kconfig opcije.

## 8.5. Glavna task petlja

```c
static void apta_service_task(void *arg)
{
    while (true) {
        apta_process_commands();

        apta_runtime_pressure_t pressure;
        apta_read_runtime_pressure(&pressure);

        apta_session_t *session =
            apta_scheduler_select_session(&pressure);

        if (session == NULL) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            continue;
        }

        uint32_t budget_us =
            apta_scheduler_get_budget_us(&pressure);

        if (budget_us == 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        int64_t started_us = esp_timer_get_time();

        while (!session->cancel_requested) {
            apta_step_result_t result =
                apta_process_one_step(session, &pressure);

            if (result != APTA_STEP_MORE_WORK) {
                break;
            }

            if ((uint32_t)(esp_timer_get_time() - started_us)
                    >= budget_us) {
                break;
            }

            if (apta_scheduler_should_preempt(&pressure)) {
                break;
            }
        }

        taskYIELD();
    }
}
```

---

# 9. USB I/O arbitraža

## 9.1. Jedan globalni gate

Svi USB čitači koriste postojeći globalni `media_io_gate`.

Prioriteti su:

```text
1. playback refill
2. playback seek, CUE, loop i scratch
3. LOAD i preload
4. PDB/ANLZ metadata
5. urgent APTA lokalni scan
6. APTA globalni background scan
```

APTA ne smije dugo blokirati čekajući gate.

## 9.2. Read helper

```c
static esp_err_t apta_media_read(
    int fd,
    void *buffer,
    size_t requested,
    size_t *read_out)
{
    if (buffer == NULL || read_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *read_out = 0;

    esp_err_t err = media_io_gate_lock(
        pdMS_TO_TICKS(CONFIG_APTA_IO_TIMEOUT_MS));

    if (err != ESP_OK) {
        apta_metrics_note_gate_timeout();
        return ESP_ERR_TIMEOUT;
    }

    int64_t start_us = esp_timer_get_time();
    ssize_t result = read(fd, buffer, requested);
    uint32_t elapsed_us =
        (uint32_t)(esp_timer_get_time() - start_us);

    media_io_gate_unlock();

    apta_metrics_note_read(
        result > 0 ? (size_t)result : 0,
        elapsed_us);

    if (result < 0) {
        return ESP_FAIL;
    }

    *read_out = (size_t)result;
    return ESP_OK;
}
```

DSP obrada počinje tek nakon `media_io_gate_unlock()`.

## 9.3. Veličine blokova

Početne vrijednosti:

```text
idle:                  64 KiB
jedan deck svira:      16 KiB
oba decka sviraju:      8 KiB
opterećen USB:          4 KiB
LOAD/seek:               0 KiB
```

## 9.4. Samo jedan APTA file descriptor aktivno čita

Moguće je zadržati dva session statea, ali background decoder pripada workeru.

Pri promjeni sesije:

```text
1. spremi decoder checkpoint prethodne sesije
2. zatvori prethodni decoder
3. otvori decoder odabrane sesije
4. seek na spremljenu scan poziciju
5. nastavi obradu
```

U prvoj verziji preporučljivo je ne mijenjati aktivnu sesiju češće od svakih nekoliko stotina milisekundi.

---

# 10. PCM tap

Lokalni waveform treba dobiti bez dodatnog USB čitanja.

## 10.1. Playback decoder integracija

Nakon dekodiranja PCM bloka:

```text
decoder
   ├── PCM ring
   └── apta_pcm_tap_submit()
```

APTA tap ne smije:

- alocirati memoriju za svaki blok;
- kopirati cijeli stereo PCM;
- blokirati decoder task;
- koristiti mutex koji može čekati.

## 10.2. Feature blok

```c
#define APTA_TAP_MONO_SAMPLES 256

typedef struct {
    uint8_t deck;

    uint64_t first_source_frame;
    uint32_t source_frame_count;
    uint32_t sample_rate;

    int16_t peak_min;
    int16_t peak_max;

    uint32_t rms_q16;

    uint32_t low_energy_q16;
    uint32_t mid_energy_q16;
    uint32_t high_energy_q16;

    uint16_t mono_count;
    int16_t mono[APTA_TAP_MONO_SAMPLES];
} apta_pcm_feature_block_t;
```

## 10.3. Downsample tijekom jednog prolaza

```c
void apta_pcm_tap_process_i16_stereo(
    uint8_t deck,
    const int16_t *pcm,
    size_t frame_count,
    uint64_t first_source_frame,
    uint32_t sample_rate)
{
    apta_pcm_feature_block_t block = {
        .deck = deck,
        .first_source_frame = first_source_frame,
        .source_frame_count = frame_count,
        .sample_rate = sample_rate,
        .peak_min = INT16_MAX,
        .peak_max = INT16_MIN
    };

    uint64_t energy = 0;
    size_t output_index = 0;

    size_t stride = frame_count / APTA_TAP_MONO_SAMPLES;
    if (stride == 0) {
        stride = 1;
    }

    for (size_t i = 0; i < frame_count; ++i) {
        int32_t left = pcm[i * 2];
        int32_t right = pcm[i * 2 + 1];
        int32_t mono = (left + right) / 2;

        if (mono < block.peak_min) {
            block.peak_min = (int16_t)mono;
        }

        if (mono > block.peak_max) {
            block.peak_max = (int16_t)mono;
        }

        energy += (uint64_t)((int64_t)mono * mono);

        if ((i % stride) == 0 &&
            output_index < APTA_TAP_MONO_SAMPLES) {
            block.mono[output_index++] = (int16_t)mono;
        }
    }

    block.mono_count = output_index;

    if (frame_count > 0) {
        block.rms_q16 = apta_isqrt_q16(
            energy / frame_count);
    }

    apta_feature_queue_try_push(&block);
}
```

Queue mora koristiti unaprijed alocirani storage ili statički FreeRTOS queue.

Ako je queue pun, blok se odbacuje. Playback nikada ne čeka APTA-u.

---

# 11. Waveform

## 11.1. Overview format

```c
typedef struct {
    int16_t minimum;
    int16_t maximum;

    uint16_t rms;

    uint8_t low;
    uint8_t mid;
    uint8_t high;

    uint8_t valid;
} apta_overview_column_t;
```

Početno:

```text
CONFIG_APTA_OVERVIEW_COLUMNS = 2048
```

Za 2048 stupaca memorija je mala i prihvatljiva.

## 11.2. Mapping source framea

```c
static uint32_t apta_frame_to_column(
    uint64_t source_frame,
    uint64_t total_frames,
    uint32_t column_count)
{
    if (total_frames == 0 || column_count == 0) {
        return 0;
    }

    uint64_t value =
        source_frame * (uint64_t)column_count /
        total_frames;

    if (value >= column_count) {
        value = column_count - 1;
    }

    return (uint32_t)value;
}
```

## 11.3. Detail tile

```c
#define APTA_DETAIL_TILE_COLUMNS 256

typedef struct {
    uint32_t tile_index;
    uint64_t first_source_frame;
    uint64_t last_source_frame;

    uint16_t valid_columns;
    uint16_t flags;

    apta_overview_column_t
        columns[APTA_DETAIL_TILE_COLUMNS];
} apta_detail_tile_t;
```

U RAM-u se drži samo:

- aktivni tile;
- nekoliko tileova oko playheada;
- LRU cache ograničene veličine.

Cijeli detail waveform ne smije biti obvezno residentan.

## 11.4. Progresivna objava

Snapshot treba sadržavati:

```c
typedef struct {
    uint32_t column_count;
    uint32_t valid_column_count;

    const apta_overview_column_t *columns;

    bool complete;
} apta_waveform_snapshot_t;
```

UI može razlikovati:

- valjani analizirani dio;
- neanalizirani dio;
- provisional waveform;
- Rekordbox waveform.

---

# 12. 3Band waveform

## 12.1. Ciljani pojasevi

Početne granice:

```text
LOW:  ispod približno 250 Hz
MID:  250 Hz – 2,5 kHz
HIGH: iznad približno 2,5 kHz
```

Nije potreban studijski crossover. Potrebna je vizualno stabilna raspodjela energije.

## 12.2. Filter state

```c
typedef struct {
    apta_biquad_t lowpass_250;
    apta_biquad_t lowpass_2500;

    float low_state[4];
    float mid_state[4];
    float high_state[4];
} apta_filterbank_t;
```

Poželjno je koristiti ESP-DSP biquad funkcije ili vlastitu optimiziranu Direct Form II implementaciju.

MID se može dobiti kao:

```text
LP(2500 Hz) - LP(250 Hz)
```

HIGH kao:

```text
original - LP(2500 Hz)
```

Time su potrebna samo dva low-pass filtera.

---

# 13. Analysis-rate signal

BPM i beatgrid ne trebaju puni 44,1/48 kHz signal.

## 13.1. Downmix

```text
mono = (left + right) / 2
```

Koristiti 32-bitni privremeni rezultat.

## 13.2. Decimacija

Uobičajeni putevi:

```text
44.100 Hz → 11.025 Hz
48.000 Hz → 12.000 Hz
```

Faktor četiri pojednostavljuje resampling.

Prije odbacivanja uzoraka primijeniti anti-alias low-pass.

## 13.3. Timestamp model

Sve vanjske pozicije moraju ostati u originalnim source frameovima.

Ne spremati beat pozicije samo u analysis-rate indeksima.

```c
uint64_t source_frame =
    analysis_sample_index *
    source_sample_rate /
    analysis_sample_rate;
```

Za akumulaciju koristiti 64-bitnu aritmetiku i remainder kako bi se izbjegao drift.

---

# 14. Onset detection

## 14.1. Početne vrijednosti

```text
analysis rate:  11.025 ili 12 kHz
FFT size:       512
hop size:       128
window:         Hann
```

## 14.2. Radni bufferi

U internoj memoriji:

```c
typedef struct {
    float *window;
    float *fft_input;
    float *fft_output;

    float *previous_magnitude;
    float *current_magnitude;

    float *novelty_ring;
    uint32_t novelty_capacity;
    uint32_t novelty_write;
} apta_onset_state_t;
```

FFT radni bufferi trebaju biti poravnani i alocirani s prikladnim memory capabilities flagovima.

## 14.3. Spectral flux

```c
static float apta_spectral_flux(
    const float *current,
    const float *previous,
    size_t bins)
{
    float flux = 0.0f;

    for (size_t i = 0; i < bins; ++i) {
        float delta = current[i] - previous[i];

        if (delta > 0.0f) {
            flux += delta;
        }
    }

    return flux;
}
```

Produkcijska verzija koristi:

- log ili komprimiranu magnitudu;
- lokalnu normalizaciju;
- odvojeni low/mid/high flux;
- noise floor;
- adaptive threshold.

## 14.4. Peak picking

Onset je valjan kada:

```text
novelty[n] > lokalni adaptive threshold
novelty[n] je lokalni maksimum
minimalni razmak od prethodnog onseta je zadovoljen
```

Adaptive threshold može koristiti median ili moving mean:

```text
threshold = local_mean + sensitivity × local_deviation
```

---

# 15. BPM analiza

## 15.1. Fast scan segmenti

Kada nijedan deck ne svira:

```text
početak glazbenog sadržaja
25% trajanja
50% trajanja
75% trajanja
završni reprezentativni segment
```

Kada jedan ili oba decka sviraju:

```text
1. područje oko playheada
2. sekvencijalno unaprijed
3. udaljeni segmenti tek kada je USB neopterećen
```

Random seekove treba minimizirati.

## 15.2. Tempo candidate format

```c
#define APTA_MAX_TEMPO_CANDIDATES 8

typedef struct {
    uint32_t bpm_x100;
    float autocorrelation_score;
    float onset_alignment_score;
    float segment_consistency_score;
    float combined_score;

    uint8_t confidence;
} apta_tempo_candidate_t;
```

## 15.3. Autocorrelation

Tempo raspon:

```text
40–300 BPM
```

Za svaki lag koji odgovara tom rasponu:

```text
score[lag] =
    suma novelty[n] × novelty[n - lag]
```

Kandidate zatim provjeriti na:

- 0,5×;
- 1×;
- 2× odnos;
- slaganje između segmenata;
- onset alignment;
- stabilnost faze.

## 15.4. Provisional BPM

BPM se može objaviti kada:

- najmanje dva reprezentativna segmenta daju kompatibilan rezultat;
- najbolji kandidat dovoljno nadmašuje drugi;
- confidence prelazi konfigurirani prag.

Primjer:

```text
BPM: 127,98
status: provisional
confidence: 82%
```

## 15.5. Finalni BPM

Finalni BPM se objavljuje nakon:

- dovoljnog dijela cijele trake;
- globalne provjere half/double-time odnosa;
- stabilnog globalnog grida;
- provjere tempo segmenata.

---

# 16. Lokalni beatgrid

Lokalni beatgrid ima veći prioritet od globalnog.

## 16.1. Područje

Početno analizirati:

```text
30 sekundi prije playheada
90 sekundi nakon playheada
```

Kada nema aktivnog playheada:

```text
početak glazbenog sadržaja
prvih 90–120 sekundi
```

## 16.2. Phase search

Za tempo kandidat izračunati beat period:

```text
frames_per_beat =
    sample_rate × 60 / BPM
```

Isprobati phase offsete unutar jednog beat perioda.

Score:

```text
nagrada za snažan onset blizu predviđenog beata
kazna za snažan onset između beatova
kazna za nestabilan period
```

## 16.3. Lokalni tracker

Tracker održava:

```c
typedef struct {
    double period_frames;
    double phase_frame;

    double tempo_velocity;
    double phase_error;

    uint32_t confirmed_beats;
    uint32_t predicted_beats;

    uint8_t confidence;
} apta_beat_tracker_t;
```

Svaki potvrđeni onset može samo ograničeno korigirati:

- fazu;
- period;
- tempo drift.

Jedan onset ne smije naglo promijeniti BPM.

---

# 17. Globalni beatgrid

## 17.1. Grid segment

```c
typedef struct {
    uint64_t first_beat_frame;

    uint32_t beat_count;
    uint32_t bpm_x100;

    uint32_t frames_per_beat_q16;

    uint8_t confidence;
    uint8_t flags;
} apta_grid_segment_t;
```

Za stabilnu pjesmu dovoljan je jedan segment.

Za promjenjivi tempo koristi se više segmenata.

## 17.2. Segment se otvara kada

- stabilni lokalni BPM odstupi više od praga;
- phase drift prijeđe dopušteni limit;
- detektirana je potvrđena tempo promjena;
- korisnik ručno promijeni grid;
- novi dio pjesme zahtijeva novi period model.

## 17.3. Offline refinement

Nakon provisional rezultata pozadinski algoritam treba:

- analizirati cijeli onset envelope;
- pronaći optimalni slijed beatova;
- dopustiti male tempo promjene;
- penalizirati nerealne skokove;
- premostiti breakdown bez bubnja;
- ponovno ocijeniti half/double-time hipotezu.

---

# 18. Confidence

## 18.1. Model

```c
typedef struct {
    uint8_t waveform;
    uint8_t bpm;
    uint8_t beat_phase;
    uint8_t beat_grid;
    uint8_t dynamic_tempo;
} apta_confidence_t;
```

## 18.2. BPM confidence

Ovisi o:

- razlici najboljeg i drugog kandidata;
- slaganju više segmenata;
- jasnoći autocorrelation peaka;
- lokalnoj stabilnosti;
- half/double-time dvosmislenosti.

## 18.3. Grid confidence

Ovisi o:

- prosječnom phase erroru;
- broju potvrđenih beatova;
- udjelu predviđenih beatova bez onseta;
- stabilnosti perioda;
- slaganju lokalnih i globalnih segmenata.

## 18.4. Pravila korištenja

```text
90–100%  Sync i Quantize dopušteni
75–89%   dopušteni uz provisional status
50–74%   BPM i waveform dostupni; Sync ograničen
ispod 50% grid se ne koristi automatski
```

Pragovi moraju biti Kconfig opcije.

---

# 19. Zaključavanje grida

## 19.1. Razlog

Pozadinska analiza ne smije promijeniti:

- aktivni loop;
- Hot Cue poziciju;
- Beat Jump rezultat;
- signed phase koji koristi Sync;
- područje koje DJ upravo reproducira.

## 19.2. Lock range

Pri događajima Play, Cue, Loop, Beat Jump ili Sync zaključati:

```text
8 beatova iza playheada
16–32 beata ispred playheada
```

## 19.3. Struktura

```c
typedef struct {
    uint64_t first_frame;
    uint64_t last_frame;

    uint32_t reason_flags;
} apta_locked_range_t;
```

## 19.4. Pravilo primjene refinementsa

Ako novi grid mijenja zaključano područje:

```text
ne primjenjuj odmah
spremi kao pending revision
primijeni pri sljedećem LOAD-u
```

Promjene izvan zaključanog područja mogu se objaviti odmah.

---

# 20. Snapshot model

## 20.1. Immutable snapshot

```c
typedef struct apta_snapshot {
    _Atomic uint32_t reference_count;

    uint32_t generation;

    apta_source_t source;
    apta_state_t state;

    apta_fingerprint_t fingerprint;

    apta_tempo_info_t tempo;
    apta_confidence_t confidence;

    apta_waveform_snapshot_t waveform;

    uint32_t grid_segment_count;
    const apta_grid_segment_t *grid_segments;

    uint32_t locked_range_count;
    const apta_locked_range_t *locked_ranges;

    uint32_t progress_permille;

    bool cache_complete;
    bool pending_refinement;
} apta_snapshot_t;
```

## 20.2. Transakcijska objava

```c
static void apta_publish_snapshot(
    apta_session_t *session,
    apta_snapshot_t *new_snapshot)
{
    apta_snapshot_t *old_snapshot;

    portENTER_CRITICAL(&session->snapshot_lock);
    old_snapshot = session->published;
    session->published = new_snapshot;
    portEXIT_CRITICAL(&session->snapshot_lock);

    if (old_snapshot != NULL) {
        apta_snapshot_release(old_snapshot);
    }
}
```

Snapshot se potpuno izradi prije ulaska u kritičnu sekciju.

---

# 21. Fingerprint

## 21.1. Brzi fingerprint

```text
SHA-256(
    format version
    file size
    duration
    sample rate
    channel count
    prvi blok datoteke
    blok iz sredine
    zadnji blok datoteke
)
```

Početne veličine:

```text
prvih 128 KiB
64 KiB iz sredine
zadnjih 128 KiB
```

## 21.2. Namjena

Fingerprint služi za:

- prepoznavanje iste trake nakon preimenovanja;
- pronalaženje cachea nakon ponovnog umetanja USB-a;
- invalidaciju cachea nakon promjene datoteke;
- razlikovanje traka s istim nazivom.

Puni hash cijele datoteke nije dio kritičnog LOAD puta.

---

# 22. APTA cache

## 22.1. Lokacija

Preporučena lokacija na internom microSD mediju:

```text
/sd/trackcache/apta/xx/<fingerprint>.apta
```

APTA cache nije spremljen na USB izvor.

Time se:

- ne mijenja korisnikov USB;
- cache može preživjeti promjenu putanje;
- izbjegava dodatni zapis na USB;
- odvajaju source I/O i cache I/O.

## 22.2. Header

```c
typedef struct {
    uint8_t magic[4];       /* "APTA" */

    uint16_t format_version;
    uint16_t header_size;

    uint32_t flags;
    uint32_t total_size;

    apta_fingerprint_t fingerprint;

    uint64_t source_file_size;
    uint32_t duration_ms;
    uint32_t source_sample_rate;

    uint16_t channels;
    uint16_t section_count;

    uint32_t header_crc32;
} apta_cache_header_t;
```

## 22.3. Sekcije

```text
META  osnovni metadata
WOVR  overview waveform
WTIX  detail tile index
WDTL  detail tileovi
TEMP  BPM i kandidati
BGRD  beatgrid
CONF  confidence
LOCK  trajna ručna zaključavanja, ako postoje
PROG  checkpoint analize
```

## 22.4. Descriptor sekcije

```c
typedef struct {
    uint32_t fourcc;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;

    uint16_t version;
    uint16_t flags;
} apta_cache_section_t;
```

## 22.5. Atomic zapis

```text
1. otvori <fingerprint>.tmp
2. zapiši header i sekcije
3. flush
4. zatvori datoteku
5. ponovno provjeri header i CRC
6. rename u <fingerprint>.apta
```

Cache write failure ne smije prekinuti reprodukciju ili analizu.

## 22.6. Checkpointi

Checkpoint spremiti nakon:

- dovršenog overview waveforma;
- provisional BPM-a;
- lokalnog beatgrida;
- globalnog beatgrida;
- završene analize.

---

# 23. Integracija s library resolverom

## 23.1. Novi opći rezultat

```c
typedef struct {
    apta_source_t source;

    bool has_waveform;
    bool has_bpm;
    bool has_beatgrid;
    bool has_cues;

    uint32_t bpm_x100;

    struct track_waveform *waveform;
    struct track_beatgrid *beatgrid;
    struct track_cues *cues;

    apta_confidence_t confidence;
} track_analysis_result_t;
```

## 23.2. Resolver redoslijed

```c
esp_err_t library_resolve_track_analysis(
    const library_track_t *track,
    track_analysis_result_t **result_out)
{
    /*
     * 1. Existing Rekordbox ANLZ/cache.
     * 2. APTA cache.
     * 3. Minimal metadata + start progressive APTA.
     */
}
```

## 23.3. Merge pravila

Rekordbox je autoritativan za:

- Hot Cueove;
- Memory Cueove;
- ručno uređeni beatgrid;
- ručno uređene loopove;
- postojeći precizni BPM.

APTA smije nadopuniti samo ono što nedostaje:

- waveform;
- BPM;
- beatgrid;
- confidence;
- detail tileove.

APTA ne smije automatski prepisati ručno uređeni Rekordbox grid.

---

# 24. Integracija s `audio_engine`

Potrebne izmjene:

```text
audio decoder output
    → apta_pcm_tap_process()
```

Audio engine treba izložiti read-only pressure snapshot:

```c
esp_err_t audio_engine_get_pressure_snapshot(
    apta_runtime_pressure_t *out);
```

Ne treba dopustiti APTA komponenti izravan pristup internom `audio_engine` stateu.

APTA smije samo čitati:

- ring fill;
- underrun counters;
- output-late counter;
- deck active/playing;
- scratch;
- Master Tempo;
- seek/load status.

---

# 25. Integracija s `deck_core`

`deck_core` treba koristiti jedinstveni analysis snapshot bez obzira na izvor.

Potrebne funkcije:

```c
bool deck_core_analysis_has_usable_bpm(uint8_t deck);
bool deck_core_analysis_has_usable_grid(uint8_t deck);

uint32_t deck_core_analysis_get_bpm_x100(uint8_t deck);

bool deck_core_analysis_find_nearest_beat(
    uint8_t deck,
    uint64_t source_frame,
    uint64_t *beat_frame_out);
```

Sync i Quantize provjeravaju confidence prije korištenja provisional grida.

Pri korištenju beat funkcije `deck_core` poziva:

```c
apta_notify_sync_use(deck, source_frame);
```

ili drugi odgovarajući lock API.

---

# 26. Integracija s UI-em

UI ne smije čitati mutable working state.

Koristi samo snapshot:

```c
const apta_snapshot_t *snapshot =
    apta_snapshot_acquire(deck);

/* render */

apta_snapshot_release(snapshot);
```

## 26.1. Vizualni status

Preporučene oznake:

```text
ANLZ    Rekordbox analiza
CACHE   APTA cache
SCAN    aktivna progresivna analiza
BPM?    provisional BPM
GRID?   provisional beatgrid
READY   dovršena analiza
```

Neanalizirani dio waveforma može se prikazati neutralnom pozadinom.

UI ne treba renderirati novu cijelu waveform površinu nakon svake kolone. Update treba biti:

- bounded;
- tile-based;
- sinkroniziran s postojećim waveform schedulerom.

---

# 27. Kconfig

```text
CONFIG_APTA_ENABLED
CONFIG_APTA_CACHE_ENABLED
CONFIG_APTA_DETAIL_WAVEFORM_ENABLED
CONFIG_APTA_DYNAMIC_TEMPO_ENABLED
```

## 27.1. Task postavke

```text
CONFIG_APTA_TASK_CORE
CONFIG_APTA_TASK_PRIORITY
CONFIG_APTA_TASK_STACK_SIZE

CONFIG_APTA_CACHE_TASK_PRIORITY
CONFIG_APTA_CACHE_TASK_STACK_SIZE
```

## 27.2. Scheduler

```text
CONFIG_APTA_BUDGET_DUAL_PLAY_US
CONFIG_APTA_BUDGET_SINGLE_PLAY_US
CONFIG_APTA_BUDGET_IDLE_US

CONFIG_APTA_BACKOFF_UNDERRUN_MS
CONFIG_APTA_RESUME_STABLE_MS

CONFIG_APTA_PCM_RING_PAUSE_PERCENT
CONFIG_APTA_PCM_RING_RESUME_PERCENT
```

## 27.3. USB I/O

```text
CONFIG_APTA_IO_TIMEOUT_MS

CONFIG_APTA_IO_CHUNK_IDLE
CONFIG_APTA_IO_CHUNK_SINGLE_PLAY
CONFIG_APTA_IO_CHUNK_DUAL_PLAY
```

## 27.4. DSP

```text
CONFIG_APTA_ANALYSIS_DECIMATION
CONFIG_APTA_FFT_SIZE
CONFIG_APTA_FFT_HOP

CONFIG_APTA_MIN_BPM
CONFIG_APTA_MAX_BPM

CONFIG_APTA_PROVISIONAL_BPM_CONFIDENCE
CONFIG_APTA_SYNC_GRID_CONFIDENCE
```

## 27.5. Waveform

```text
CONFIG_APTA_OVERVIEW_COLUMNS
CONFIG_APTA_DETAIL_TILE_COLUMNS
CONFIG_APTA_RESIDENT_DETAIL_TILES
```

## 27.6. Cache

```text
CONFIG_APTA_CACHE_PATH
CONFIG_APTA_CACHE_MAX_BYTES
CONFIG_APTA_CACHE_CHECKPOINT_ENABLED
```

## 27.7. Debug

```text
CONFIG_APTA_METRICS
CONFIG_APTA_VERBOSE_LOG
CONFIG_APTA_DUMP_ONSET
CONFIG_APTA_FAULT_INJECTION
```

---

# 28. CMake i dependency

## 28.1. `CMakeLists.txt`

```cmake
idf_component_register(
    SRCS
        "apta_service.c"
        "apta_session.c"
        "apta_scheduler.c"
        "apta_pcm_tap.c"
        "apta_scan_decoder.c"
        "apta_resampler.c"
        "apta_waveform.c"
        "apta_filterbank.c"
        "apta_onset.c"
        "apta_tempo.c"
        "apta_beatgrid.c"
        "apta_confidence.c"
        "apta_fingerprint.c"
        "apta_cache.c"
        "apta_snapshot.c"
        "apta_metrics.c"

    INCLUDE_DIRS
        "include"

    PRIV_INCLUDE_DIRS
        "."

    REQUIRES
        esp_timer
        mbedtls
        esp-dsp

    PRIV_REQUIRES
        audio_engine
        library
        media_io_gate
)
```

## 28.2. `idf_component.yml`

```yaml
dependencies:
  espressif/esp-dsp:
    version: "1.8.2"
```

Dependency mora biti zaključan kroz projektni lock file.

---

# 29. Telemetrija

## 29.1. Metrics struktura

```c
typedef struct {
    uint64_t total_cpu_us;
    uint32_t maximum_slice_us;

    uint64_t usb_bytes_read;
    uint32_t usb_read_count;
    uint32_t usb_gate_timeout_count;
    uint32_t usb_max_read_us;
    uint32_t usb_max_gate_wait_us;

    uint32_t pause_count;
    apta_pause_reason_t last_pause_reason;

    uint32_t cache_hit_count;
    uint32_t cache_miss_count;
    uint32_t cache_write_failure_count;

    uint32_t snapshot_publish_count;
    uint32_t rejected_locked_grid_update_count;

    size_t peak_internal_bytes;
    size_t peak_psram_bytes;
} apta_metrics_t;
```

## 29.2. Status API

```json
{
  "analysis": {
    "deck1": {
      "state": "refining",
      "source": "progressive",
      "progress_permille": 630,
      "bpm_x100": 12798,
      "bpm_confidence": 91,
      "grid_confidence": 84,
      "overview_complete": true,
      "cache_complete": false,
      "usb_bytes_read": 8182784,
      "pause_count": 7,
      "last_pause_reason": "audio_ring_low"
    }
  }
}
```

---

# 30. Error handling

## 30.1. USB removal

Pri uklanjanju jedinog USB medija:

```text
1. označi media source nedostupnim
2. zabrani nova otvaranja
3. prekini aktivni APTA read
4. otkaži obje APTA sesije
5. zatvori scan decoder
6. zaustavi playback prema postojećem pravilu
7. oslobodi track path i decoder resurse
8. zadrži samo sigurne immutable snapshotove
```

## 30.2. Decoder error

Ako scan decoder ne može nastaviti:

- playback decoder se ne dira;
- session prelazi u `ERROR`;
- lokalni waveform iz playback PCM-a može ostati dostupan;
- cache se ne zapisuje kao complete;
- prethodno valjan snapshot ostaje objavljen.

## 30.3. Cache error

Cache read ili write error:

- nije fatalan;
- cache se odbacuje;
- analiza se pokreće ponovno;
- reprodukcija ostaje neometana.

## 30.4. Nedostatak memorije

Pri allocation failureu:

```text
1. odbaci detail tileove
2. smanji resident tile cache
3. pauziraj globalni refinement
4. zadrži playback waveform i postojeći grid
5. ne pokušavaj agresivno ponovno alocirati u petlji
```

---

# 31. Implementacijske faze

## Faza A0 — Baseline

Izmjeriti:

- CPU load;
- task runtime;
- PCM ring minimum;
- USB read latency;
- gate wait;
- interni heap;
- PSRAM;
- DSI underrun;
- output-late;
- dual-deck playback;
- dual Master Tempo;
- dual scratch.

### Exit kriterij

Postoji reproducibilan baseline prije APTA izmjena.

---

## Faza A1 — Infrastruktura

Implementirati:

- `apta` komponentu;
- dvije sesije;
- jedan service task;
- jedan cache task;
- command queue;
- state machine;
- immutable snapshot;
- osnovne metrics.

### Exit kriterij

Start, pause, resume, cancel i session replacement rade bez leakova.

---

## Faza A2 — Lokalni waveform tap

Implementirati:

- playback PCM tap;
- min/max/RMS;
- lokalne detail tileove;
- UI prikaz;
- bounded queue.

### Exit kriterij

Neanalizirana pjesma dobiva lokalni waveform bez dodatnog USB čitanja.

---

## Faza A3 — Background overview

Implementirati:

- jedan scan decoder;
- jedan globalni USB gate;
- time-sliced čitanje;
- overview akumulaciju;
- progressive snapshot;
- pause/resume prema pressureu.

### Exit kriterij

Overview se dovršava tijekom dual playbacka bez audio i DSI regresije.

---

## Faza A4 — 3Band waveform

Implementirati:

- low/mid/high filterbank;
- band energy;
- RGB ili 3Band vrijednosti;
- detail tile format.

### Exit kriterij

Waveform vizualno stabilno razlikuje bas, srednje i visoke frekvencije.

---

## Faza A5 — Onset envelope

Implementirati:

- downmix;
- decimaciju;
- Hann;
- FFT;
- spectral flux;
- peak picking;
- host dump.

### Exit kriterij

Glavni ritmički tranzijenti proizvode stabilne onset peakove.

---

## Faza A6 — Provisional BPM

Implementirati:

- fast scan;
- autocorrelation;
- candidate scoring;
- half/double-time;
- confidence;
- provisional objavu.

### Exit kriterij

Stabilne elektroničke trake brzo dobivaju konzistentan provisional BPM.

---

## Faza A7 — Lokalni beatgrid

Implementirati:

- phase search;
- lokalni tracker;
- playhead window;
- provisional grid;
- `deck_core` integraciju.

### Exit kriterij

Loop, Beat Jump i Quantize rade na lokalnom gridu.

---

## Faza A8 — Grid locking

Implementirati:

- lock range;
- Play/Cue/Loop/Sync notifikacije;
- pending revision;
- zaštitu aktivnih segmenata.

### Exit kriterij

Background analiza ne može pomaknuti aktivni loop ili Sync fazu.

---

## Faza A9 — Globalni grid

Implementirati:

- full-track onset obradu;
- offline refinement;
- tempo segmente;
- dynamic tempo;
- finalni confidence.

### Exit kriterij

Cijela pjesma dobiva stabilan finalni beatgrid.

---

## Faza A10 — Cache

Implementirati:

- fingerprint;
- cache header;
- sekcije;
- CRC;
- checkpoint;
- atomic write;
- cache validation;
- eviction.

### Exit kriterij

Drugi LOAD iste trake ne pokreće ponovno audio skeniranje.

---

## Faza A11 — Resolver integracija

Implementirati:

- Rekordbox → metadata cache → APTA cache → live APTA redoslijed;
- merge pravila;
- source status;
- jedinstveno sučelje za UI i `deck_core`.

### Exit kriterij

Korisnik i deck logika ne moraju znati odakle analiza dolazi.

---

## Faza A12 — Hardening

Testirati:

- USB removal;
- SD removal;
- corrupt cache;
- decoder failure;
- allocation failure;
- 100 izmjeničnih D1/D2 loadova;
- istodobni playback;
- mixed 44,1/48 kHz;
- Master Tempo;
- loop i Sync;
- scratch;
- dugačke trake;
- malformed datoteke.

### Exit kriterij

Nema panica, watchdog resetova, leakova, underruna ni blokiranog USB stacka.

---

# 32. Host testovi

```text
tests/apta_state/
tests/apta_scheduler/
tests/apta_waveform/
tests/apta_filterbank/
tests/apta_onset/
tests/apta_tempo/
tests/apta_beatgrid/
tests/apta_locking/
tests/apta_cache/
tests/apta_resolver/
tests/apta_faults/
```

## 32.1. Sintetički testni signali

Generirati:

- click track 60–200 BPM;
- 44,1 i 48 kHz;
- half-time;
- double-time;
- sinkopu;
- swing;
- dugi intro;
- breakdown;
- tempo ramp;
- naglu tempo promjenu;
- tišinu;
- clipping;
- vrlo tihu traku.

## 32.2. Početni kriteriji

Za stabilne elektroničke trake:

```text
finalni BPM error:       ≤ 0,1%
median beat error:       ≤ 15 ms
95. percentil:           ≤ 30 ms
provisional BPM error:   ≤ 1%
```

Za live drums i dynamic tempo koristi se zasebna tolerancija.

---

# 33. Hardware acceptance

| Test | USB | Deck 1 | Deck 2 | APTA |
|---|---|---|---|---|
| Single deck | priključen | play | idle | D1 scan |
| Dual deck | priključen | play | play | jedan scan |
| Dual session | priključen | loaded | loaded | time-slice |
| LOAD tijekom playa | priključen | play | load | pauza |
| Mixed sample rate | priključen | 44,1 kHz | 48 kHz | bounded |
| Dual Master Tempo | priključen | MT | MT | throttle |
| Dual scratch | priključen | scratch | scratch | pauza |
| Loop + Sync | priključen | loop | sync | refinement |
| USB removal | uklonjen | play | loaded | cancel |
| SD removal | priključen | play | play | bez cachea |
| Spor USB | priključen | play | play | throttle |
| Sto loadova | priključen | alternate | alternate | bez leaka |

Obvezno pratiti:

- PCM underrun;
- output-late;
- DSI underrun;
- USB error;
- gate timeout;
- max read latency;
- max APTA slice;
- internal heap;
- largest internal block;
- PSRAM;
- cache integrity;
- watchdog.

---

# 34. Produkcijski MVP

APTA MVP je prihvatljiv kada:

1. MP3, WAV ili FLAC bez ANLZ-a može se odmah učitati.
2. Reprodukcija ne čeka analizu.
3. Lokalni waveform nastaje iz playback PCM-a.
4. Overview se progresivno popunjava.
5. Provisional BPM se objavljuje s confidenceom.
6. Lokalni beatgrid omogućuje loop i Beat Jump.
7. Finalni grid nastaje u pozadini.
8. Aktivni grid se ne pomiče.
9. Rezultat se sprema u cache.
10. Ponovni LOAD koristi cache.
11. Rekordbox ANLZ uvijek ima prednost.
12. Postoji samo jedan aktivni USB background scan.
13. Oba decka koriste isti USB izvor.
14. USB removal ne izaziva panic.
15. Cache failure nije fatalan.
16. Nema novih PCM underruna.
17. Nema novih DSI underruna.
18. Nema memory leaka kroz najmanje 100 loadova.
19. Svi host testovi prolaze.
20. ESP-IDF 5.5.4 release build prolazi.

---

# 35. Konačni implementacijski model

```text
JEDAN USB MASS-STORAGE IZVOR
            │
            ▼
    JEDAN MEDIA I/O GATE
            │
      ┌─────┼─────┐
      │     │     │
      ▼     ▼     ▼
    D1     D2    APTA
 playback playback scan
 decoder  decoder worker

DVIJE APTA SESIJE
JEDAN APTA SERVICE TASK
NAJVIŠE JEDAN BACKGROUND SCAN DECODER
AUDIO UVIJEK IMA PRIORITET
REKORDBOX ANLZ UVIJEK IMA PRIORITET
APTA CACHE JE FALLBACK I AUTOMATSKA OPTIMIZACIJA
```

Prvi stvarni kodni rez treba obuhvatiti samo Faze A0–A3. BPM i beatgrid ne treba dodavati dok jedan background decoder, USB arbitraža, lokalni waveform i progresivni overview ne prođu dual-deck hardware acceptance.
