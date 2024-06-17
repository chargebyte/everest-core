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
 * @brief Issue a log message.
 *
 * @param[in] level the log level this message belongs to (type enum dloglevel)
 * @param[in] printf()-like format string and parameters, without tailing '\n'
 *
 * @return void
 */
void dlog(const dloglevel_t loglevel, const char* format, ...);
#endif /* LOG_H */
