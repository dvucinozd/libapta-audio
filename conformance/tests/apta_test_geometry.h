// SPDX-License-Identifier: Apache-2.0
/*
 * C3: internal geometry as the tests see it.
 *
 * The library's capacity constants are #ifndef-guarded in
 * src/core/apta_internal.h so a host can override them from the build system.
 * Tests cannot include that header, but an override passed through
 * CMAKE_C_FLAGS reaches these translation units too, so the same macro name
 * with the same default gives tests a geometry that follows the library's.
 *
 * The defaults below are duplicated from apta_internal.h and could drift.
 * apta.waveform.overview asserts that the library agrees with this header at
 * runtime, so drift fails a test rather than passing silently.
 */
#ifndef APTA_TEST_GEOMETRY_H
#define APTA_TEST_GEOMETRY_H

#ifndef APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN
#define APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN 1024u
#endif

#ifndef APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN
#define APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN 256u
#endif

#ifndef APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE
#define APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE 64u
#endif

#define APTA_TEST_COLUMN_FRAMES APTA_INTERNAL_OVERVIEW_FRAMES_PER_COLUMN

/*
 * Tests with fixed static-workspace buffers size them for the default
 * geometry. A finer overview resolution means proportionally more accumulator
 * entries, so those buffers scale with it. One at the default, so default
 * builds allocate exactly what they always did.
 */
#ifndef APTA_INTERNAL_ONSET_BIN_CAPACITY
#define APTA_INTERNAL_ONSET_BIN_CAPACITY 4096u
#endif

#ifndef APTA_INTERNAL_GLOBAL_BIN_CAPACITY
#define APTA_INTERNAL_GLOBAL_BIN_CAPACITY 16384u
#endif

#if APTA_TEST_COLUMN_FRAMES >= 1024u
#define APTA_TEST_COLUMN_SCALE 1u
#else
#define APTA_TEST_COLUMN_SCALE (1024u / APTA_TEST_COLUMN_FRAMES)
#endif

/* The onset and global rings are fixed-size but overridable, and they are
 * charged to the same workspace. */
#if APTA_INTERNAL_ONSET_BIN_CAPACITY <= 4096u
#define APTA_TEST_ONSET_SCALE 1u
#else
#define APTA_TEST_ONSET_SCALE (APTA_INTERNAL_ONSET_BIN_CAPACITY / 4096u)
#endif

#if APTA_INTERNAL_GLOBAL_BIN_CAPACITY <= 16384u
#define APTA_TEST_GLOBAL_SCALE 1u
#else
#define APTA_TEST_GLOBAL_SCALE (APTA_INTERNAL_GLOBAL_BIN_CAPACITY / 16384u)
#endif

#if APTA_TEST_ONSET_SCALE >= APTA_TEST_GLOBAL_SCALE
#define APTA_TEST_RING_SCALE APTA_TEST_ONSET_SCALE
#else
#define APTA_TEST_RING_SCALE APTA_TEST_GLOBAL_SCALE
#endif

#define APTA_TEST_WORKSPACE_SCALE \
    (APTA_TEST_COLUMN_SCALE * APTA_TEST_RING_SCALE)
#define APTA_TEST_DETAIL_COLUMN_FRAMES APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN
#define APTA_TEST_DETAIL_TILE_FRAMES \
    (APTA_INTERNAL_DETAIL_FRAMES_PER_COLUMN * \
     APTA_INTERNAL_DETAIL_COLUMNS_PER_TILE)

#endif /* APTA_TEST_GEOMETRY_H */
