// SPDX-License-Identifier: Apache-2.0
#ifndef APTA_ERRORS_H
#define APTA_ERRORS_H

#include <stdint.h>

typedef int32_t apta_status_t;

#define APTA_STATUS_OK             ((apta_status_t)0)
#define APTA_STATUS_MORE_WORK      ((apta_status_t)1)
#define APTA_STATUS_WOULD_BLOCK    ((apta_status_t)2)
#define APTA_STATUS_END_OF_INPUT   ((apta_status_t)3)
#define APTA_STATUS_NOT_AVAILABLE  ((apta_status_t)4)

#define APTA_ERROR_INVALID_ARGUMENT      ((apta_status_t)-1)
#define APTA_ERROR_OUT_OF_MEMORY         ((apta_status_t)-2)
#define APTA_ERROR_UNSUPPORTED           ((apta_status_t)-3)
#define APTA_ERROR_INCOMPATIBLE_VERSION  ((apta_status_t)-4)
#define APTA_ERROR_SOURCE                ((apta_status_t)-5)
#define APTA_ERROR_CORRUPT_DATA          ((apta_status_t)-6)
#define APTA_ERROR_CANCELLED             ((apta_status_t)-7)
#define APTA_ERROR_INTERNAL              ((apta_status_t)-8)
#define APTA_ERROR_BUSY                  ((apta_status_t)-9)
#define APTA_ERROR_CONFLICT              ((apta_status_t)-10)
#define APTA_ERROR_LIMIT_EXCEEDED        ((apta_status_t)-11)
#define APTA_ERROR_INVALID_STATE         ((apta_status_t)-12)

#endif /* APTA_ERRORS_H */
