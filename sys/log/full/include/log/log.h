/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#ifndef __SYS_LOG_FULL_H__
#define __SYS_LOG_FULL_H__

#include "os/mynewt.h"
#include "log/ignore.h"
#include "cbmem/cbmem.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global log info */
struct log_info {
    uint32_t li_next_index;
    uint8_t li_version;
};
#define LOG_VERSION_V3  3
#define LOG_VERSION_V2  2
#define LOG_VERSION_V1  1

extern struct log_info g_log_info;

struct log;
struct log_entry_hdr;

/**
 * Used for walks and reads; indicates part of log to access.
 */
struct log_offset {
    /* If   lo_ts == -1: Only access last log entry;
     * Elif lo_ts == 0:  Don't filter by timestamp;
     * Else:             Only access entries whose ts >= lo_ts.
     */
    int64_t lo_ts;

    /* Only access entries whose index >= lo_index. */
    uint32_t lo_index;

    /* On read, lo_data_len gets populated with the number of bytes read. */
    uint32_t lo_data_len;

    /* Specific to walk / read function. */
    void *lo_arg;
};

typedef int (*log_walk_func_t)(struct log *, struct log_offset *log_offset,
        void *offset, uint16_t len);

typedef int (*lh_read_func_t)(struct log *, void *dptr, void *buf,
        uint16_t offset, uint16_t len);
typedef int (*lh_read_mbuf_func_t)(struct log *, void *dptr, struct os_mbuf *om,
                                   uint16_t offset, uint16_t len);
typedef int (*lh_append_func_t)(struct log *, void *buf, int len);
typedef int (*lh_append_mbuf_func_t)(struct log *, struct os_mbuf *om);
typedef int (*lh_walk_func_t)(struct log *,
        log_walk_func_t walk_func, struct log_offset *log_offset);
typedef int (*lh_flush_func_t)(struct log *);
typedef int (*lh_registered_func_t)(struct log *);

#define LOG_TYPE_STREAM  (0)
#define LOG_TYPE_MEMORY  (1)
#define LOG_TYPE_STORAGE (2)

struct log_handler {
    int log_type;
    lh_read_func_t log_read;
    lh_read_mbuf_func_t log_read_mbuf;
    lh_append_func_t log_append;
    lh_append_mbuf_func_t log_append_mbuf;
    lh_walk_func_t log_walk;
    lh_flush_func_t log_flush;
    /* Functions called only internally (no API for apps) */
    lh_registered_func_t log_registered;
};

#if MYNEWT_VAL(LOG_VERSION) == 2
struct log_entry_hdr {
    int64_t ue_ts;
    uint32_t ue_index;
    uint8_t ue_module;
    uint8_t ue_level;
}__attribute__((__packed__));
#elif MYNEWT_VAL(LOG_VERSION) == 3
struct log_entry_hdr {
    int64_t ue_ts;
    uint32_t ue_index;
    uint8_t ue_module;
    uint8_t ue_level;
    uint8_t ue_etype;
}__attribute__((__packed__));
#else
#error "Unsupported log version"
#endif

#define LOG_ENTRY_HDR_SIZE (sizeof(struct log_entry_hdr))

#define LOG_LEVEL_DEBUG    (0)
#define LOG_LEVEL_INFO     (1)
#define LOG_LEVEL_WARN     (2)
#define LOG_LEVEL_ERROR    (3)
#define LOG_LEVEL_CRITICAL (4)
/* Up to 7 custom log levels. */
#define LOG_LEVEL_MAX      (UINT8_MAX)

#define LOG_LEVEL_STR(level) \
    (LOG_LEVEL_DEBUG    == level ? "DEBUG"    :\
    (LOG_LEVEL_INFO     == level ? "INFO"     :\
    (LOG_LEVEL_WARN     == level ? "WARN"     :\
    (LOG_LEVEL_ERROR    == level ? "ERROR"    :\
    (LOG_LEVEL_CRITICAL == level ? "CRITICAL" :\
     "UNKNOWN")))))

/* Log module, eventually this can be a part of the filter. */
#define LOG_MODULE_DEFAULT          (0)
#define LOG_MODULE_OS               (1)
#define LOG_MODULE_NEWTMGR          (2)
#define LOG_MODULE_NIMBLE_CTLR      (3)
#define LOG_MODULE_NIMBLE_HOST      (4)
#define LOG_MODULE_NFFS             (5)
#define LOG_MODULE_REBOOT           (6)
#define LOG_MODULE_IOTIVITY         (7)
#define LOG_MODULE_TEST             (8)
#define LOG_MODULE_PERUSER          (64)
#define LOG_MODULE_MAX              (255)

#define LOG_MODULE_STR(module)      log_module_get_name(module)

#define LOG_ETYPE_STRING         (0)
#if MYNEWT_VAL(LOG_VERSION) > 2
#define LOG_ETYPE_CBOR           (1)
#define LOG_ETYPE_BINARY         (2)
#endif

/* Logging medium */
#define LOG_STORE_CONSOLE    1
#define LOG_STORE_CBMEM      2
#define LOG_STORE_FCB        3

/* UTC Timestamnp for Jan 2016 00:00:00 */
#define UTC01_01_2016    1451606400

#define LOG_NAME_MAX_LEN    (64)

#if MYNEWT_VAL(LOG_LEVEL) <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(__l, __mod, __msg, ...) log_printf(__l, __mod, \
        LOG_LEVEL_DEBUG, __msg, ##__VA_ARGS__)
#else
#define LOG_DEBUG(__l, __mod, ...) IGNORE(__VA_ARGS__)
#endif

#if MYNEWT_VAL(LOG_LEVEL) <= LOG_LEVEL_INFO
#define LOG_INFO(__l, __mod, __msg, ...) log_printf(__l, __mod, \
        LOG_LEVEL_INFO, __msg, ##__VA_ARGS__)
#else
#define LOG_INFO(__l, __mod, ...) IGNORE(__VA_ARGS__)
#endif

#if MYNEWT_VAL(LOG_LEVEL) <= LOG_LEVEL_WARN
#define LOG_WARN(__l, __mod, __msg, ...) log_printf(__l, __mod, \
        LOG_LEVEL_WARN, __msg, ##__VA_ARGS__)
#else
#define LOG_WARN(__l, __mod, ...) IGNORE(__VA_ARGS__)
#endif

#if MYNEWT_VAL(LOG_LEVEL) <= LOG_LEVEL_ERROR
#define LOG_ERROR(__l, __mod, __msg, ...) log_printf(__l, __mod, \
        LOG_LEVEL_ERROR, __msg, ##__VA_ARGS__)
#else
#define LOG_ERROR(__l, __mod, ...) IGNORE(__VA_ARGS__)
#endif

#if MYNEWT_VAL(LOG_LEVEL) <= LOG_LEVEL_CRITICAL
#define LOG_CRITICAL(__l, __mod, __msg, ...) log_printf(__l, __mod, \
        LOG_LEVEL_CRITICAL, __msg, ##__VA_ARGS__)
#else
#define LOG_CRITICAL(__l, __mod, ...) IGNORE(__VA_ARGS__)
#endif

#ifndef MYNEWT_VAL_LOG_LEVEL
#define LOG_SYSLEVEL    ((uint8_t)0xff)
#else
#define LOG_SYSLEVEL    ((uint8_t)MYNEWT_VAL_LOG_LEVEL)
#endif

struct log {
    char *l_name;
    const struct log_handler *l_log;
    void *l_arg;
    STAILQ_ENTRY(log) l_next;
    uint8_t l_level;
};

/* Newtmgr Log opcodes */
#define LOGS_NMGR_OP_READ         (0)
#define LOGS_NMGR_OP_CLEAR        (1)
#define LOGS_NMGR_OP_APPEND       (2)
#define LOGS_NMGR_OP_MODULE_LIST  (3)
#define LOGS_NMGR_OP_LEVEL_LIST   (4)
#define LOGS_NMGR_OP_LOGS_LIST    (5)

/* Log system level functions (for all logs.) */
void log_init(void);
struct log *log_list_get_next(struct log *);

/*
 * Register per-user log module
 *
 * This function associates user log module with given name.
 *
 * If \p id is non-zero, module is registered with selected id.
 * If \p id is zero, module id is selected automatically (first available).
 *
 * Up to `LOG_MAX_USER_MODULES` (syscfg) modules can be registered with ids
 * starting from `LOG_MODULE_PERUSER`.
 *
 * @param id    Selected module id
 * @param name  Module name
 *
 * @return  module id on success, 0 on failure
 */
uint8_t log_module_register(uint8_t id, const char *name);

/*
 * Get name for module id
 *
 * This works for both system and user registered modules.
 *
 * @param id  Module id
 *
 * @return  module name or NULL if not a valid module
 */
const char *log_module_get_name(uint8_t id);

/* Log functions, manipulate a single log */
int log_register(char *name, struct log *log, const struct log_handler *,
                 void *arg, uint8_t level);
int log_append_typed(struct log *, uint8_t, uint8_t, uint8_t, void *, uint16_t);
int log_append_mbuf_typed(struct log *, uint8_t, uint8_t, uint8_t,
                          struct os_mbuf *);

static inline int
log_append(struct log *log, uint8_t module, uint8_t level, void *data,
           uint16_t len)
{
    return log_append_typed(log, module, level, LOG_ETYPE_STRING, data, len);
}

static inline int
log_append_mbuf(struct log *log, uint8_t module, uint8_t level,
                struct os_mbuf *om)
{
    return log_append_mbuf_typed(log, module, level, LOG_ETYPE_STRING, om);
}

#define LOG_PRINTF_MAX_ENTRY_LEN (128)
void log_printf(struct log *log, uint16_t, uint16_t, char *, ...);
int log_read(struct log *log, void *dptr, void *buf, uint16_t off,
        uint16_t len);
int log_read_mbuf(struct log *log, void *dptr, struct os_mbuf *om, uint16_t off,
                  uint16_t len);
int log_walk(struct log *log, log_walk_func_t walk_func,
        struct log_offset *log_offset);
int log_flush(struct log *log);

/* Handler exports */
#if MYNEWT_VAL(LOG_CONSOLE)
extern const struct log_handler log_console_handler;
#endif
extern const struct log_handler log_cbmem_handler;
#if MYNEWT_VAL(LOG_FCB)
extern const struct log_handler log_fcb_handler;
extern const struct log_handler log_fcb_slot1_handler;
#endif

/* Private */
#if MYNEWT_VAL(LOG_NEWTMGR)
int log_nmgr_register_group(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __SYS_LOG_FULL_H__ */
