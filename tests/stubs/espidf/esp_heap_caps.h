// SPDX-License-Identifier: Apache-2.0
#ifndef TEST_ESP_HEAP_CAPS_H
#define TEST_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_8BIT     (1u << 0)
#define MALLOC_CAP_INTERNAL (1u << 1)
#define MALLOC_CAP_SPIRAM   (1u << 2)
#define MALLOC_CAP_DMA      (1u << 3)

void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void heap_caps_free(void *memory);
size_t heap_caps_get_free_size(uint32_t caps);

#endif
