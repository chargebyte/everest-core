// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022-2023 chargebyte GmbH
// Copyright (C) 2022-2023 Contributors to EVerest
#ifndef LOG_H
#define LOG_H

/**
 * @brief Describe the intended log level of a message, or the maximum level a message must have to be displayed.
 */
typedef enum dloglevel_t {
    DLOG_LEVEL_ALWAYS = 0, ///< internal use only, for notification of log level change
    DLOG_LEVEL_ERROR,      ///< error
    DLOG_LEVEL_WARNING,    ///< warning, not leading to unexpected behavior such as termination
    DLOG_LEVEL_INFO,       ///< informational message
    DLOG_LEVEL_DEBUG,      ///< message to help debug daemon activity
    DLOG_LEVEL_TRACE,      ///< message to provide extra internal information
    DLOG_LEVEL_NUMLEVELS,  ///< don't use, only for internal detection of upper range
} dloglevel_t;

/**
 * @brief Internal: Issue a log message. Please use the dlog() macro instead.
 *
 * @return void
 */
void dlog_func(const dloglevel_t loglevel, const char* format, ...);

/**
 * @brief Issue a log message.
 *
 * @param[in] level the log level this message belongs to (type enum dloglevel)
 * @param[in] printf()-like format string and parameters, without tailing '\n'
 *
 * @return void
 */
// this is a macro, so that when dlog() is used, it gets expanded at the caller's location
#define dlog(level, ...)                                                                                               \
    do {                                                                                                               \
        dlog_func((level), ##__VA_ARGS__);                                                                             \
    } while (0)

#endif /* LOG_H */
