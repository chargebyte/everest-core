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

// FIXME: inline?
void dlog_func(const dloglevel_t loglevel, const char* format, ...) {
    char* format_copy = NULL;
    va_list args;

    va_start(args, format);

    // print the user given part
    // strip possible newline character from user-given string
    // FIXME: could be skipped
    if (format) {
        size_t formatlen = std::string(format).size();
        format_copy = static_cast<char*>(calloc(1, formatlen + 1)); // additional byte for terminating \0
        memcpy(format_copy, format, formatlen);
        if ((formatlen >= 1) && (format_copy[formatlen - 1] == '\n')) {
            format_copy[formatlen - 1] = '\0';
        }
    }
    char output[256];
    if (format_copy != NULL) {
        vsnprintf(output, sizeof(output), format_copy, args);
    }
    va_end(args);
    if (format_copy) {
        free(format_copy);
    }

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
