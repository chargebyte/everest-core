// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022-2023 chargebyte GmbH
// Copyright (C) 2022-2023 Contributors to EVerest
#include "log.hpp"
#include <everest/logging.hpp> // for logging
#include <stdarg.h>            // for va_list, va_{start,end}()
#include <stdio.h>             // for v*printf()
#include <stdlib.h>            // for atoi()
#include <string.h>            // for strlen()
#include <sys/time.h>          // for gettimeofday()
#include <time.h>              // for strftime()

void dlog(const dloglevel_t loglevel, const char* format, ...) {
    char* format_copy = NULL;
    va_list args;

    va_start(args, format);
    char output[1024] = "\0";

    if (format != NULL) {
        vsnprintf(output, sizeof(output), format, args);
    }
    va_end(args);

    switch (loglevel) {
    case DLOG_LEVEL_ERROR:
        EVLOG_error << output;
        break;
    case DLOG_LEVEL_WARNING:
        EVLOG_warning << output;
        break;
    case DLOG_LEVEL_INFO:
        EVLOG_info << output;
        break;
    case DLOG_LEVEL_DEBUG:
        EVLOG_debug << output;
        break;
    case DLOG_LEVEL_TRACE:
        EVLOG_verbose << output;
        break;
    default:
        EVLOG_critical << "Unknown log level";
        break;
    }
}
