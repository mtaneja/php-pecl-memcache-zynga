/*
  Copyright 2013 Zynga Inc.
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2007 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Antony Dovgal <tony@daylessday.org>                         |
  |          Mikael Johansson <mikael AT synd DOT info>                  |
  +----------------------------------------------------------------------+
*/

/* $Id: memcache.c,v 1.111 2009/01/15 18:15:50 mikl Exp $ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <fcntl.h>
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>
#include <map>
#include <string>

#ifdef HAVE_BZ2
#include <bzlib.h>
#endif

#include <time.h>
#include <iostream>

extern "C" {
#include "php.h"
#include "php_ini.h"
#include "ext/standard/crc32.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"
#include "ext/standard/php_var.h"
#include "ext/standard/php_smart_str.h"
#include "SAPI.h"

#ifdef HAVE_MEMCACHE_IGBINARY
	#include "ext/igbinary/igbinary.h"
#endif

#include "php_network.h"
#include "php_memcache.h"
#include "memcache_queue.h"
}

#include "logger.h"

#ifndef ZEND_ENGINE_2
#define OnUpdateLong OnUpdateInt
#endif

/* True global resources - no need for thread safety here */
static int le_memcache_pool, le_pmemcache;
static zend_class_entry *memcache_class_entry_ptr;
static int memcache_lzo_enabled;
static void php_memcache_destroy_globals(zend_memcache_globals *memcache_globals_p TSRMLS_DC)
{
}

typedef struct _strings {
	char *data;
	int len;
} pecl_string_t;

//mc_logger_t * LogManager::val = NULL;
bool RequestLogger::enabled = false;
RequestLogger *RequestLogger::m_instance = NULL;
int maxLoggerSize;

std::map<std::string, mmc_t *> temp_multiget_proxy_mmc;

ZEND_DECLARE_MODULE_GLOBALS(memcache)

ZEND_BEGIN_ARG_INFO(memcache_get2_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(get2_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_get_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(0)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_get_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_findserver_arginfo, 0)
	ZEND_ARG_INFO(0, memcache)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_findserver_arginfo1, 0)
	ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_set_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_set_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_add_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_add_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_cas_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_cas_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_cas_arginfo3, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_cas_arginfo4, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_replace_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(memcache_replace_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_getl_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_getl_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_getlMultiByKey_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_getlMultiByKey_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
	ZEND_ARG_PASS_INFO(1)
ZEND_END_ARG_INFO();

// Parameter requirements for memcache_getByKey_arginfo
ZEND_BEGIN_ARG_INFO(memcache_getByKey_arginfo, 0)
	ZEND_ARG_PASS_INFO(0) // mmcobject
	ZEND_ARG_PASS_INFO(0) // key
	ZEND_ARG_PASS_INFO(0) // shardKey
	ZEND_ARG_PASS_INFO(1) // value
	ZEND_ARG_PASS_INFO(1) // flags
	ZEND_ARG_PASS_INFO(1) // cas = compare and swap token
ZEND_END_ARG_INFO();
ZEND_BEGIN_ARG_INFO(getByKey_arginfo, 0)
	ZEND_ARG_PASS_INFO(0) // key
	ZEND_ARG_PASS_INFO(0) // shardKey
	ZEND_ARG_PASS_INFO(1) // value
	ZEND_ARG_PASS_INFO(1) // flags
	ZEND_ARG_PASS_INFO(1) // cas = compare and swap token
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_unlock_arginfo, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO(memcache_unlock_arginfo1, 0)
	ZEND_ARG_PASS_INFO(0)
	ZEND_ARG_PASS_INFO(0)
ZEND_END_ARG_INFO();

/* {{{ memcache_functions[]
 */
zend_function_entry memcache_functions[] = {
	PHP_FE(memcache_connect,		NULL)
	PHP_FE(memcache_pconnect,		NULL)
	PHP_FE(memcache_add_server,		NULL)
	PHP_FE(memcache_set_server_params,		NULL)
	PHP_FE(memcache_get_server_status,		NULL)
	PHP_FE(memcache_get_version,	NULL)
	PHP_FE(memcache_addByKey,		NULL)
	PHP_FE(memcache_addMultiByKey,	memcache_add_arginfo)
	PHP_FE(memcache_add,			NULL)
	PHP_FE(memcache_set,			NULL)
	PHP_FE(memcache_setByKey,		NULL)
	PHP_FE(memcache_setMultiByKey,	memcache_set_arginfo)
	PHP_FE(memcache_replaceByKey,	NULL)
	PHP_FE(memcache_replaceMultiByKey, memcache_replace_arginfo)
	PHP_FE(memcache_replace,		NULL)
	PHP_FE(memcache_findserver,		memcache_findserver_arginfo)
	PHP_FE(memcache_get,			memcache_get_arginfo)
	PHP_FE(memcache_get2,			memcache_get2_arginfo)
	PHP_FE(memcache_getl,			memcache_getl_arginfo)
	PHP_FE(memcache_getlMultiByKey,		memcache_getlMultiByKey_arginfo)
	PHP_FE(memcache_unlock,			memcache_unlock_arginfo)
	PHP_FE(memcache_unlockMultiByKey,	NULL)
	PHP_FE(memcache_getByKey,		memcache_getByKey_arginfo)
	PHP_FE(memcache_getMultiByKey,	NULL)
	PHP_FE(memcache_casByKey,		memcache_cas_arginfo3)
	PHP_FE(memcache_casMultiByKey,	memcache_cas_arginfo)
	PHP_FE(memcache_cas,			memcache_cas_arginfo3)
	PHP_FE(memcache_delete,			NULL)
	PHP_FE(memcache_deleteByKey,	NULL)
	PHP_FE(memcache_deleteMultiByKey, NULL)
	PHP_FE(memcache_debug,			NULL)
	PHP_FE(memcache_get_stats,		NULL)
	PHP_FE(memcache_get_extended_stats,		NULL)
	PHP_FE(memcache_set_compress_threshold,	NULL)
	PHP_FE(memcache_incrementByKey,	NULL)
	PHP_FE(memcache_decrementByKey,	NULL)
	PHP_FE(memcache_increment,		NULL)
	PHP_FE(memcache_decrement,		NULL)
	PHP_FE(memcache_appendByKey,	NULL)
	PHP_FE(memcache_prependByKey,	NULL)
	PHP_FE(memcache_append,			NULL)
	PHP_FE(memcache_prepend,		NULL)
	PHP_FE(memcache_close,			NULL)
	PHP_FE(memcache_flush,			NULL)
	PHP_FE(memcache_setoptimeout,	NULL)
	PHP_FE(memcache_enable_proxy,	NULL)
	PHP_FE(memcache_setproperty,	NULL)
	PHP_FE(memcache_setlogname,		NULL)
	{NULL, NULL, NULL}
};

static zend_function_entry php_memcache_class_functions[] = {
	PHP_FALIAS(connect,			memcache_connect,			NULL)
	PHP_FALIAS(pconnect,		memcache_pconnect,			NULL)
	PHP_FALIAS(addserver,		memcache_add_server,		NULL)
	PHP_FALIAS(setserverparams,		memcache_set_server_params,		NULL)
	PHP_FALIAS(getserverstatus,		memcache_get_server_status,		NULL)
	PHP_FALIAS(getversion,		memcache_get_version,		NULL)
	PHP_FALIAS(addByKey,		memcache_addByKey,			NULL)
	PHP_FALIAS(addMultiByKey,	memcache_addMultiByKey,		memcache_add_arginfo1)
	PHP_FALIAS(add,				memcache_add,				NULL)
	PHP_FALIAS(set,				memcache_set,				NULL)
	PHP_FALIAS(setByKey,		memcache_setByKey,			NULL)
	PHP_FALIAS(setMultiByKey,	memcache_setMultiByKey,		memcache_set_arginfo1)
	PHP_FALIAS(replaceByKey,	memcache_replaceByKey,		NULL)
	PHP_FALIAS(replaceMultiByKey, memcache_replaceMultiByKey, memcache_replace_arginfo1)
	PHP_FALIAS(replace,			memcache_replace,			NULL)
	PHP_FALIAS(findserver,		memcache_findserver,	    memcache_findserver_arginfo1)
	PHP_FALIAS(get,				memcache_get,				memcache_get_arginfo1)
	PHP_FALIAS(get2,			memcache_get2,				get2_arginfo)
	PHP_FALIAS(getl,			memcache_getl,				memcache_getl_arginfo1)
	PHP_FALIAS(getlMultiByKey,		memcache_getlMultiByKey,			memcache_getlMultiByKey_arginfo1)
	PHP_FALIAS(unlock,			memcache_unlock,			memcache_unlock_arginfo1)
	PHP_FALIAS(unlockMultiByKey,		memcache_unlockMultiByKey,		NULL)
	PHP_FALIAS(getByKey,		memcache_getByKey,			getByKey_arginfo)
	PHP_FALIAS(getMultiByKey,	memcache_getMultiByKey,	    NULL)
	PHP_FALIAS(casByKey,		memcache_casByKey,			memcache_cas_arginfo4)
	PHP_FALIAS(casMultiByKey,	memcache_casMultiByKey,		memcache_cas_arginfo1)
	PHP_FALIAS(cas,				memcache_cas,				memcache_cas_arginfo4)
	PHP_FALIAS(delete,			memcache_delete,			NULL)
	PHP_FALIAS(deleteByKey,		memcache_deleteByKey,		NULL)
	PHP_FALIAS(deleteMultiByKey,memcache_deleteMultiByKey,	NULL)
	PHP_FALIAS(debug,			memcache_debug,				NULL)
	PHP_FALIAS(getstats,		memcache_get_stats,			NULL)
	PHP_FALIAS(getextendedstats,		memcache_get_extended_stats,		NULL)
	PHP_FALIAS(setcompressthreshold,	memcache_set_compress_threshold,	NULL)
	PHP_FALIAS(incrementByKey,	memcache_incrementByKey,	NULL)
	PHP_FALIAS(decrementByKey,	memcache_decrementByKey,	NULL)
	PHP_FALIAS(increment,		memcache_increment,			NULL)
	PHP_FALIAS(decrement,		memcache_decrement,			NULL)
	PHP_FALIAS(appendByKey,		memcache_appendByKey,		NULL)
	PHP_FALIAS(prependByKey,	memcache_prependByKey,		NULL)
	PHP_FALIAS(append,			memcache_append,			NULL)
	PHP_FALIAS(prepend,			memcache_prepend,			NULL)
	PHP_FALIAS(close,			memcache_close,				NULL)
	PHP_FALIAS(flush,			memcache_flush,				NULL)
	PHP_FALIAS(setoptimeout,	memcache_setoptimeout,		NULL)
	PHP_FALIAS(enableproxy,	memcache_enable_proxy,			NULL)
	PHP_FALIAS(setproperty,	memcache_setproperty,			NULL)
	PHP_FALIAS(setlogname,	memcache_setlogname,			NULL)
	{NULL, NULL, NULL}
};

/* }}} */

/* {{{ memcache_module_entry
 */
zend_module_entry memcache_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"memcache",
	memcache_functions,
	PHP_MINIT(memcache),
	PHP_MSHUTDOWN(memcache),
	PHP_RINIT(memcache),
	PHP_RSHUTDOWN(memcache),
	PHP_MINFO(memcache),
#if ZEND_MODULE_API_NO >= 20010901
	PHP_MEMCACHE_VERSION,
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_MEMCACHE
ZEND_GET_MODULE(memcache)
#endif

static PHP_INI_MH(OnUpdateChunkSize) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.chunk_size must be a positive integer ('%s' given)", new_value);
		return FAILURE;
	}

	return OnUpdateLong(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
}
/* }}} */

static PHP_INI_MH(OnUpdateFailoverAttempts) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.max_failover_attempts must be a positive integer ('%s' given)", new_value);
		return FAILURE;
	}

	return OnUpdateLong(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
}
/* }}} */

static PHP_INI_MH(OnUpdateHashStrategy) /* {{{ */
{
	if (!strcasecmp(new_value, "standard")) {
		MEMCACHE_G(hash_strategy) = MMC_STANDARD_HASH;
	}
	else if (!strcasecmp(new_value, "consistent")) {
		MEMCACHE_G(hash_strategy) = MMC_CONSISTENT_HASH;
	}
	else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.hash_strategy must be in set {standard, consistent} ('%s' given)", new_value);
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateHashFunction) /* {{{ */
{
	if (!strcasecmp(new_value, "crc32")) {
		MEMCACHE_G(hash_function) = MMC_HASH_CRC32;
	}
	else if (!strcasecmp(new_value, "fnv")) {
		MEMCACHE_G(hash_function) = MMC_HASH_FNV1A;
	}
	else {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.hash_function must be in set {crc32, fnv} ('%s' given)", new_value);
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateDefaultTimeout) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.default_timeout must be a positive number greater than or equal to 1 ('%s' given)", new_value);
		return FAILURE;
	}
	MEMCACHE_G(default_timeout_ms) = lval;
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateCompressionLevel) /* {{{ */
{
	long int lval;

	lval = strtol(new_value, NULL, 10);
	if (lval < 0 || lval > 9) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.compression_level must be >= 0 and <= 9 ");
		return FAILURE;
	}
	MEMCACHE_G(compression_level) = lval;
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateRetryInterval) /* {{{ */
{
	int lval;
	lval = strtol(new_value, NULL, 10);
	if (lval < 3 || lval > 30) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.retry_interval must be >= 3 and <= 30 ");
		return FAILURE;
	}
	MEMCACHE_G(retry_interval) = lval;
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateProxyHost) /* {{{ */
{
	if (new_value != NULL) {
		MEMCACHE_G(proxy_hostlen) = strlen(new_value);
		return OnUpdateString(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
	}
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateLogConf) /* {{{ */
{
	if (new_value != NULL) {
		return OnUpdateString(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
	}
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateIntegrityRetryCount) /* {{{ */
{
	int lval;
	lval = strtol(new_value, NULL, 10);
	if (lval < 0 || lval > 3) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.integrity_error_retry_count must be >= 0 and <= 3 ");
		return FAILURE;
	}
	MEMCACHE_G(integrity_error_retry_count) = lval;
	return SUCCESS;
}
/* }}} */

static PHP_INI_MH(OnUpdateIntegrityAlgo) /* {{{ */
{
	if (new_value != NULL) {
		if (strcmp(new_value, DI_CHKSUM_CRC_STR) == 0 || strcmp(new_value, DI_CHKSUM_OFF_STR) == 0) {
			return OnUpdateString(entry, new_value, new_value_length, mh_arg1, mh_arg2, mh_arg3, stage TSRMLS_CC);
		}
		else {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "memcache.integrity_algo must be one of {\"%s\", \"%s\"}, will continue to use %s ",
				DI_CHKSUM_CRC_STR, DI_CHKSUM_OFF_STR, MEMCACHE_G(integrity_algo));
			return FAILURE;
		}
	}
	return SUCCESS;
}
/* }}} */



/* {{{ PHP_INI */
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("memcache.allow_failover",	"0",		PHP_INI_ALL, OnUpdateLong,		allow_failover,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.max_failover_attempts",	"20",	PHP_INI_ALL, OnUpdateFailoverAttempts,		max_failover_attempts,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.default_port",		"11211",	PHP_INI_ALL, OnUpdateLong,		default_port,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.chunk_size",		"8192",		PHP_INI_ALL, OnUpdateChunkSize,	chunk_size,		zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.hash_strategy",		"standard",	PHP_INI_ALL, OnUpdateHashStrategy,	hash_strategy,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.hash_function",		"crc32",	PHP_INI_ALL, OnUpdateHashFunction,	hash_function,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.default_timeout_ms",	"10000",	PHP_INI_ALL, OnUpdateDefaultTimeout,	default_timeout_ms,	zend_memcache_globals,	memcache_globals)
	STD_PHP_INI_ENTRY("memcache.compression_level",	"0",	PHP_INI_ALL, OnUpdateCompressionLevel,	compression_level,	zend_memcache_globals,	memcache_globals)
   STD_PHP_INI_ENTRY("memcache.tcp_nodelay",       "1",        PHP_INI_ALL, OnUpdateBool, tcp_nodelay,     zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.null_on_keymiss",     "0",    PHP_INI_ALL, OnUpdateBool,  false_on_error,  zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.proxy_enabled",     "0",    PHP_INI_ALL, OnUpdateBool,  proxy_enabled,  zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.proxy_host",        NULL,   PHP_INI_ALL, OnUpdateProxyHost, proxy_host, zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.proxy_port",        "0",    PHP_INI_ALL, OnUpdateLong,  proxy_port, zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.connection_retry_count",        "0",    PHP_INI_ALL, OnUpdateLong,  connection_retry_count, zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.retry_interval",    "15",    PHP_INI_ALL, OnUpdateRetryInterval,  retry_interval, zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.log_conf",        NULL,   PHP_INI_ALL, OnUpdateLogConf, log_conf, zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.integrity_error_retry_count",     "3",    PHP_INI_ALL, OnUpdateLong,  integrity_error_retry_count,  zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.integrity_algo",     "crc32",    PHP_INI_ALL, OnUpdateIntegrityAlgo,  integrity_algo,  zend_memcache_globals,  memcache_globals)
   STD_PHP_INI_ENTRY("memcache.log_max_multi_op",    "50",    PHP_INI_ALL, OnUpdateLong, log_max_multi_op,  zend_memcache_globals,  memcache_globals)
PHP_INI_END()
/* }}} */

/* {{{ internal function protos */
static void _mmc_pool_list_dtor(zend_rsrc_list_entry * TSRMLS_DC);
static void _mmc_pserver_list_dtor(zend_rsrc_list_entry * TSRMLS_DC);

static void mmc_server_free(mmc_t * TSRMLS_DC);
static int mmc_server_store(mmc_t *, const char *, int, unsigned long & TSRMLS_DC);

static int mmc_compress(char **, unsigned long *, const char *, int, int TSRMLS_DC);
static int mmc_uncompress(char **, unsigned long *, const char *, int, int);
static int mmc_get_pool(zval *, mmc_pool_t ** TSRMLS_DC);
static int mmc_readline(mmc_t * TSRMLS_DC);
static char * mmc_get_version(mmc_t * TSRMLS_DC);
static int mmc_str_left(char *, char *, int, int);
static int mmc_sendcmd(mmc_t *, const char *, int TSRMLS_DC);
static int mmc_parse_response(mmc_t *mmc, char *, int, char **, int *, int *, unsigned long *, int *);
static int mmc_exec_retrieval_cmd_multi(mmc_pool_t *, zval *, zval **, zval **, zval *, zval * TSRMLS_DC);
static int mmc_read_value(mmc_t *, char **, int *, char **, int *, int *, unsigned long * TSRMLS_DC);
static int mmc_flush(mmc_t *, int TSRMLS_DC);
static void php_handle_store_command(INTERNAL_FUNCTION_PARAMETERS, char * command, int command_len, zend_bool by_key TSRMLS_DC);
static void php_handle_multi_store_command(INTERNAL_FUNCTION_PARAMETERS, char * command, int command_len TSRMLS_DC);
static void php_mmc_get(mmc_pool_t *pool, zval *zkey, char *key, zval **return_value, zval **status_array, zval *flags, zval *cas, bool return_corrupted);
static void php_mmc_getl(mmc_pool_t *pool, zval *zkey, zval **return_value, zval *flags, zval *cas, int timeout,  char *metadata, int &metadata_len);
static void php_mmc_unlock(mmc_pool_t *pool, zval *zkey, unsigned long cas, INTERNAL_FUNCTION_PARAMETERS);
static int mmc_get_stats(mmc_t *, char *, int, int, zval * TSRMLS_DC);
static int mmc_incr_decr(mmc_t *, int, char *, int, int, long * , mc_logger_t * TSRMLS_DC);
static void php_mmc_incr_decr(mmc_pool_t *pool, char *key, int key_len, char *shard_key, int shard_key_len, long value, zend_bool by_key, int increment_flag, zval **return_value);
static void php_mmc_connect(INTERNAL_FUNCTION_PARAMETERS, int);
static void mmc_init_multi(TSRMLS_D);
static void mmc_free_multi(TSRMLS_D);
static int php_mmc_get_by_key(mmc_pool_t *pool, zval *zkey, char *key, zval *zshardKey, zval *zvalue, zval *return_flags, zval *return_cas TSRMLS_DC);
static void php_mmc_get_multi_by_key(mmc_pool_t *pool, zval *zkey_array, zval **return_value TSRMLS_DC);
static void php_mmc_store_multi_by_key(zval * mmc_object, zval *zkey_array, char *command, int command_len, zval **return_value, zval **val_len TSRMLS_DC);
static int php_mmc_store(zval * mmc_object, char *key, int key_len, zval *value, int flags, int expire, unsigned long &cas, char *shard_key, int shard_key_len, char *command, int command_len, zend_bool by_key, zval *val_len);
static int php_mmc_delete_by_key(mmc_pool_t *pool, char *key, int key_len, char *shard_key, int shard_key_len, int time TSRMLS_DC);
static void php_mmc_delete_multi_by_key(mmc_pool_t *pool, zval *zkey_array, int time, zval **return_value TSRMLS_DC);
static int mmc_str(char *haystack, char *needle, int haystack_len, int needle_len);

/* }}} */

/* {{{ hash strategies */
extern mmc_hash_t mmc_standard_hash;
extern mmc_hash_t mmc_consistent_hash;
/* }}} */

/* {{{ php_memcache_init_globals()
*/
static void php_memcache_init_globals(zend_memcache_globals *memcache_globals_p TSRMLS_DC)
{
	MEMCACHE_G(debug_mode)		  = 0;
	MEMCACHE_G(num_persistent)	  = 0;
	MEMCACHE_G(compression_level) = 0;
	MEMCACHE_G(hash_strategy)	  = MMC_STANDARD_HASH;
	MEMCACHE_G(hash_function)	  = MMC_HASH_CRC32;
	MEMCACHE_G(default_timeout_ms)= MMC_DEFAULT_TIMEOUT;
	MEMCACHE_G(tcp_nodelay)       = 1;
	MEMCACHE_G(proxy_enabled)     = 0;
	MEMCACHE_G(false_on_error)    = 0;
	MEMCACHE_G(proxy_host)        = NULL;
	MEMCACHE_G(proxy_hostlen)     = 0;
	MEMCACHE_G(proxy_port)        = 0;
	MEMCACHE_G(connection_retry_count) = 0;
	MEMCACHE_G(integrity_error_retry_count) = 3;
	MEMCACHE_G(integrity_algo) = DI_CHKSUM_CRC_STR;
}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(memcache)
{
	zend_class_entry memcache_class_entry;
	INIT_CLASS_ENTRY(memcache_class_entry, "Memcache", php_memcache_class_functions);
	memcache_class_entry_ptr = zend_register_internal_class(&memcache_class_entry TSRMLS_CC);

	le_memcache_pool = zend_register_list_destructors_ex(_mmc_pool_list_dtor, NULL, "memcache connection", module_number);
	le_pmemcache = zend_register_list_destructors_ex(NULL, _mmc_pserver_list_dtor, "persistent memcache connection", module_number);

#ifdef ZTS
	ts_allocate_id(&memcache_globals_id, sizeof(zend_memcache_globals), (ts_allocate_ctor) php_memcache_init_globals, NULL);
#else
	php_memcache_init_globals(&memcache_globals TSRMLS_CC);
#endif

	REGISTER_LONG_CONSTANT("MEMCACHE_COMPRESSED", MMC_COMPRESSED, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MEMCACHE_COMPRESSED_LZO", MMC_COMPRESSED_LZO, CONST_CS | CONST_PERSISTENT);
	REGISTER_LONG_CONSTANT("MEMCACHE_COMPRESSED_BZIP2", MMC_COMPRESSED_BZIP2, CONST_CS | CONST_PERSISTENT);

#ifdef HAVE_MEMCACHE_IGBINARY
	REGISTER_LONG_CONSTANT("MEMCACHE_SERIALIZED_IGBINARY", MMC_SERIALIZED_IGBINARY, CONST_CS | CONST_PERSISTENT);
#endif

	REGISTER_INI_ENTRIES();

#if HAVE_MEMCACHE_SESSION
	REGISTER_LONG_CONSTANT("MEMCACHE_HAVE_SESSION", 1, CONST_CS | CONST_PERSISTENT);
	php_session_register_module(ps_memcache_ptr);
#else
	REGISTER_LONG_CONSTANT("MEMCACHE_HAVE_SESSION", 0, CONST_CS | CONST_PERSISTENT);
#endif
	memcache_lzo_enabled = (lzo_init() == LZO_E_OK)? 1: 0;
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(memcache)
{
#ifdef ZTS
	ts_free_id(memcache_globals_id);
#else
	php_memcache_destroy_globals(&memcache_globals TSRMLS_CC);
#endif

	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(memcache)
{
	const char *err;
	//zval **data;
	MEMCACHE_G(debug_mode) = 0;
	MEMCACHE_G(in_multi) = 0;
	MEMCACHE_G(proxy_connect_failed) = 0;
	if ((err = LogManager::checkAndLoadConfig(MEMCACHE_G(log_conf)))) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error in expr parsing %s in file %s. Please fix the error to logging to work", err, MEMCACHE_G(log_conf));
	}

	maxLoggerSize = MEMCACHE_G(log_max_multi_op);
	RequestLogger::instance()->initialize();
	return SUCCESS;
}
/* }}} */

/*{{{
*/
PHP_RSHUTDOWN_FUNCTION(memcache)
{
	RequestLogger::instance()->finalize(SG(request_info).request_uri);
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(memcache)
{
	char buf[MAX_LENGTH_OF_LONG + 1];

	sprintf(buf, "%ld", MEMCACHE_G(num_persistent));

	php_info_print_table_start();
	php_info_print_table_header(2, "memcache support", "enabled");
	php_info_print_table_row(2, "Active persistent connections", buf);
	php_info_print_table_row(2, "LZO compression", memcache_lzo_enabled? "enabled": "disabled");
#ifdef HAVE_BZ2
	php_info_print_table_row(2, "BZip2 compression", "enabled");
#endif
#ifdef HAVE_MEMCACHE_IGBINARY
	php_info_print_table_row(2, "Igbinary serialization", "enabled");
#endif
	php_info_print_table_row(2, "Version", PHP_MEMCACHE_VERSION);
	php_info_print_table_row(2, "Revision", "$Revision: 23976 $");
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */

/* ------------------
   internal functions
   ------------------ */

#ifdef ZEND_DEBUG
void mmc_debug(const char *format, ...) /* {{{ */
{
	TSRMLS_FETCH();

	if (MEMCACHE_G(debug_mode)) {
		char buffer[1024];
		va_list args;

		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer)-1, format, args);
		va_end(args);
		buffer[sizeof(buffer)-1] = '\0';
		php_printf("DEBUG: %s<br />\n", buffer);
	}
}
/* }}} */
#endif

static struct timeval _convert_timeoutms_to_ts(long msecs) /* {{{ */
{
	struct timeval tv;
	int secs = 0;

	secs = msecs / 1000;
	tv.tv_sec = secs;
	tv.tv_usec = ((msecs - (secs * 1000)) * 1000) % 1000000;
	return tv;
}
/* }}} */

static void _mmc_pool_list_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) /* {{{ */
{
	mmc_pool_free((mmc_pool_t *)rsrc->ptr TSRMLS_CC);
}
/* }}} */

static void _mmc_pserver_list_dtor(zend_rsrc_list_entry *rsrc TSRMLS_DC) /* {{{ */
{
	mmc_server_free((mmc_t *)rsrc->ptr TSRMLS_CC);
}
/* }}} */

mmc_t *mmc_server_new(char *host, int host_len, unsigned short port, int persistent, int timeout, int retry_interval, int use_binary TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc = (mmc_t *)pemalloc(sizeof(mmc_t), persistent);
	memset(mmc, 0, sizeof(*mmc));

	mmc->host = (char *)pemalloc(host_len + 1, persistent);
	memcpy(mmc->host, host, host_len);
	mmc->host[host_len] = '\0';

	mmc->proxy_str = (char *)pemalloc(4 + host_len + 1 + MAX_LENGTH_OF_LONG + 1, persistent);
	sprintf(mmc->proxy_str, "%c:%s:%d ", use_binary ? 'B' : 'A', host, port);
	mmc->proxy_str_len = strlen(mmc->proxy_str);

	mmc->port = port;
	mmc->status = MMC_STATUS_DISCONNECTED;

	mmc->persistent = persistent;
	if (persistent) {
		MEMCACHE_G(num_persistent)++;
	}

	mmc->timeout = timeout;
	mmc->timeoutms = timeout * 1000;
	mmc->connect_timeoutms = timeout * 1000;
	mmc->data_integrity_algo = DI_CHKSUM_UNSUPPORTED;
	mmc->di_algo_in_use = DI_CHKSUM_UNSUPPORTED;
	mmc->got_options = false;
	mmc->retry_interval = retry_interval;

	return mmc;
}
/* }}} */

static void mmc_server_callback_dtor(zval **callback TSRMLS_DC) /* {{{ */
{
	zval **this_obj;

	if (!callback || !*callback) return;

	if (Z_TYPE_PP(callback) == IS_ARRAY &&
		zend_hash_index_find(Z_ARRVAL_PP(callback), 0, (void **)&this_obj) == SUCCESS &&
		Z_TYPE_PP(this_obj) == IS_OBJECT) {
		zval_ptr_dtor(this_obj);
	}
	zval_ptr_dtor(callback);
}
/* }}} */

static void mmc_server_callback_ctor(zval **callback TSRMLS_DC) /* {{{ */
{
	zval **this_obj;

	if (!callback || !*callback) return;

	if (Z_TYPE_PP(callback) == IS_ARRAY &&
		zend_hash_index_find(Z_ARRVAL_PP(callback), 0, (void **)&this_obj) == SUCCESS &&
		Z_TYPE_PP(this_obj) == IS_OBJECT) {
		zval_add_ref(this_obj);
	}
	zval_add_ref(callback);
}
/* }}} */

static void mmc_server_sleep(mmc_t *mmc TSRMLS_DC) /*
	prepare server struct for persistent sleep {{{ */
{
	mmc_server_callback_dtor(&mmc->failure_callback TSRMLS_CC);
	mmc->failure_callback = NULL;

	if (mmc->error != NULL) {
		pefree(mmc->error, mmc->persistent);
		mmc->error = NULL;
	}
}
/* }}} */

static void mmc_server_free(mmc_t *mmc TSRMLS_DC) /* {{{ */
{
	if (mmc->in_free) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Recursive reference detected, bailing out");
		return;
	}
	mmc->in_free = 1;

	mmc_server_sleep(mmc TSRMLS_CC);

	if (mmc->persistent) {
		free(mmc->host);
		free(mmc->proxy_str);
		free(mmc);
		MEMCACHE_G(num_persistent)--;
	}
	else {
		if (mmc->stream != NULL) {
		    php_stream_close(mmc->stream);
		}
		efree(mmc->host);
		efree(mmc->proxy_str);
		efree(mmc);
	}
}
/* }}} */

static void mmc_server_seterror(mmc_t *mmc, const char *error, int errnum) /* {{{ */
{
	if (error != NULL) {
		if (mmc->error != NULL) {
			pefree(mmc->error, mmc->persistent);
		}

		mmc->error = pestrdup(error, mmc->persistent);
		mmc->errnum = errnum;
	}
}
/* }}} */

static void mmc_server_received_error(mmc_t *mmc, int response_len)  /* {{{ */
{
	if (mmc_str_left(mmc->inbuf, "ERROR", response_len, sizeof("ERROR") - 1)) {
		mmc->inbuf[response_len < MMC_BUF_SIZE - 1 ? response_len : MMC_BUF_SIZE - 1] = '\0';
		mmc_server_seterror(mmc, mmc->inbuf, 0);
	}
	if (mmc_str_left(mmc->inbuf, "CLIENT_ERROR", response_len, sizeof("CLIENT_ERROR") - 1))
	{
		mmc->inbuf[response_len < MMC_BUF_SIZE - 1 ? response_len : MMC_BUF_SIZE - 1] = '\0';
		mmc_server_seterror(mmc, mmc->inbuf, 0);
	}
	if (mmc_str_left(mmc->inbuf, "SERVER_ERROR", response_len, sizeof("SERVER_ERROR") - 1))
	{
		mmc->inbuf[response_len < MMC_BUF_SIZE - 1 ? response_len : MMC_BUF_SIZE - 1] = '\0';
		mmc_server_seterror(mmc, mmc->inbuf, 0);
	}
	else if (mmc_str_left(mmc->inbuf, "NOT_FOUND", response_len, sizeof("NOT_FOUND") - 1))
	{
		mmc_server_seterror(mmc, "Key does not exist or the Queue specified is not a valid one", 0);
	}
	else {
		mmc_server_seterror(mmc, "Received malformed response", 0);
	}
}
/* }}} */

int mmc_server_failure(mmc_t *mmc TSRMLS_DC) /*determines if a request should be retried or is a hard network failure {{{ */
{
	if (mmc->proxy) {
		mmc_server_disconnect(mmc->proxy TSRMLS_CC);
		mmc_server_deactivate(mmc TSRMLS_CC);
		return 1;
	}

	switch (mmc->status) {
		case MMC_STATUS_DISCONNECTED:
			return 0;

		/* attempt reconnect of sockets in unknown state */
		case MMC_STATUS_UNKNOWN:
			mmc->status = MMC_STATUS_DISCONNECTED;
			return 0;
	}

	mmc_server_deactivate(mmc TSRMLS_CC);
	return 1;
}
/* }}} */

static int get_options(mmc_pool_t *pool, mmc_t *mmc)
{
	char options[MAX_OPTIONS_LEN];
	int options_len, response_len;
	php_stream *stream;
	char *options_start, *options_end, *di_options_start;

	options_len = sprintf(options, "options ");
	options_len += sprintf(options + options_len, "version=%s DIAlgo=%s\r\n",
		PHP_MEMCACHE_VERSION, (pool->enable_checksum == 1) ? MEMCACHE_G(integrity_algo) : DI_CHKSUM_OFF_STR);

	if (mmc->proxy) {
		stream = mmc->proxy->stream;
		if (php_stream_write(stream, mmc->proxy_str, mmc->proxy_str_len) != mmc->proxy_str_len) {
		    mmc_server_seterror(mmc, "Failed sending proxy string", 0);
		    return -1;
		}
	} else {
		stream = mmc->stream;
	}

	php_netstream_data_t *sock = (php_netstream_data_t*)stream->abstract;

	if (mmc->timeoutms > 1) {
		sock->timeout = _convert_timeoutms_to_ts(mmc->timeoutms);
	}

	if (php_stream_write(stream, options, options_len) != options_len) {
		mmc_server_seterror(mmc, "Failed sending command and value to stream", 0);
		return -1;
	}

	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0) {
		return -1;
	}

    if (mmc_str_left(mmc->inbuf, "SERVER_ERROR", response_len, sizeof("SERVER_ERROR") - 1)) {
		return -1;
	}

	mmc->data_integrity_algo = DI_CHKSUM_UNSUPPORTED;
	options_start = mmc->inbuf;
	options_end = mmc->inbuf + response_len - 2;
	// Check for Data Integrity algorithm in the options
	if(mmc_str_left(options_start, "options", response_len, sizeof("options") - 1) &&
		(di_options_start = php_memnstr(options_start, "DIAlgo", sizeof("DIAlgo") -1, options_end)) != NULL) {
		int di_options_len = options_end - di_options_start;
		char *di_algo = php_memnstr(di_options_start, "=", 1, di_options_start + di_options_len);
		if (di_algo != NULL && php_memnstr(di_algo, DI_CHKSUM_CRC_STR,
					sizeof(DI_CHKSUM_CRC_STR) - 1, di_options_start + di_options_len) != NULL) {
			mmc->data_integrity_algo |= DI_CHKSUM_CRC;
		}
		else {
			mmc->data_integrity_algo |= DI_CHKSUM_SUPPORTED_OFF;
		}
	}

	return 0;
}

mmc_t * mmc_pool_find(mmc_pool_t *pool, const char *key, int key_len) {
	mmc_t *mmc = pool->hash->find_server(pool->hash_state, key, key_len, pool->proxy_enabled);
	if (mmc == NULL || mmc->status == MMC_STATUS_FAILED || (pool->proxy_enabled && mmc->proxy == NULL))
		return mmc;

	// Why chek for proxy->got_options here? we (usually) only have a single connection to the
	// proxy that gets used to talk to all Membases. But in multiget, we open a separate connection
	// to the proxy for each key. We want to send options command on every one of these
	// connections (so that mcmux knows pecl understands checksums).
	if(mmc->got_options == false || (pool->proxy_enabled && mmc->proxy->got_options == false)) {
		if (get_options(pool, mmc) >= 0) {
			mmc->got_options = true;
			if (pool->proxy_enabled)
				mmc->proxy->got_options = true;
		}
		else {
			mmc_server_failure(mmc TSRMLS_CC);
		}
	}

	if (pool->enable_checksum != 0) {
		mmc->di_algo_in_use = mmc->data_integrity_algo;
	}
	else {
		// The pool settings say checksum are off, the final settings will be OFF or unsupported,
		// depending on what the downstream says
		if (mmc->data_integrity_algo == DI_CHKSUM_UNSUPPORTED) {
			mmc->di_algo_in_use = DI_CHKSUM_UNSUPPORTED;
		}
		else {
			mmc->di_algo_in_use = DI_CHKSUM_SUPPORTED_OFF;
		}
	}
	MMC_DEBUG(("Pool says checksums = %d, downstream says %d final answer %d \n", pool->enable_checksum,
		mmc->data_integrity_algo, mmc->di_algo_in_use));
	return mmc;
}


static int mmc_server_store(mmc_t *mmc, const char *request, int request_len, unsigned long &cas TSRMLS_DC) /* {{{ */
{
	int response_len;
	php_stream *stream;

	LogManager::getLogger()->setHost(mmc->host);

	if (mmc->proxy) {
		stream = mmc->proxy->stream;
		if (php_stream_write(stream, mmc->proxy_str, mmc->proxy_str_len) != mmc->proxy_str_len) {
		    mmc_server_seterror(mmc, "Failed sending proxy string", 0);
		    return -1;
		}
	} else {
		stream = mmc->stream;
	}

	php_netstream_data_t *sock = (php_netstream_data_t*)stream->abstract;

	if (mmc->timeoutms > 1) {
		sock->timeout = _convert_timeoutms_to_ts(mmc->timeoutms);
	}


	if (php_stream_write(stream, request, request_len) != request_len) {
		mmc_server_seterror(mmc, "Failed sending command and value to stream", 0);
		return -1;
	}

	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0) {
		return -1;
	}

	if(mmc_str_left(mmc->inbuf, "STORED", response_len, sizeof("STORED") - 1)) {
		LogManager::getLogger()->setCode(MC_STORED);
		cas = strtoul(mmc->inbuf+sizeof("STORED"), NULL, 10);
		return 1;
	}

	/* return FALSE */
	if(mmc_str_left(mmc->inbuf, "NOT_STORED", response_len, sizeof("NOT_STORED") - 1)) {
		LogManager::getLogger()->setCode(MC_NOT_STORED);
		return 0;
	}

	if(mmc_str_left(mmc->inbuf, "NOT_FOUND", response_len, sizeof("NOT_FOUND") - 1)) {
		LogManager::getLogger()->setCode(MC_NOT_FOUND);
		return 0;
	}

	if(mmc_str_left(mmc->inbuf, "EXISTS", response_len, sizeof("EXISTS") - 1)) {
		LogManager::getLogger()->setCode(MC_EXISTS);
		return 0;
	}

	if (mmc_str(mmc->inbuf, "temporary failure", response_len, sizeof("temporary failure") - 1)) {
		LogManager::getLogger()->setCode(MC_TMP_FAIL);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to perform operation, temporary failure. Item may be locked or server out of memory");
		return 0;
	}

	/* return FALSE without failover */
	if (mmc_str_left(mmc->inbuf, "SERVER_ERROR out of memory", response_len, sizeof("SERVER_ERROR out of memory") - 1) ||
		mmc_str_left(mmc->inbuf, "SERVER_ERROR object too large", response_len, sizeof("SERVER_ERROR object too large")-1)) {
		LogManager::getLogger()->setCode(MC_SERVER_MEM_ERROR);
		return 0;
	}

	/* object may be locked, return FALSE without failover */
	if (mmc_str_left(mmc->inbuf, "LOCK_ERROR", response_len, sizeof("LOCK_ERROR") - 1)) {
		LogManager::getLogger()->setCode(MC_LOCK_ERROR);
		return 0;
	}

	/* data integrity error, retry for integrity_error_retry_count times */
	if (mmc_str_left(mmc->inbuf, "SERVER_ERROR checksum failed", response_len, sizeof("SERVER_ERROR checksum failed") - 1)) {
		return -2;
	}


	mmc_server_received_error(mmc, response_len);
	return -1;
}
/* }}} */

int mmc_prepare_key_ex(const char *key, unsigned int key_len, char *result, unsigned int *result_len TSRMLS_DC)  /* {{{ */
{
	unsigned int i;
	if (key_len == 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Key cannot be empty");
		return MMC_REQUEST_FAILURE;
	}

	*result_len = key_len < MMC_KEY_MAX_SIZE ? key_len : MMC_KEY_MAX_SIZE;
	result[*result_len] = '\0';

	for (i=0; i<*result_len; i++) {
		result[i] = ((unsigned char)key[i]) > ' ' ? key[i] : '_';
	}

	return MMC_OK;
}
/* }}} */

int mmc_prepare_key(zval *key, char *result, unsigned int *result_len TSRMLS_DC)  /* {{{ */
{
	if (Z_TYPE_P(key) == IS_STRING) {
		return mmc_prepare_key_ex(Z_STRVAL_P(key), Z_STRLEN_P(key), result, result_len TSRMLS_CC);
	} else {
		int res;
		zval *keytmp;
		ALLOC_ZVAL(keytmp);

		*keytmp = *key;
		zval_copy_ctor(keytmp);
		convert_to_string(keytmp);

		res = mmc_prepare_key_ex(Z_STRVAL_P(keytmp), Z_STRLEN_P(keytmp), result, result_len TSRMLS_CC);

		zval_dtor(keytmp);
		FREE_ZVAL(keytmp);

		return res;
	}
}
/* }}} */

static unsigned int mmc_hash_crc32(const char *key, int key_len) /* CRC32 hash {{{ */
{
	unsigned int crc = ~0;
	int i;

	for (i=0; i<key_len; i++) {
		CRC32(crc, key[i]);
	}

  	return ~crc;
}
/* }}} */

static unsigned int mmc_hash_fnv1a(const char *key, int key_len) /* FNV-1a hash {{{ */
{
	unsigned int hval = FNV_32_INIT;
	int i;

	for (i=0; i<key_len; i++) {
		hval ^= (unsigned int)key[i];
		hval *= FNV_32_PRIME;
	}

	return hval;
}
/* }}} */

static void mmc_pool_init_hash(mmc_pool_t *pool TSRMLS_DC) /* {{{ */
{
	mmc_hash_function hash;

	switch (MEMCACHE_G(hash_strategy)) {
		case MMC_CONSISTENT_HASH:
			pool->hash = &mmc_consistent_hash;
			break;
		default:
			pool->hash = &mmc_standard_hash;
	}

	switch (MEMCACHE_G(hash_function)) {
		case MMC_HASH_FNV1A:
			hash = &mmc_hash_fnv1a;
			break;
		default:
			hash = &mmc_hash_crc32;
	}

	pool->hash_state = pool->hash->create_state(hash);
}
/* }}} */

mmc_pool_t *mmc_pool_new(TSRMLS_D) /* {{{ */
{
	mmc_pool_t *pool = (mmc_pool_t*)emalloc(sizeof(mmc_pool_t));
	pool->num_servers = 0;
	pool->compress_threshold = 0;
	pool->in_free = 0;
	pool->min_compress_savings = MMC_DEFAULT_SAVINGS;
	pool->proxy_enabled = MEMCACHE_G(proxy_enabled);
	pool->false_on_error = MEMCACHE_G(false_on_error);
	pool->log_name = pestrdup("default", 1);

	ALLOC_INIT_ZVAL(pool->cas_array);
	array_init(pool->cas_array);
	pool->enable_checksum = 0;

	mmc_pool_init_hash(pool TSRMLS_CC);

	return pool;
}
/* }}} */

void mmc_pool_free(mmc_pool_t *pool TSRMLS_DC) /* {{{ */
{
	int i;

	if (pool->in_free) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Recursive reference detected, bailing out");
		return;
	}
	pool->in_free = 1;

	for (i=0; i<pool->num_servers; i++) {
		if (!pool->servers[i]) {
			continue;
		}

		if (pool->servers[i]->persistent == 0 && pool->servers[i]->host != NULL) {
			mmc_server_free(pool->servers[i] TSRMLS_CC);
		} else {
			mmc_server_sleep(pool->servers[i] TSRMLS_CC);
		}

		pool->servers[i] = NULL;

	}

	if (pool->cas_array) {
		zval_ptr_dtor(&(pool->cas_array));
	}

	if (pool->num_servers) {
		efree(pool->servers);
		efree(pool->requests);
	}

	if (pool->log_name) {
		pefree(pool->log_name, 1);
		pool->log_name = NULL;
	}

	pool->hash->free_state(pool->hash_state);
	efree(pool);
}
/* }}} */

void mmc_pool_add(mmc_pool_t *pool, mmc_t *mmc, unsigned int weight) /* {{{ */
{
	/* add server and a preallocated request pointer */
	if (pool->num_servers) {
		pool->servers = (mmc_t**)erealloc(pool->servers, sizeof(mmc_t *) * (pool->num_servers + 1));
		pool->requests = (mmc_t**)erealloc(pool->requests, sizeof(mmc_t *) * (pool->num_servers + 1));
	}
	else {
		pool->servers = (mmc_t**)emalloc(sizeof(mmc_t *));
		pool->requests = (mmc_t**)emalloc(sizeof(mmc_t *));
	}

	pool->servers[pool->num_servers] = mmc;
	pool->num_servers++;

	pool->hash->add_server(pool->hash_state, mmc, weight);
}
/* }}} */

static int mmc_pool_close(mmc_pool_t *pool TSRMLS_DC) /* disconnects and removes all servers in the pool {{{ */
{
	if (pool->num_servers) {
		int i;

		for (i=0; i<pool->num_servers; i++) {
			if (pool->servers[i]->persistent == 0 && pool->servers[i]->host != NULL) {
				mmc_server_free(pool->servers[i] TSRMLS_CC);
			} else {
				mmc_server_sleep(pool->servers[i] TSRMLS_CC);
			}
		}

		efree(pool->servers);
		pool->servers = NULL;
		pool->num_servers = 0;

		efree(pool->requests);
		pool->requests = NULL;

		/* reallocate the hash strategy state */
		pool->hash->free_state(pool->hash_state);
		mmc_pool_init_hash(pool TSRMLS_CC);
	}

	return 1;
}
/* }}} */

static unsigned int checksum_crc32(pecl_string_t *s, int count) /* CRC32 hash {{{ */
{
	unsigned int crc = ~0;
	int i, p;

	for (p=0; p<count; p++) {
		for (i=0; i<s[p].len; i++) {
			CRC32(crc, s[p].data[i]);
		}
	}

  	return ~crc;
}

static inline unsigned int get_checksum_extra(const char *value, int value_len, int flags) {
	pecl_string_t s[2];
	// For new style crc, we also need to include the flags in the compressed checksum
	s[0].data = (char *)&flags;
	s[0].len = sizeof(flags);
	s[1].data = (char *)value;
	s[1].len = value_len;
	return checksum_crc32(s, sizeof(s) / sizeof(s[0]));
}

static inline unsigned int get_checksum(const char *value, int value_len) {
	pecl_string_t s;
	s.data = (char *)value;
	s.len = value_len;
	return checksum_crc32(&s, 1);
}

int mmc_pool_store(mmc_pool_t *pool, const char *command, int command_len, const char *key, int key_len, int flags, int expire, unsigned long &cas, const char *value, int value_len, zend_bool by_key, const char *shard_key, int shard_key_len, zval *val_len  TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc;
	char *request;
	int request_len, result = -1;
	char *key_copy = NULL, *data = NULL, *shard_key_copy = NULL;
	unsigned long **cas_lookup;
	char add_new_crc, calc_crc;
	unsigned int new_crc_len ;
	unsigned int crc32 = 0;		// crc header of the compressed data
	unsigned int uncrc32 = 0;	// crc header of the uncompressed data
	char crc_in_hdr[MAX_CRC_BUF] = {0};
	char crc_in_val[MAX_CRC_BUF] = {0};
	int retry_count = 0;
	int len_crc_in_hdr = 0, len_crc_in_val = 0, cas_key_len = 0;
	char *cas_key = NULL;

	MMC_DEBUG(("mmc_pool_store: key '%s' len '%d'", key, key_len));

	while (result < 0) {
		// We want to find the server before we do anything else.
		// Because only the server can tell us what kind of checksum is needed.
		if (by_key) {
			mmc = mmc_pool_find(pool, shard_key, shard_key_len TSRMLS_CC);
		} else {
			mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC);
		}

		if(mmc == NULL || mmc->status == MMC_STATUS_FAILED) {
			MMC_DEBUG(("mmc_pool_store: mmc is null"));
			LogManager::getLogger()->setCode(CONNECT_FAILED);
			break;
		}

		if (key_len > MMC_KEY_MAX_SIZE) {
			key = key_copy = estrndup(key, MMC_KEY_MAX_SIZE);
			key_len = MMC_KEY_MAX_SIZE;
		}

		if (by_key) {
			if (shard_key_len > MMC_KEY_MAX_SIZE) {
				shard_key = shard_key_copy = estrndup(shard_key, MMC_KEY_MAX_SIZE);
				shard_key_len = MMC_KEY_MAX_SIZE;
			}
			cas_key = new char[shard_key_len+key_len+1];
			memcpy(cas_key, key, key_len);
			memcpy(cas_key+key_len, shard_key, shard_key_len+1);
			cas_key_len = key_len+shard_key_len+1;
		} else {
			cas_key = (char *)key;
			cas_key_len = key_len+1;
		}

		/* autocompress large values */
		/* skip compression when the command is either append or prepend */
		if (!(strncmp(command, "append", command_len) && strncmp(command, "prepend", command_len))) {
			flags = 0;
		} else if (pool->compress_threshold && value_len >= pool->compress_threshold &&
				!(flags & (MMC_COMPRESSED | MMC_COMPRESSED_LZO | MMC_COMPRESSED_BZIP2))) {
			flags |= (MEMCACHE_G(compression_level) > 0)? MMC_COMPRESSED: MMC_COMPRESSED_LZO;
		}

		add_new_crc = 0;
		calc_crc = 0;
		add_new_crc = (mmc->di_algo_in_use & DI_CHKSUM_CRC) || (mmc->di_algo_in_use & DI_CHKSUM_SUPPORTED_OFF);
		// Add old style CRC (before the blob) only if data integrity is enabled but new style checksum is NOT supported (maybe because mcmux or MB is old)
		// We need to calculate CRC if the feature is enabled AND (new style CRC is required OR old style CRC is required)
		calc_crc = mmc->di_algo_in_use & DI_CHKSUM_CRC;
		MMC_DEBUG(("add_new_crc set to %d \n", add_new_crc));

		// For the new style checksum, we want to protect flags using the checksum.
		// If the data is compressed, include flags in the compressed checksum.
		// Else, include the flags in uncompressed checksum

		if ((flags & MMC_COMPRESSED) || (flags & MMC_COMPRESSED_LZO) || (flags & MMC_COMPRESSED_BZIP2)) {
			unsigned long data_len;

			if (!memcache_lzo_enabled) {
				flags &= ~MMC_COMPRESSED_LZO;

				if(!(flags & MMC_COMPRESSED_BZIP2)) {
					flags |= MMC_COMPRESSED;
				}
			}

			if (!mmc_compress(&data, &data_len, value, value_len, flags TSRMLS_CC)) {
				LogManager::getLogger()->setCode(COMPRESS_FAILED);
				/* mmc_server_seterror(mmc, "Failed to compress data", 0); */
				return -1;
			}

			/* was enough space saved to motivate uncompress processing on get */
			if (data_len < value_len * (1 - pool->min_compress_savings)) {
				if (calc_crc) {
					uncrc32 = get_checksum(value, value_len);
					// New style CRC - include the flags in the checksum
					crc32 = (add_new_crc) ? get_checksum_extra(data, data_len, flags) :
								get_checksum(data, data_len);
				}
				value = data;
				value_len = data_len;
			}
			else {
				flags &= ~(MMC_COMPRESSED | MMC_COMPRESSED_LZO | MMC_COMPRESSED_BZIP2);
				efree(data);
				data = NULL;
			}
		}

		//compute crc of the uncompressed data (if not done already above)
		if (calc_crc && uncrc32 == 0) {
			// New style CRC - include flags in the checksum
			if (add_new_crc) {
				uncrc32 = get_checksum_extra(value, value_len, flags);
			}
			else {
				uncrc32 = get_checksum(value, value_len);
			}
		}

		// Calculate the crc and format it
		if (add_new_crc) {
			int chksum_metadata = mmc->di_algo_in_use;
			if (mmc->di_algo_in_use & DI_CHKSUM_SUPPORTED_OFF) {
				len_crc_in_hdr = snprintf(crc_in_hdr, MAX_CRC_BUF,  "%.4x:", chksum_metadata);
			}
			else if (crc32)  {
				len_crc_in_hdr = snprintf(crc_in_hdr, MAX_CRC_BUF,  "%.4x:%.8x:%.8x", chksum_metadata, crc32, uncrc32);
			} else {
				len_crc_in_hdr = snprintf(crc_in_hdr, MAX_CRC_BUF, "%.4x:%.8x", chksum_metadata, uncrc32);
			}
		}


		/*
		 * if no cas value has been specified, check if we have one stored for this key
		 * only set operations will be converted to cas
		 */

		if (pool->cas_array && command[0] == 's') {
			if (FAILURE != zend_hash_find(Z_ARRVAL_P(pool->cas_array), (char *)cas_key, strlen(cas_key)+1,
					(void**)&cas_lookup)) {
				cas = **cas_lookup ;
				zend_hash_del(Z_ARRVAL_P(pool->cas_array), (char *)key, key_len + 1);
			}
		}

		int caslen = (cas != 0)? MAX_LENGTH_OF_LONG + 1: 0;

		request = (char *)emalloc(
				command_len
				+ 1 /* space */
				+ key_len
				+ 1 /* space */
				+ MAX_LENGTH_OF_LONG
				+ 1 /* space */
				+ MAX_LENGTH_OF_LONG
				+ 1 /* space */
				+ MAX_LENGTH_OF_LONG
				+ 1 /* space */
				+ caslen
				+ sizeof("\r\n") - 1
				+ value_len
				+ sizeof("\r\n") - 1
				+ 1
				+ len_crc_in_hdr
				);

		if (caslen) {
			request_len = sprintf(request, "cas %s %d %d %d %s %lu returncas\r\n",
					key, flags, expire, value_len, len_crc_in_hdr ? crc_in_hdr : "", cas);
		} else {
			request_len = sprintf(request, "%s %s %d %d %d %s\r\n",
					command, key, flags, expire, value_len, len_crc_in_hdr ? crc_in_hdr : "");
		}

		if (len_crc_in_val) {
			memcpy(request + request_len, crc_in_val, len_crc_in_val);
		}
		memcpy(request + request_len + len_crc_in_val, value, value_len - len_crc_in_val);

		LogManager::getLogger()->setCas(cas);

		request_len += value_len;

		memcpy(request + request_len, "\r\n", sizeof("\r\n") - 1);
		request_len += sizeof("\r\n") - 1;

		request[request_len] = '\0';

		if (val_len) {
			ZVAL_LONG(val_len, value_len);
		}

		MMC_DEBUG(("mmc_pool_store: Sending request '%s'", request));

		do {
			result = mmc_server_store(mmc, request, request_len, cas TSRMLS_CC);
			// We will retry the set
		}while(result == -2 && ++retry_count < MEMCACHE_G(integrity_error_retry_count));


		if (result < 0) {
			if (result == -2) {
				LogManager::getLogger()->setCode(DI_CHECKSUM_SET_FAILED);
				php_error(E_WARNING, "Data Integrity verification failed for key %s, did not set the value\n", key);
				result = 0;		// Just to break out of the loop
			}
			else {
				mmc_server_failure(mmc TSRMLS_CC);
			}
		}

		if (key_copy != NULL) {
			efree(key_copy);
		}

		if (by_key) {
			if (shard_key_copy != NULL) {
				efree(shard_key_copy);
			}
			delete []cas_key;
		}

		if (data != NULL) {
			efree(data);
		}

		efree(request);
	}

	if (!mmc) {
		LogManager::getLogger()->setCode(CONNECT_FAILED);
		LogManager::getLogger()->setHost(PROXY_STR);
	} else {
		if (mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
		}
		LogManager::getLogger()->setHost(mmc->host);
	}

	LogManager::getLogger()->setResLen(value_len);

	return result;
}
/* }}} */


int mmc_pool_store_wrapper(mmc_pool_t *pool, const char *command, int command_len, const char *key, int key_len, int flags, int expire, unsigned long cas, const char *value, int value_len, zend_bool by_key, const char *shard_key, int shard_key_len, zval *val_len  TSRMLS_DC) /* {{{ */
{
	return mmc_pool_store(pool, command, command_len, key, key_len, flags, expire, cas, value, value_len, by_key, shard_key, shard_key_len, val_len);
}
/*}}}*/

static int mmc_compress(char **result, unsigned long *result_len, const char *data, int data_len, int flags TSRMLS_DC) /* {{{ */
{
	int status, level = MEMCACHE_G(compression_level);

	// *result_len = data_len + (data_len / 1000) + 25 + 1; /* some magic from zlib.c */
	*result_len = data_len + (data_len / 16) + 64 + 3; /* worst-case expansion for lzo */
	*result = (char *) emalloc(*result_len);

	if (!*result) {
		return 0;
	}

	if (flags & MMC_COMPRESSED) {
		if (level == 0) level = 6;

		if (level >= 0) {
			status = compress2((unsigned char *) *result, result_len, (unsigned const char *) data, data_len, level);
		} else {
			status = compress((unsigned char *) *result, result_len, (unsigned const char *) data, data_len);
		}
	} else if(flags & MMC_COMPRESSED_BZIP2) {
		/* optimize later for bzip2 compress estimations */
		/* *result_len = data_len + (0.01 * data_len) + 600; */
		int block_size = 4;
		int work_factor = 0;
		unsigned int intRetSize = (unsigned int)*result_len;

		status = BZ2_bzBuffToBuffCompress(*result, &intRetSize, (char*)data, data_len, block_size, 0, work_factor);
		*result_len = intRetSize;
		switch(status) {
			case BZ_OK: status = Z_OK; break;
			default: status = Z_DATA_ERROR; break;
		}
	} else {
		status = lzo1x_1_compress((const unsigned char *)data, data_len, (unsigned char*)*result, (lzo_uint*)result_len, MEMCACHE_G(lzo_wmem));
		switch (status) {
			case LZO_E_OK: status = Z_OK; break;
			case LZO_E_OUTPUT_OVERRUN: status = Z_BUF_ERROR; break;
			default: status = Z_DATA_ERROR;
		}
	}

	if (status == Z_OK) {
		*result = (char *)erealloc(*result, *result_len + 1);
		(*result)[*result_len] = '\0';
		return 1;
	}

	switch (status) {
		case Z_MEM_ERROR:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Not enough memory to perform compression");
			break;
		case Z_BUF_ERROR:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Not enough room in the output buffer to perform compression");
			break;
		case Z_STREAM_ERROR:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid compression level");
			break;
		default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown error during compression");
			break;
	}

	efree(*result);
	return 0;
}
/* }}}*/

static int bz2_decompress_safe(unsigned char** result, size_t *result_len, unsigned const char* data, size_t data_len) /* {{{ */
{
#ifdef  HAVE_BZ2
	/* refer to ext/bz2/bz2.c */
	int error;
	long small = 0;
	#if defined(PHP_WIN32)
	unsigned __int64 size = 0;
	#else
	unsigned long long size = 0;
	#endif
	bz_stream bzs;

	bzs.bzalloc = NULL;
	bzs.bzfree = NULL;

	if (BZ2_bzDecompressInit(&bzs, 0, small) != BZ_OK) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "BZ2_bzDecompressInit() failed");
		return 0;
	}

	bzs.next_in = (char*)data;
	bzs.avail_in = data_len;

	/* review: result is exact size as result_len (there is no + 1) */
	bzs.avail_out = *result_len;
	bzs.next_out = (char *)*result;

	while ((error = BZ2_bzDecompress(&bzs)) == BZ_OK && bzs.avail_in > 0) {
		/* compression is better then 2:1, need to allocate more memory */
		bzs.avail_out = data_len;
		size = (bzs.total_out_hi32 * (unsigned int) -1) + bzs.total_out_lo32;
		*result = (unsigned char*)safe_erealloc(*result, 1, bzs.avail_out, size );
		bzs.next_out = (char*)*result + size;
	}

	if (error == BZ_STREAM_END || error == BZ_OK) {
		size = (bzs.total_out_hi32 * (unsigned int) -1) + bzs.total_out_lo32;
	} else { /* real error */
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "BZ2_bzDecompress() failed");
	}

	BZ2_bzDecompressEnd(&bzs);

	*result_len = size;

	switch(error) {
		case BZ_STREAM_END:
		case BZ_OK:
			return Z_OK;
	}

	return Z_DATA_ERROR;

#else
	/* TODO : php_error_docref */
#endif
}
/* }}} */

static int mmc_uncompress(char **result, unsigned long *result_len, const char *data, int data_len, int flags) /* {{{ */
{
	int status;
	unsigned int factor = 1, maxfactor = 16;
	char *tmp1 = NULL;

	do {
		*result_len = (unsigned long)data_len * (1 << factor++);
		*result = (char *) erealloc(tmp1, *result_len);
		if (flags & MMC_COMPRESSED) {
			status = uncompress((unsigned char *) *result, result_len, (unsigned const char *) data, data_len);
		} else if(flags & MMC_COMPRESSED_BZIP2) {
			status = bz2_decompress_safe((unsigned char**) result, result_len, (unsigned const char *) data, data_len);
		} else {
			status = lzo1x_decompress_safe((const unsigned char*)data, data_len, (unsigned char *)*result, (lzo_uint*)result_len, NULL);
			if (status == LZO_E_OUTPUT_OVERRUN) status = Z_BUF_ERROR;
			else if (status == LZO_E_OK) status = Z_OK;
			else status = Z_DATA_ERROR;
		}
		tmp1 = *result;
	} while (status == Z_BUF_ERROR && factor < maxfactor);

	if (status == Z_OK) {
		*result = (char *)erealloc(*result, *result_len + 1);
		(*result)[*result_len] = '\0';
		return 1;
	}

	efree(*result);
	return 0;
}
/* }}}*/

static int mmc_get_pool(zval *id, mmc_pool_t **pool TSRMLS_DC) /* {{{ */
{
	zval **connection;
	int resource_type;

	if (Z_TYPE_P(id) != IS_OBJECT || zend_hash_find(Z_OBJPROP_P(id), "connection", sizeof("connection"), (void **) &connection) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "No servers added to memcache connection");
		return 0;
	}

	*pool = (mmc_pool_t *) zend_list_find(Z_LVAL_PP(connection), &resource_type);

	if (!*pool || resource_type != le_memcache_pool) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid Memcache->connection member variable");
		return 0;
	}

	return Z_LVAL_PP(connection);
}
/* }}} */

static int _mmc_open(mmc_t *mmc, char **error_string, int *errnum TSRMLS_DC) /* {{{ */
{
	struct timeval tv;
	char *hostname = NULL, *hash_key = NULL, *errstr = NULL;
	int	hostname_len, err = 0;
	int ntries = MEMCACHE_G(connection_retry_count);
	/* close open stream */
	if (mmc->stream != NULL) {
		mmc_server_disconnect(mmc TSRMLS_CC);
	}

	if (mmc->connect_timeoutms > 0) {
		tv = _convert_timeoutms_to_ts(mmc->connect_timeoutms);
	} else {
		tv.tv_sec = mmc->timeout;
		tv.tv_usec = 0;
	}

	if (mmc->port) {
		hostname_len = spprintf(&hostname, 0, "%s:%d", mmc->host, mmc->port);
	}
	else {
		hostname_len = spprintf(&hostname, 0, "%s", mmc->host);
	}

	if (mmc->persistent) {
		spprintf(&hash_key, 0, "memcache:%d:%s", getpid(), hostname);
	}

	do {
		if (ntries < MEMCACHE_G(connection_retry_count)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING,
			"Server %s (tcp %d) failed with: Connection Error. Retry attempt #%d",
			mmc->host, mmc->port, MEMCACHE_G(connection_retry_count) - ntries);
		}
#if PHP_API_VERSION > 20020918
		mmc->stream = php_stream_xport_create( hostname, hostname_len,
										   ENFORCE_SAFE_MODE | REPORT_ERRORS,
										   STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT,
										   hash_key, &tv, NULL, &errstr, &err);
#else
		if (mmc->persistent) {
			switch(php_stream_from_persistent_id(hash_key, &(mmc->stream) TSRMLS_CC)) {
			case PHP_STREAM_PERSISTENT_SUCCESS:
				if (php_stream_eof(mmc->stream)) {
					php_stream_pclose(mmc->stream);
					mmc->stream = NULL;
					break;
				}
			case PHP_STREAM_PERSISTENT_FAILURE:
				break;
			}
		}

		//This retries in case of connection failure based on ntries.
		if (!mmc->stream) {
			int socktype = SOCK_STREAM;
			mmc->stream = php_stream_sock_open_host(mmc->host, mmc->port, socktype, &tv, hash_key);
	  	}

#endif
	} while (!mmc->stream && ntries-- > 0);

	efree(hostname);
	if (mmc->persistent) {
		efree(hash_key);
	}

	if (!mmc->stream) {
		MMC_DEBUG(("_mmc_open: can't open socket to host"));
		mmc_server_seterror(mmc, errstr != NULL ? errstr : "Connection failed", err);
		mmc_server_deactivate(mmc TSRMLS_CC);

		if (errstr) {
			if (error_string) {
				*error_string = errstr;
			}
			else {
				efree(errstr);
			}
		}
		if (errnum) {
			*errnum = err;
		}

		return 0;
	}

	php_stream_auto_cleanup(mmc->stream);
	php_stream_set_option(mmc->stream, PHP_STREAM_OPTION_READ_TIMEOUT, 0, &tv);
	php_stream_set_option(mmc->stream, PHP_STREAM_OPTION_WRITE_BUFFER, PHP_STREAM_BUFFER_NONE, NULL);
	php_stream_set_chunk_size(mmc->stream, MEMCACHE_G(chunk_size));

	int socketd = ((php_netstream_data_t*)mmc->stream->abstract)->socket;
	int flag = MEMCACHE_G(tcp_nodelay)? 1: 0;
	setsockopt(socketd, IPPROTO_TCP,  TCP_NODELAY, (void *) &flag, sizeof(int));

	mmc->status = MMC_STATUS_CONNECTED;
	mmc->got_options = false;

	if (mmc->error != NULL) {
		pefree(mmc->error, mmc->persistent);
		mmc->error = NULL;
	}

	return 1;
}
/* }}} */

int mmc_open(mmc_t *mmc, int force_connect, char **error_string, int *errnum TSRMLS_DC) /* {{{ */
{
	switch (mmc->status) {
		case MMC_STATUS_DISCONNECTED:
			return _mmc_open(mmc, error_string, errnum TSRMLS_CC);

		case MMC_STATUS_CONNECTED:
			return 1;

		case MMC_STATUS_UNKNOWN:
			/* check connection if needed */
			if (force_connect) {
				char *version;
				if ((version = mmc_get_version(mmc TSRMLS_CC)) == NULL && !_mmc_open(mmc, error_string, errnum TSRMLS_CC)) {
					break;
				}
				if (version) {
					efree(version);
				}
				mmc->status = MMC_STATUS_CONNECTED;
			}
			return 1;

		case MMC_STATUS_FAILED:
			if (mmc->retry_interval >= 0 && (long)time(NULL) >= mmc->failed + mmc->retry_interval) {
				if (_mmc_open(mmc, error_string, errnum TSRMLS_CC) /*&& mmc_flush(mmc, 0 TSRMLS_CC) > 0*/) {
					return 1;
				}
			}
			break;
	}
	return 0;
}
/* }}} */

void mmc_server_disconnect(mmc_t *mmc TSRMLS_DC) /* {{{ */
{
	if (mmc->stream != NULL) {
		if (mmc->persistent) {
			php_stream_pclose(mmc->stream);
		}
		else {
			php_stream_close(mmc->stream);
		}
		mmc->stream = NULL;
	}
	mmc->status = MMC_STATUS_DISCONNECTED;
}
/* }}} */

void mmc_server_deactivate(mmc_t *mmc TSRMLS_DC) /* 	disconnect and marks the server as down {{{ */
{
	mmc_server_disconnect(mmc TSRMLS_CC);
	mmc->status = MMC_STATUS_FAILED;
	mmc->failed = (long)time(NULL);

	if (mmc->failure_callback != NULL) {
		zval *retval = NULL;
		zval *host, *tcp_port, *udp_port, *error, *errnum;
		zval **params[5];

		params[0] = &host;
		params[1] = &tcp_port;
		params[2] = &udp_port;
		params[3] = &error;
		params[4] = &errnum;

		MAKE_STD_ZVAL(host);
		MAKE_STD_ZVAL(tcp_port); MAKE_STD_ZVAL(udp_port);
		MAKE_STD_ZVAL(error); MAKE_STD_ZVAL(errnum);

		ZVAL_STRING(host, mmc->host, 1);
		ZVAL_LONG(tcp_port, mmc->port); ZVAL_LONG(udp_port, 0);

		if (mmc->error != NULL) {
			ZVAL_STRING(error, mmc->error, 1);
		}
		else {
			ZVAL_NULL(error);
		}
		ZVAL_LONG(errnum, mmc->errnum);

		call_user_function_ex(EG(function_table), NULL, mmc->failure_callback, &retval, 5, params, 0, NULL TSRMLS_CC);

		zval_ptr_dtor(&host);
		zval_ptr_dtor(&tcp_port); zval_ptr_dtor(&udp_port);
		zval_ptr_dtor(&error); zval_ptr_dtor(&errnum);

		if (retval != NULL) {
			zval_ptr_dtor(&retval);
		}
	}
	else {
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Server %s (tcp %d) failed with: %s (%d)",
			mmc->host, mmc->port, mmc->error, mmc->errnum);
	}
}
/* }}} */

static int mmc_readline(mmc_t *mmc TSRMLS_DC) /* {{{ */
{
	char *response;
	size_t response_len;
	int retry_count = 0;
	char buffer[100] = {0};

	php_stream *stream = mmc->proxy? mmc->proxy->stream: mmc->stream;
	if (stream == NULL) {
		mmc_server_seterror(mmc, "Socket is closed", 0);
		return -1;
	}

	/*
	 * we have seen that php_stream_get_line sometimes returns with null value even if the
	 * connection is still valid. to workaround this we retry reading from the socket
	 * three times before giving up
	 */
	while (retry_count++ < 3) {
		response = php_stream_get_line(stream, ZSTR(mmc->inbuf), MMC_BUF_SIZE, &response_len);

		if (response) {
			MMC_DEBUG(("mmc_readline: read data:"));
			MMC_DEBUG(("mmc_readline:---"));
			MMC_DEBUG(("%s", response));
			MMC_DEBUG(("mmc_readline:---"));
			return response_len;
		} else if (response == false) {
			/* response will be set to false if an error has occured */
			break;
		}
	}

	snprintf(buffer, 100, "Failed reeading line from stream. Retry count %d", retry_count);

	mmc_server_seterror(mmc, buffer, 0);
	return -1;
}
/* }}} */

#define VERSION_ERR_STR "Malformed version string :"
#define VERSION_ERR_STR_S sizeof(VERSION_ERR_STR)-1
#define MAX_ERR_STR_LEN 256

static char *mmc_get_version(mmc_t *mmc TSRMLS_DC) /* {{{ */
{
	char *version_str;
	int response_len;

	if (mmc_sendcmd(mmc, "version", sizeof("version") - 1 TSRMLS_CC) < 0) {
		return NULL;
	}

	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0) {
		return NULL;
	}

	if (mmc_str_left(mmc->inbuf, "VERSION ", response_len, sizeof("VERSION ") - 1)) {
		version_str = estrndup(mmc->inbuf + sizeof("VERSION ") - 1, response_len - (sizeof("VERSION ") - 1) - (sizeof("\r\n") - 1) );
		return version_str;
	}

	if (response_len > MAX_ERR_STR_LEN) {
		response_len = MAX_ERR_STR_LEN;
	}

	version_str = (char *)emalloc(response_len + VERSION_ERR_STR_S);
	memcpy(version_str, VERSION_ERR_STR, VERSION_ERR_STR_S);
	memcpy(version_str + VERSION_ERR_STR_S, mmc->inbuf, response_len);

	mmc_server_seterror(mmc, version_str, 0);

	efree(version_str);
	return NULL;
}
/* }}} */

static int mmc_str_left(char *haystack, char *needle, int haystack_len, int needle_len) /* {{{ */
{
	char *found;

	found = php_memnstr(haystack, needle, needle_len, haystack + haystack_len);
	if ((found - haystack) == 0) {
		return 1;
	}
	return 0;
}
/* }}} */

static int mmc_str(char *haystack, char *needle, int haystack_len, int needle_len) /* {{{ */
{
	char *found;

	found = php_memnstr(haystack, needle, needle_len, haystack + haystack_len);
	return (found ? 1 : 0);
}
/* }}} */

static int mmc_sendcmd(mmc_t *mmc, const char *cmd, int cmdlen TSRMLS_DC) /* {{{ */
{
	char *command;
	int command_len, proxy_len;
	php_stream *stream;

	if (mmc->proxy) {
		stream = mmc->proxy->stream;
		proxy_len = mmc->proxy_str_len;
	} else {
		stream = mmc->stream;
		proxy_len = 0;
	}

	php_netstream_data_t *sock = (php_netstream_data_t*)stream->abstract;

	if (!mmc || !cmd) {
		return -1;
	}

	MMC_DEBUG(("mmc_sendcmd: sending command '%s'", cmd));

	command = (char *)emalloc(proxy_len + cmdlen + sizeof("\r\n"));
	if (proxy_len) {
		memcpy(command, mmc->proxy_str, proxy_len);
	}
	memcpy(command + proxy_len, cmd, cmdlen);
	memcpy(command + proxy_len + cmdlen, "\r\n", sizeof("\r\n") - 1);
	command_len = proxy_len + cmdlen + sizeof("\r\n") - 1;
	command[command_len] = '\0';

	if (mmc->timeoutms > 1) {
		sock->timeout = _convert_timeoutms_to_ts(mmc->timeoutms);
	}


	if (php_stream_write(stream, command, command_len) != command_len) {
		mmc_server_seterror(mmc, "Failed writing command to stream", 0);
		efree(command);
		return -1;
	}
	efree(command);

	return 1;
}
/* }}}*/

static int mmc_parse_response(mmc_t *mmc, char *response, int response_len, char **key, int *key_len, int *flags, unsigned long *cas, char **chksum, int *value_len) /* {{{ */
{
	int i=0, n=0;
	int spaces[5];
	int nspaces = (cas != NULL)? 4: 3;

	if (!response || response_len <= 0) {
		mmc_server_seterror(mmc, "Empty response", 0);
		return -1;
	}

	// We expect a checksum in the header
	if (mmc->di_algo_in_use != DI_CHKSUM_UNSUPPORTED) {
		nspaces++;
	}

	int valsize = sizeof("VALUE ") -1;
	if (response_len < valsize ||
		strncmp(response, "VALUE ", valsize) != 0) {
		mmc_server_received_error(mmc, response_len);
		return -1;
	}

	MMC_DEBUG(("mmc_parse_response: got response '%s'", response));

	for (i=0, n=0; i < response_len && n < nspaces; i++) {
		if (response[i] == ' ') {
			spaces[n++] = i;
		}
	}

	MMC_DEBUG(("mmc_parse_response: found %d spaces", n));

	if (n < nspaces) {
		mmc_server_seterror(mmc, "Malformed VALUE header", 0);
		return -1;
	}

	if (key != NULL) {
		int len = spaces[1] - spaces[0] - 1;

		*key = (char *)emalloc(len + 1);
		*key_len = len;

		memcpy(*key, response + spaces[0] + 1, len);
		(*key)[len] = '\0';
	}

	*flags = atoi(response + spaces[1]);
	*value_len = atoi(response + spaces[2]);
	if (mmc->di_algo_in_use != DI_CHKSUM_UNSUPPORTED) {
		*chksum = response + spaces[3] + 1;
		if (nspaces == 5) {
			*cas = strtoul(response + spaces[4], NULL, 10);
		}
	}
	else {
		*chksum = NULL;
		if (nspaces == 4) {
			*cas = strtoul(response + spaces[3], NULL, 10);
		}
	}

	if (*flags < 0 || *value_len < 0) {
		mmc_server_seterror(mmc, "Malformed VALUE header", 0);
		return -1;
	}

	MMC_DEBUG(("mmc_parse_response: 1st space is at %d position", spaces[1]));
	MMC_DEBUG(("mmc_parse_response: 2nd space is at %d position", spaces[2]));
	MMC_DEBUG(("mmc_parse_response: flags = %d", *flags));
	MMC_DEBUG(("mmc_parse_response: value_len = %d ", *value_len));

	return 1;
}
/* }}} */

static int mmc_postprocess_value(const char* key, int flags, const char* host, zval **return_value, char *value, int value_len TSRMLS_DC) /*
	post-process a value into a result zval struct, value will be free()'ed during process {{{ */
{
	const char *value_tmp = value;


	if(flags & MMC_SERIALIZED) {
 		php_unserialize_data_t var_hash;
 		PHP_VAR_UNSERIALIZE_INIT(var_hash);
		LogManager::getLogger()->startSerialTime();
		LogManager::saveLogger();
 		if (!php_var_unserialize(return_value, (const unsigned char **)&value_tmp, (const unsigned char *)(value_tmp + value_len), &var_hash TSRMLS_CC)) {

 			if (!MEMCACHE_G(debug_mode)) {
 				ZVAL_FALSE(*return_value);
 				PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
 				efree(value);
 				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "unable to unserialize data for key=%s, server=%s", key, host);
 				return 0;
 			} else {
 				ZVAL_STRINGL(*return_value, value, value_len, 1);
 				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "unable to unserialize data for key=%s, server=%s, raw value returned as zval string", key, host);
 			}
 		}
		LogManager::restoreLogger();
		LogManager::getLogger()->stopSerialTime();
 		PHP_VAR_UNSERIALIZE_DESTROY(var_hash);

 	} else if(flags & MMC_SERIALIZED_IGBINARY) {
 	#ifdef HAVE_MEMCACHE_IGBINARY
		LogManager::getLogger()->startSerialTime();
		LogManager::saveLogger();
 		/* igbinary functions return 0 on success */
 		if( igbinary_unserialize((uint8_t *)value, value_len, return_value TSRMLS_CC) != 0 ) {

 			if (!MEMCACHE_G(debug_mode)) {
 				ZVAL_FALSE(*return_value);
 				efree(value);
 				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "unable to igbinary_unserialize data for key=%s, server=%s", key, host);
 				return 0;
 			} else {
 				ZVAL_STRINGL(*return_value, value, value_len, 1);
 				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "unable to igbinary_unserialize data for key=%s, server=%s, raw value returned as zval string", key, host);
 			}
 		}
		LogManager::restoreLogger();
		LogManager::getLogger()->stopSerialTime();
 	#else
 		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Missing igbinary support (key=%s)", key);
 		efree(value);
 		return 0;
 	#endif
  	}

	efree(value);
	return 1;
}
/* }}} */


int mmc_exec_getl_cmd(mmc_pool_t *pool, const char *key, int key_len, zval **return_value, zval *return_flags, zval *return_cas, int timeout , char *metadata, int &metadata_len TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc;
	char *command, *value;
	int result = -1, command_len, response_len, value_len, flags = 0;
	unsigned long cas = 0;
	int chksum_error_retry_cnt = 0;

	MMC_DEBUG(("mmc_exec_getl_cmd: key '%s'", key));
	LogManager::getLogger()->setKey(key);

	if (timeout <= 0 || timeout > 30)
		timeout = 15;

	if (metadata_len > 0) {
		command_len = spprintf(&command, 0, "getl %s %d %s", key, timeout, metadata);
	}
	else {
		command_len = spprintf(&command, 0, "getl %s %d", key, timeout);
	}

	while (result < 0 && (mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC)) != NULL &&
		mmc->status != MMC_STATUS_FAILED) {
		MMC_DEBUG(("mmc_exec_getl_cmd: found server '%s:%d' for key '%s'", mmc->host, mmc->port, key));
		LogManager::getLogger()->setHost(mmc->host);

		value = NULL;
		// This loop is to retry just the sending and receiving part in case of checksum errors
		do {
			if (value)
				efree(value);
			/* send command and read value */
			if ( mmc_sendcmd(mmc, command, command_len TSRMLS_CC) <=0 )
				break;

			result = mmc_read_value(mmc, NULL, NULL, &value, &value_len, &flags, &cas TSRMLS_CC);
			if (result >= 0 || result == CHKSUM_MISMATCH_DETECTED) {
				if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 || !mmc_str_left(mmc->inbuf, "END", response_len, sizeof("END")-1)) {
					mmc_server_seterror(mmc, "Malformed END line", 0);
					LogManager::getLogger()->setCode(MC_MALFORMD);
					result = -1;
				}
			}
		} while(result == CHKSUM_MISMATCH_DETECTED && ++chksum_error_retry_cnt < MEMCACHE_G(integrity_error_retry_count));

		if (result >= 0) {
			if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
				result = mmc_postprocess_value(key, flags, mmc->host, return_value, value, value_len TSRMLS_CC);
			}
			else if (0 == cas) {
				/* no lock error, the cas value should never be zero */
				mmc_server_seterror(mmc, "Invalid cas value", 0);
				LogManager::getLogger()->setCode(INVALID_CAS);
				result = -1;
			} else {
				ZVAL_STRINGL(*return_value, value, value_len, 0);
			}
		}
		else if (result == -2) {
			/* consume unread data in buffer */
			if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 ||
					!mmc_str_left(mmc->inbuf, "END", response_len, sizeof("END")-1)) {

				mmc_server_seterror(mmc, "Malformed END line", 0);
				LogManager::getLogger()->setCode(MC_MALFORMD);
				result = -1;
				mmc_readline(mmc TSRMLS_CC);
			} else {
				result = 0;
			}
		} else if (result < 0) {
			if (mmc_str_left(mmc->inbuf, "LOCK_ERROR", strlen(mmc->inbuf), sizeof("LOCK_ERROR")-1)) {
				/* failed to lock */
				ZVAL_FALSE(*return_value);
				LogManager::getLogger()->setCode(MC_LOCK_ERROR);
				result = 0;
				char *metadata_start = mmc->inbuf + (sizeof("LOCK_ERROR ") - 1);
				metadata_len = strlen(mmc->inbuf) - (sizeof("LOCK_ERROR \r\n") - 1);
				if (metadata_len > 0) {
					memcpy(metadata, metadata_start, metadata_len);
					metadata[metadata_len] = '\0';
				}
				else {
					metadata_len = 0;
				}
			} else if (mmc_str_left(mmc->inbuf, "NOT_FOUND", strlen(mmc->inbuf), sizeof("NOT_FOUND")-1)) {
				/* key doesn't exist */
				result = 0;
				LogManager::getLogger()->setCode(MC_NOT_FOUND);
			} else if (result == CHKSUM_MISMATCH_DETECTED) {
				result = 0;
				LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED_GETL);
				efree(value);
			} else {
				LogManager::getLogger()->setCode(SVR_OPN_FAILED);
			 	mmc_server_failure(mmc TSRMLS_CC);
			}
		}
	}

	LogManager::getLogger()->setCas(cas);
	LogManager::getLogger()->setFlags(flags);

	if (!mmc) {
		LogManager::getLogger()->setHost(PROXY_STR);
		LogManager::getLogger()->setCode(CONNECT_FAILED);
	} else {
		if (mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
		}
		LogManager::getLogger()->setHost(mmc->host);
	}

	if (result == 0 && (Z_TYPE_P(*return_value) != IS_BOOL)) {
		ZVAL_NULL(*return_value);
	}

	/*
	 * valid cas value and the result is not -1, then store the cas value
	 * along with the key in the hash table
	 */
	if (cas && result != -1) {
		add_assoc_long_ex(pool->cas_array, (char *) key, key_len + 1, cas);
	}


	if (return_flags != NULL) {
		zval_dtor(return_flags);
		ZVAL_LONG(return_flags, flags);
	}

	if (return_cas != NULL) {
		zval_dtor(return_cas);
		ZVAL_LONG(return_cas, cas);
	}


	efree(command);
	return result;
}
/* }}} */

int mmc_exec_unl_cmd(mmc_pool_t *pool, const char *key, int key_len, unsigned long cas, INTERNAL_FUNCTION_PARAMETERS) /* {{{ */
{
	mmc_t *mmc;
	char *command, *value;
	int result = -1, command_len, response_len, value_len, flags = 0;

	MMC_DEBUG(("mmc_exec_unl_cmd: key '%s' cas '%l'", key, cas));

	command_len = spprintf(&command, 0, "unl %s %lu", key, cas);

	LogManager::getLogger()->setKey(key);
	LogManager::getLogger()->setCas(cas);

	while (result < 0 && (mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC)) != NULL &&
		mmc->status != MMC_STATUS_FAILED) {
		MMC_DEBUG(("mmc_exec_unl_cmd: found server '%s:%d' for key '%s'", mmc->host, mmc->port, key));
		if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) < 0) {
			efree(command);
			result = -3;
			break;
		}
		efree(command);

		if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0){
			LogManager::getLogger()->setCode(READLINE_FAILED);
			MMC_DEBUG(("failed to read the server's response"));
			result = -2;
			break;
		}


		MMC_DEBUG(("mmc_exec_unl_cmd: server's response is '%s'", mmc->inbuf));

		if(mmc_str_left(mmc->inbuf,"UNLOCKED", response_len, sizeof("UNLOCKED") - 1)) {
			LogManager::getLogger()->setCode(MC_UNLOCKED);
			result = 1;
			break;
		}

		LogManager::getLogger()->setCode(CMD_FAILED);
		mmc_server_received_error(mmc, response_len);
		result = -1;
		break;
	}

	if (!mmc) {
		LogManager::getLogger()->setHost(PROXY_STR);
		LogManager::getLogger()->setCode(CONNECT_FAILED);
	} else {
		if (mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
		}
		LogManager::getLogger()->setHost(mmc->host);
	}

	return result;
}
/* }}} */

int mmc_exec_unl_cmd_by_key(mmc_pool_t *pool, mmc_t *mmc, const char *key, int key_len, const char *shard_key,
	int shard_key_len) /* {{{ */
{
	char *command, *value;
	int result = -1, command_len, response_len, value_len, flags = 0;
	zval **cas_lookup;
	int cas_key_len = shard_key_len+key_len;
	char *cas_key = new char[cas_key_len];
	memcpy(cas_key, key, key_len-1);
	memcpy(cas_key+key_len-1, shard_key, shard_key_len+1);

	if (pool->cas_array && FAILURE != zend_hash_find(Z_ARRVAL_P(pool->cas_array),
				(char *)cas_key, cas_key_len , (void**)&cas_lookup)) {
		command_len = spprintf(&command, 0, "unl %s %lu", key, **cas_lookup);

		if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) < 0) {
			efree(command);
			result = -3;
		} else if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0){
			result = -2;
		} else if (mmc_str_left(mmc->inbuf,"UNLOCKED", response_len,
					sizeof("UNLOCKED") - 1)) {
			result = 1;
		}
	}

	delete []cas_key;
	return result;
}
/* }}} */

int mmc_exec_retrieval_cmd(mmc_pool_t *pool, const char *key, int key_len, zval **return_value, zval *return_flags, zval *return_cas, int return_corrupted TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc;
	char *command, *value;
	int result = -1, command_len, response_len, value_len = 0, flags = 0;
	unsigned long cas;
	unsigned long *pcas = (return_cas != NULL)? &cas: NULL;
	int chksum_error_retry_cnt = 0;

	MMC_DEBUG(("mmc_exec_retrieval_cmd: key '%s'", key));

	command_len = (pcas != NULL)? spprintf(&command, 0, "gets %s", key):
									spprintf(&command, 0, "get %s", key);

	LogManager::getLogger()->setKey(key);
	LogManager::getLogger()->setLogName(pool->log_name);

	while (result < 0 && (mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC)) != NULL &&
		mmc->status != MMC_STATUS_FAILED) {
		MMC_DEBUG(("mmc_exec_retrieval_cmd: found server '%s:%d' for key '%s'", mmc->host, mmc->port, key));

		/* send command and read value */
		// This loop is to retry just the sending and receiving part in case of checksum errors
		value = NULL;
		do {
			if (value)
				efree(value);
			/* send command and read value */
			if ( mmc_sendcmd(mmc, command, command_len TSRMLS_CC) <=0 )
				break;

			result = mmc_read_value(mmc, NULL, NULL, &value, &value_len, &flags, pcas TSRMLS_CC);
			LogManager::getLogger()->setFlags(flags);
			/* not found */
			if (result == 0) {
				if (pool->false_on_error) {
					ZVAL_NULL(*return_value);
				} else {
					ZVAL_FALSE(*return_value);
				}
			}
			/* read "END" */
			else if (result > 0 || result == CHKSUM_MISMATCH_DETECTED) {
				if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 || !mmc_str_left(mmc->inbuf, "END", response_len, sizeof("END")-1)) {
					mmc_server_seterror(mmc, "Malformed END line", 0);
					LogManager::getLogger()->setCode(MC_MALFORMD);
					result = -1;
				}
			}
		} while(result == CHKSUM_MISMATCH_DETECTED && ++chksum_error_retry_cnt < MEMCACHE_G(integrity_error_retry_count));


		if (result > 0) {
			if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
				result = mmc_postprocess_value(key, flags, mmc->host, return_value, value, value_len TSRMLS_CC);
			}
			else {
				ZVAL_STRINGL(*return_value, value, value_len, 0);
			}
		}
		else if (result == -2) {
			/* failed to uncompress or unserialize */
			if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 ||
				!mmc_str_left(mmc->inbuf, "END", response_len, sizeof("END")-1)) {

				mmc_server_seterror(mmc, "Malformed END line", 0);
				LogManager::getLogger()->setCode(MC_MALFORMD);
				result = -1;
			} else {
				ZVAL_FALSE(*return_value);
				result = 0;
			}
		}
		else if (result < 0) {
			if (result == CHKSUM_MISMATCH_DETECTED) {
				LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED_GET);
				result = 0;
				if (return_corrupted) {
					ZVAL_STRINGL(*return_value, value, value_len, 0);
				}
				else {
					efree(value);
				}
			}
			else {
				LogManager::getLogger()->setCode(SVR_OPN_FAILED);
				mmc_server_failure(mmc TSRMLS_CC);
			}
		}
	}

	if (!mmc) {
		LogManager::getLogger()->setHost(PROXY_STR);
		LogManager::getLogger()->setCode(CONNECT_FAILED);
	} else {
		if (mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
		}
		LogManager::getLogger()->setHost(mmc->host);
	}

	if (return_flags != NULL) {
		zval_dtor(return_flags);
		ZVAL_LONG(return_flags, flags);
	}

	if (return_cas != NULL) {
		zval_dtor(return_cas);
		ZVAL_LONG(return_cas, cas);
	}

	if (return_cas) {
		LogManager::getLogger()->setCas(*pcas);
	}

	efree(command);
	return result;
}


static size_t tokenize_command(char *command, token_t *tokens, const size_t max_tokens) {
	char *s, *e;
	size_t ntokens = 0;

	assert(command != NULL && tokens != NULL && max_tokens > 1);

	for (s = e = command; ntokens < max_tokens - 1; ++e) {
		if (*e == ' ') {
			if (s != e) {
				tokens[ntokens].value = s;
				tokens[ntokens].length = e - s;
				ntokens++;
				*e = '\0';
			}
			s = e + 1;
		}
		else if (*e == '\0') {
			if (s != e) {
				tokens[ntokens].value = s;
				tokens[ntokens].length = e - s;
				ntokens++;
			}

			break; /* string end */
		}
	}

	/*
	 * If we scanned the whole string, the terminal value pointer is null,
	 * otherwise it is the first unprocessed character.
	 */
	tokens[ntokens].value =  *e == '\0' ? NULL : e;
	tokens[ntokens].length = 0;
	ntokens++;

	return ntokens;
}

static void update_result_array(zval **status_array, char *line, int res_code) {
	if (status_array == NULL) {
		return;
	}
	token_t tokens[MAX_TOKENS];
	int i;
	int ntokens = tokenize_command(line, (token_t *)&tokens, MAX_TOKENS);

	for (i = 1; i < ntokens; i = i+1) {
		if (tokens[i].value) {
			add_assoc_bool_ex(*status_array, tokens[i].value, tokens[i].length + 1, 0);
			LogManager::getLogger(tokens[i].value)->setCode(res_code);
		}
	}
}

static int mmc_exec_retrieval_cmd_multi(
	mmc_pool_t *pool, zval *keys, zval **return_value, zval **status_array, zval *return_flags, zval *return_cas TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc;
	HashPosition pos;
	zval **zkey;
	char *result_key, *value;
	char key[MMC_KEY_MAX_SIZE];
	unsigned int key_len;
	unsigned long cas;
	unsigned long *pcas = (return_cas != NULL)? &cas: NULL;
	char *command_line[pool->num_servers];
	int done = 0;
	zval *chksum_error_retry_hash;
	int retry_count = 0;
	bool chksum_failed = false, do_failover = false;
	LogManager::setMultiLogger();

	int	i = 0, j, num_requests, result, result_status, result_key_len, value_len, flags;
	mmc_queue_t serialized = {0};		/* mmc_queue_t<zval *>, pointers to zvals which need unserializing */
	mmc_queue_t serialized_key = {0};	/* pointers to corresponding keys */
	mmc_queue_t serialized_flags = {0};	/* pointers to corresponding flags */
	mmc_queue_t serialized_key_mmc = {0};	/* pointers to corresponding mmc */

	array_init(*return_value);

	if (status_array != NULL)
		array_init(*status_array);

	if (return_flags != NULL) {
		zval_dtor(return_flags);
		array_init(return_flags);
	}

	if (return_cas != NULL) {
		zval_dtor(return_cas);
		array_init(return_cas);
	}

	MAKE_STD_ZVAL(chksum_error_retry_hash);
	array_init(chksum_error_retry_hash);

	mmc_init_multi(TSRMLS_C);

	/* until no retrival errors or all servers have failed */
	do {
		result_status = 0; num_requests = 0;
		zend_hash_internal_pointer_reset_ex(Z_ARRVAL_P(keys), &pos);

		/* first pass to build requests for each server */
		while (zend_hash_get_current_data_ex(Z_ARRVAL_P(keys), (void **)&zkey, &pos) == SUCCESS) {
			if (mmc_prepare_key(*zkey, key, &key_len TSRMLS_CC) == MMC_OK) {
				/* schedule key if
						- first round OR
						- if missing from result OR
						- If we have a checksum mismatch error for it (and we have not exceeded the checksum error retries)
				*/
				LogManager::createLogger(key);
				LogManager::setLogger(key);
				LogManager::getLogger()->setCmd("getMulti");
				LogManager::getLogger()->setLogName(pool->log_name);

				if (!i || (do_failover && !zend_hash_exists(Z_ARRVAL_PP(return_value), key, key_len +1)) ||
						(chksum_failed && zend_hash_exists(Z_ARRVAL_P(chksum_error_retry_hash), key, key_len +1)) ) {
					if ((mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC)) != NULL && mmc->status != MMC_STATUS_FAILED) {
						if (!(mmc->outbuf.len)) {
							append_php_smart_string(&(mmc->outbuf), pcas != NULL? "gets": "get",
									pcas != NULL? sizeof("gets")-1: sizeof("get")-1);
							pool->requests[num_requests++] = mmc;
						}
						LogManager::getLogger()->setHost(mmc->host);

						append_php_smart_string(&(mmc->outbuf), " ", 1);
						append_php_smart_string(&(mmc->outbuf), key, key_len);
						MMC_DEBUG(("mmc_exec_retrieval_cmd_multi: scheduled key '%s' for '%s:%d' request length '%d'", key, mmc->host, mmc->port, mmc->outbuf.len));
						/* for get2, mark this server as a good server for now */
						if (status_array && !done) {
							add_assoc_bool_ex(*status_array, key, key_len + 1, 1);
						}
						LogManager::getLogger()->setCode(MC_SUCCESS);
					}
					else {
						if (status_array) {
						/*for get2, key belongs to a failed server */
						add_assoc_bool_ex(*status_array, key, key_len + 1, 0);
						}
						LogManager::getLogger()->setCode(CONNECT_FAILED);
						if (mmc) {
							LogManager::getLogger()->setHost(mmc->host);
						} else {
							LogManager::getLogger()->setHost(PROXY_STR);
						}
					}
				}
			}
			zend_hash_move_forward_ex(Z_ARRVAL_P(keys), &pos);
		}
		done = 1;

		/* second pass to send requests in parallel */
		for (j=0; j<num_requests; j++) {
			smart_str_0(&(pool->requests[j]->outbuf));

			if (status_array) {
				command_line[j] = (char *)emalloc(pool->requests[j]->outbuf.len + 1);
				memcpy(command_line[j], pool->requests[j]->outbuf.c, pool->requests[j]->outbuf.len);
				command_line[j][pool->requests[j]->outbuf.len] = '\0';
			}

			if ((result = mmc_sendcmd(pool->requests[j], pool->requests[j]->outbuf.c, pool->requests[j]->outbuf.len TSRMLS_CC)) < 0) {
				mmc_server_failure(pool->requests[j] TSRMLS_CC);
				result_status = result;
				if (status_array) {
					update_result_array(&(*status_array), command_line[j], WRITE_TO_SERVER_FAILED);
				}
			}
		}

		if (zend_hash_num_elements(Z_ARRVAL_P(chksum_error_retry_hash)) > 0) {
			zend_hash_clean(Z_ARRVAL_P(chksum_error_retry_hash));
		}

		LogManager::createLogger("TMP_KEY");
		/* third pass to read responses */
		for (j=0; j<num_requests; j++) {
			if (pool->requests[j]->status != MMC_STATUS_FAILED) {
				do {
					value = NULL;
					result_key = NULL;
					value_len = flags = result_key_len = cas = 0;
					int free_key = 1;
					LogManager::setLogger("TMP_KEY");

					result = mmc_read_value(pool->requests[j], &result_key, &result_key_len, &value, &value_len, &flags, pcas TSRMLS_CC);
					if (result == -2) {
						/* uncompression failed */
						LogManager::getLogger(result_key)->setCode(UNCOMPRESS_FAILED);
						if (status_array) {
							add_assoc_bool_ex(*status_array, result_key, result_key_len + 1, 0);
						}
					}
					else if (result == CHKSUM_MISMATCH_DETECTED) {
						LogManager::getLogger(result_key)->setCode(DI_CHECKSUM_GET_FAILED_GET);
						add_assoc_null_ex(chksum_error_retry_hash, result_key, result_key_len+1);
						if (status_array) {
							add_assoc_bool_ex(*status_array, result_key, result_key_len+1, 0);
						}
						efree(value);
					}
					else if (result <= 0) {
						if (result_key) {
							free(result_key);
						}
						break;
					}
					else if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
						zval *result;
						MAKE_STD_ZVAL(result);
						ZVAL_STRINGL(result, value, value_len, 0);

						/* don't store duplicate values */
						if (zend_hash_add(Z_ARRVAL_PP(return_value), result_key, result_key_len + 1, &result, sizeof(result), NULL) == SUCCESS) {
							mmc_queue_push(&serialized, result);
							mmc_queue_push(&serialized_key, result_key);
							mmc_queue_push(&serialized_flags, (void*)(unsigned long)flags);
							mmc_queue_push(&serialized_key_mmc, (void*)(pool->requests[j]));
							free_key = 0;
						}
						else {
							zval_ptr_dtor(&result);
						}
					}
					else {
						add_assoc_stringl_ex(*return_value, result_key, result_key_len + 1, value, value_len, 0);
					}

					LogManager::getLogger(result_key)->setResLen(value_len);
                    LogManager::getLogger(result_key)->setFlags(flags);
                    LogManager::getLogger(result_key)->setCas(cas);

					if (return_flags != NULL) {
						add_assoc_long_ex(return_flags, result_key, result_key_len + 1, flags);
					}

					if (return_cas != NULL) {
						add_assoc_long_ex(return_cas, result_key, result_key_len + 1, cas);
					}

					if (free_key)
						efree(result_key);
				} while(1);

				/* check for server failure */
				if (result < 0 && result != CHKSUM_MISMATCH_DETECTED) {
					mmc_server_failure(pool->requests[j] TSRMLS_CC);
					result_status = result;
					if (status_array) {
						update_result_array(&(*status_array), command_line[j], LogManager::getLogger()->getCode());
					}
				}
			}

			smart_str_free(&(pool->requests[j]->outbuf));
		}

		if (status_array) {
			for (j=0; j < num_requests; j++) {
				efree(command_line[j]);
			}
		}

		chksum_failed = zend_hash_num_elements(Z_ARRVAL_P(chksum_error_retry_hash)) > 0 &&
							++retry_count < MEMCACHE_G(integrity_error_retry_count);
		do_failover = result_status < 0 && MEMCACHE_G(allow_failover) && i <= MEMCACHE_G(max_failover_attempts);
		i++;
	} while (do_failover || chksum_failed);

	zval_ptr_dtor(&chksum_error_retry_hash);

	/* post-process serialized values */
	if (serialized.len) {
		zval *value;
		char *key;
		int flag;

		while ((value = (zval *)mmc_queue_pop(&serialized)) != NULL) {
			key = (char *)mmc_queue_pop(&serialized_key);
			LogManager::setLogger(key);
 			flag = (int) (unsigned long) (void*) mmc_queue_pop(&serialized_flags);
			mmc_t *popped_mmc = (mmc_t *)(mmc_queue_pop(&serialized_key_mmc));
			const char *hostname = (popped_mmc == NULL) ? "UNKNOWN" : popped_mmc->host;
			if (popped_mmc == NULL) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "MMC NULL for (serialized) key %s", key);
			}
			if (result = mmc_postprocess_value(key, flag, hostname, &value, Z_STRVAL_P(value), Z_STRLEN_P(value) TSRMLS_CC) == 0) {
				/* unserialize failed */
				if (status_array) {
					add_assoc_bool_ex(*status_array, key, strlen(key) + 1, 0);
				}
				LogManager::getLogger()->setCode(UNSERIALIZE_FAILED);
			}
			efree(key);
		}

		mmc_queue_free(&serialized);
		mmc_queue_free(&serialized_key);
		mmc_queue_free(&serialized_flags);
		mmc_queue_free(&serialized_key_mmc);
	}

	mmc_free_multi(TSRMLS_C);

	return (status_array != NULL)? 0: result_status;
}
/* }}} */

static char *get_key(char *inbuf)
{
	char *key = NULL;
	int i = 0;
	while(*inbuf++  != ' '); //skip VALUE string
	key = inbuf;
	while(*++inbuf != ' ' && i++ < 1024 && *inbuf != '\n');
	*inbuf = 0;
	return key;
}


static int mmc_read_value(mmc_t *mmc, char **key, int *key_len, char **value, int *value_len, int *flags, unsigned long *cas TSRMLS_DC) /* {{{ */
{
	char *data;
	int response_len, data_len, i, size;
	php_stream *stream = mmc->proxy? mmc->proxy->stream: mmc->stream;
	char *chksum = NULL;

	/* read "VALUE <key> <flags> <bytes> <cas>\r\n" header line */
	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0) {
		MMC_DEBUG(("failed to read the server's response"));
		LogManager::getLogger()->setCode(READLINE_FAILED);
		return -1;
	}


	/* reached the end of the data */
	if (mmc_str_left(mmc->inbuf, "END", response_len, sizeof("END") - 1)) {
		if (!MEMCACHE_G(in_multi)) {
			LogManager::getLogger()->setCode(MC_ONLY_END);
		}
		return 0;
	}

	if (mmc_parse_response(mmc, mmc->inbuf, response_len, key, key_len, flags, cas, &chksum, &data_len) < 0) {
		LogManager::getLogger()->setCode(PARSE_RESPONSE_FAILED);
		return -1;
	}

	MMC_DEBUG(("mmc_read_value: data len is %d bytes", data_len));

	/* data_len + \r\n + \0 */
	data = (char *)emalloc(data_len + 3);
	LogManager::getLogger()->setResLen(data_len);

	for (i=0; i<data_len+2; i+=size) {
		if ((size = php_stream_read(stream, data + i, data_len + 2 - i)) == 0) {
			mmc_server_seterror(mmc, "Failed reading value response body", 0);
			efree(data);
			LogManager::getLogger()->setCode(READ_VALUE_FAILED);
			return -1;
		}
	}

	/* Null termination is needed to make a valid php string, required to work for
		php functions like is_numeric */
	data[data_len] = '\0';

	if (!memcache_lzo_enabled && (*flags & MMC_COMPRESSED_LZO) && data_len > 0) {
		mmc_server_seterror(mmc, "Failed to uncompress data - lzo init failed", 0);
		efree(data);
		php_error_docref(NULL TSRMLS_CC, E_NOTICE, "unable to uncompress data from server=%s - lzo init failed", mmc->host);
		LogManager::getLogger()->setCode(UNCOMPRESS_FAILED);
		return -2;
	}

	//if the data was encapsulated with the crc header then extract the crc header
	//and check for data sanity before uncompression
	long crc_value = 0;
	long uncrc_value = 0;
	char *hp = data;
	unsigned int crc_hdr_len = 0;
	unsigned int hp_len = data_len;
	bool has_chksum = false, has_new_checksum = false;
	int chksum_metadata;

	// At the end of the two HUGE if blocks, we will have the checksum if one is present (new style or old)

	// Old style CRC (the checksum is prepended to the value). We need to check this even if new style checksums are enabled
	if (*flags & MMC_CHKSUM) {
		// get the crc of the compressed data
		char crc_buf[MMC_CHKSUM_LEN] = {0};
		char uncrc_buf[MMC_CHKSUM_LEN] = {0};
		int i = 0, j = 0, min_len = 0;

#define PECL_MIN(a,b) a < b ? a : b

		if (*flags & (MMC_COMPRESSED | MMC_COMPRESSED_LZO | MMC_COMPRESSED_BZIP2)) {
			// Reading compressed crc checksum

			min_len = PECL_MIN(data_len, MMC_CHKSUM_LEN);

			for (i = 0; i < min_len && data[i] != '\r'; i++) {
				crc_buf[i] = data[i];
			}

			if (i == data_len) {
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "CRC checking failed Invalid format. Key %s Host %s",
					get_key(mmc->inbuf), mmc->host);
				efree(data);
				LogManager::getLogger()->setCode(CRC_CHKSUM1_FAILED);
				return -2;
			}

			if (data[i] == '\r') {
				crc_value = strtol(crc_buf, NULL, 16);
				if (crc_value == LONG_MAX || crc_value == LONG_MIN) {
					php_error_docref(NULL TSRMLS_CC, E_NOTICE, "overflow or undeflow \
 						happend with crc checksum Key %s Host %s", get_key(mmc->inbuf), mmc->host);
					efree(data);
					return -2;
				}

				i++; // skip over the first \r

			} else {
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "CRC checking failed Invalid format. Key %s Host %s",
					get_key(mmc->inbuf), mmc->host);
				efree(data);
				LogManager::getLogger()->setCode(CRC_CHKSUM2_FAILED);
				return -2;
			}
		}

		min_len = PECL_MIN(data_len-i, MMC_CHKSUM_LEN);

		for (j = 0; j < min_len && data[j+i] != '\r'; j++) {
			uncrc_buf[j] = data[i+j];
		}

		i += j;

		if (i == data_len) {
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "CRC checking failed Invalid format.Key %s Host %s",
					get_key(mmc->inbuf), mmc->host);
			efree(data);
			LogManager::getLogger()->setCode(CRC_CHKSUM3_FAILED);
			return -2;
		}

		if (i+2 < data_len && data[i] == '\r' && data[i+1] == '\n') {
			uncrc_value = strtol(uncrc_buf, NULL, 16);
			if (uncrc_value == LONG_MAX || uncrc_value == LONG_MIN) {
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "overflow or undeflow \
						happend with crc checksum Key %s Host %s", get_key(mmc->inbuf), mmc->host);
				efree(data);
				return -2;
			}
		} else {
			// the crc data is not in the right format. fail
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "CRC checking failed Invalid format.Key %s Host %s",
				get_key(mmc->inbuf), mmc->host);
			efree(data);
			LogManager::getLogger()->setCode(CRC_CHKSUM4_FAILED);
			return -2;
		}

		crc_hdr_len = i+2;
		hp = data + crc_hdr_len;
		hp_len = data_len - crc_hdr_len;
		has_chksum = true;
	}
#undef PECL_MIN
	else if (mmc->di_algo_in_use != DI_CHKSUM_UNSUPPORTED) {			// New style data integrity (checksum is part of the header)
		has_new_checksum = true;
		// The header flag says there should be checksums, but the header doesnt have it!
		if (chksum == NULL) {
			php_error(E_WARNING, "Data Integrity verification failed, no checksum in header. Key %s Host %s\n",
				get_key(mmc->inbuf), mmc->host);
			*value = data; *value_len = data_len;
			LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED1);
			return CHKSUM_MISMATCH_DETECTED;
		}

		int cnt = sscanf(chksum, "%x:", &chksum_metadata);
		// We dont get the metadata of the checksum
		if (cnt < 1) {
			php_error(E_WARNING, "Data Integrity verification failed, incorrect checksum format for metadata. Chksum %s Key %s Host %s\n",
					chksum, get_key(mmc->inbuf), mmc->host);
			*value = data; *value_len = data_len;
			LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED2);
			return CHKSUM_MISMATCH_DETECTED;
		}

		if ((mmc->di_algo_in_use & DI_CHKSUM_SUPPORTED_OFF) == 0 &&
				(chksum_metadata & DI_CHKSUM_SUPPORTED_OFF) == 0) {
			if (*flags & (MMC_COMPRESSED | MMC_COMPRESSED_LZO | MMC_COMPRESSED_BZIP2)) {
				int cnt = sscanf(chksum, "%x:%x:%x", &chksum_metadata, &crc_value, &uncrc_value);
				// We dont get the 3 components of the checksum
				if (cnt < 3) {
					php_error(E_WARNING, "Data Integrity verification failed, incorrect checksum format"
							"for compressed data. Chksum %s Key %s Host %s\n",
							chksum, get_key(mmc->inbuf), mmc->host);
					*value = data; *value_len = data_len;
					LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED3);
					return CHKSUM_MISMATCH_DETECTED;
				}
			}
			else {
				int cnt = sscanf(chksum, "%x:%x", &chksum_metadata, &uncrc_value);
				// We dont get the 2 components of the checksum
				if (cnt < 2) {
					php_error(E_WARNING, "Data Integrity verification failed, \
							incorrect checksum format. Chksum %s Key %s Host %s\n",
							chksum, get_key(mmc->inbuf), mmc->host);
					*value = data; *value_len = data_len;
					LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED4);
					return CHKSUM_MISMATCH_DETECTED;
				}
			}

			if ((chksum_metadata & mmc->di_algo_in_use) == 0) {
				php_error(E_WARNING, "Cannot verify checksum, mismatch in checksum algorithm. "
						"Expected %x, value in header %x Key %s Host %s\n",
						mmc->di_algo_in_use, chksum_metadata, get_key(mmc->inbuf), mmc->host);
				*value = data; *value_len = data_len;
				LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED4);
				return CHKSUM_MISMATCH_DETECTED;
			}
		}

		unsigned int error_flags = ((DI_CHKSUM_MISMATCH_MCMUX) | (DI_CHKSUM_MISMATCH_MOXI) | (DI_CHKSUM_MISMATCH_MB));
		if (chksum_metadata & error_flags) {
			php_error(E_WARNING, "Data Integrity verification failed, "
					"ckecksum mismatch detected downstream (in %s, integrity algo %x, checksum metadata %x)"
					". Key %s Host %s, header checksum %x\n",
					get_componet_name(chksum_metadata), mmc->di_algo_in_use, chksum_metadata,
					get_key(mmc->inbuf), mmc->host, uncrc_value);
			*value = data; *value_len = data_len;
			switch (chksum_metadata & error_flags) {

				case DI_CHKSUM_MISMATCH_MCMUX:
					LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED_MCMUX);
					break;
				case DI_CHKSUM_MISMATCH_MB:
					LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED_MB);
					break;
				case DI_CHKSUM_MISMATCH_MOXI:
					LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED_MOXI);
					break;
			}
			return CHKSUM_MISMATCH_DETECTED;
		}

		hp = data;
		hp_len = data_len;
		has_chksum = true;
	}

	if (*flags & (MMC_COMPRESSED | MMC_COMPRESSED_LZO | MMC_COMPRESSED_BZIP2)) {
		char *result_data;
		unsigned long result_len = 0;

		if (crc_value) {
			// compute checksum of the compressed data
			unsigned int crc = (has_new_checksum) ? get_checksum_extra(hp, hp_len, *flags) :
				get_checksum(hp, hp_len);
			if (crc != crc_value) {
				char *key = NULL;
				php_error(E_WARNING, "Data Integrity verification failed on compressed data. "
						"Key %s Host %s, calculated crc %x crc in header %x\n",
					get_key(mmc->inbuf), mmc->host, crc, crc_value);
				*value = data; *value_len = data_len;
				LogManager::getLogger()->setCode(CRC_CHKSUM5_FAILED);
				return CHKSUM_MISMATCH_DETECTED;
			}
		}

		if (!mmc_uncompress(&result_data, &result_len, hp, hp_len, *flags)) {
			mmc_server_seterror(mmc, "Failed to uncompress data", 0);
			efree(data);
			php_error_docref(NULL TSRMLS_CC, E_NOTICE, "unable to uncompress data from server=%s", mmc->host);
			LogManager::getLogger()->setCode(UNCOMPRESS_FAILED);
			return -2;
		}

		if (uncrc_value) {
			// compute checksum of the uncompressed data
			unsigned int crc = get_checksum(result_data, result_len);
			if (crc != uncrc_value) {
				php_error(E_WARNING, "Data Integrity verification failed on uncompressed data. \
						Key %s Host %s, uncompressed calculated crc %x, crc in header %x \n",
						get_key(mmc->inbuf), mmc->host, crc, uncrc_value);
				efree(data);
				*value = result_data; *value_len = result_len;
				LogManager::getLogger()->setCode(CRC_CHKSUM6_FAILED);
				return CHKSUM_MISMATCH_DETECTED;
			}
		}
		efree(data);
		data = result_data;
		data_len = result_len;

	} else if (has_chksum) {
		char *result_data;
		unsigned long result_len = 0;

		if (uncrc_value) {
			// compute checksum of the uncompressed data
			unsigned int crc = (has_new_checksum) ? get_checksum_extra(hp, hp_len, *flags) :
				get_checksum(hp, hp_len);
			if (crc != uncrc_value) {
				char *key = NULL;
				php_error(E_WARNING, "Data Integrity verification failed on data. Key %s Host %s. \
						calculated crc %x, crc in header %x\n",
						get_key(mmc->inbuf), mmc->host, crc, uncrc_value);
				*value = data; *value_len = data_len;
				LogManager::getLogger()->setCode(CRC_CHKSUM7_FAILED);
				return CHKSUM_MISMATCH_DETECTED;
			}
		}

		result_len = hp_len;
		result_data = (char *)emalloc(result_len + 3);
		memcpy(result_data, data + crc_hdr_len, result_len);
		efree(data);

		result_data[result_len] = '\r';
		result_data[result_len+1] = '\n';
		result_data[result_len+2] = 0;

		*value = result_data;
		*value_len = result_len;
		return 1;
	}

	*value = data;
	*value_len = data_len;


	return 1;
}
/* }}} */

int mmc_delete(mmc_t *mmc, const char *key, int key_len, int time TSRMLS_DC) /* {{{ */
{
	char *command;
	int command_len, response_len;

	command_len = spprintf(&command, 0, "delete %s %d", key, time);

	MMC_DEBUG(("mmc_delete: trying to delete '%s'", key));

	if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) < 0) {
		efree(command);
		return -1;
	}
	efree(command);

	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0){
		LogManager::getLogger()->setCode(READLINE_FAILED);
		MMC_DEBUG(("failed to read the server's response"));
		return -1;
	}




	MMC_DEBUG(("mmc_delete: server's response is '%s'", mmc->inbuf));

	if(mmc_str_left(mmc->inbuf,"DELETED", response_len, sizeof("DELETED") - 1)) {
		LogManager::getLogger()->setCode(MC_DELETED);
		return 1;
	}

	if(mmc_str_left(mmc->inbuf,"NOT_FOUND", response_len, sizeof("NOT_FOUND") - 1)) {
		LogManager::getLogger()->setCode(MC_NOT_FOUND);
		return 0;
	}

	if(mmc_str(mmc->inbuf, "temporary failure", response_len, sizeof("temporary failure") - 1)) {
		LogManager::getLogger()->setCode(MC_TMP_FAIL);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to delete temporary failure. Item may be locked");
		return 0;
	}

	mmc_server_received_error(mmc, response_len);
	return -1;
}
/* }}} */

static int mmc_flush(mmc_t *mmc, int timestamp TSRMLS_DC) /* {{{ */
{
	char *command;
	int command_len, response_len;

	MMC_DEBUG(("mmc_flush: flushing the cache"));

	if (timestamp) {
		command_len = spprintf(&command, 0, "flush_all %d", timestamp);
	}
	else {
		command_len = spprintf(&command, 0, "flush_all");
	}

	if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) < 0) {
		efree(command);
		return -1;
	}
	efree(command);

	/* get server's response */
	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0){
		return -1;
	}

	MMC_DEBUG(("mmc_flush: server's response is '%s'", mmc->inbuf));

	if(mmc_str_left(mmc->inbuf, "OK", response_len, sizeof("OK") - 1)) {
		return 1;
	}

	mmc_server_received_error(mmc, response_len);
	return -1;
}
/* }}} */

/*
 * STAT 6:chunk_size 64
 */
static int mmc_stats_parse_stat(char *start, char *end, zval *result TSRMLS_DC)  /* {{{ */
{
	char *space, *colon, *key;
	long index = 0;

	/* find space delimiting key and value */
	if ((space = php_memnstr(start, " ", 1, end)) == NULL) {
		return 0;
	}

	/* find colon delimiting subkeys */
	if ((colon = php_memnstr(start, ":", 1, space - 1)) != NULL) {
		zval *element, **elem;
		key = estrndup(start, colon - start);

		/* find existing or create subkey array in result */
		if ((is_numeric_string(key, colon - start, &index, NULL, 0) &&
			zend_hash_index_find(Z_ARRVAL_P(result), index, (void **) &elem) != FAILURE) ||
			zend_hash_find(Z_ARRVAL_P(result), key, colon - start + 1, (void **) &elem) != FAILURE) {
			element = *elem;
		}
		else {
			MAKE_STD_ZVAL(element);
			array_init(element);
			add_assoc_zval_ex(result, key, colon - start + 1, element);
		}

		efree(key);
		return mmc_stats_parse_stat(colon + 1, end, element TSRMLS_CC);
	}

	/* no more subkeys, add value under last subkey */
	key = estrndup(start, space - start);
	add_assoc_stringl_ex(result, key, space - start + 1, space + 1, end - space, 1);
	efree(key);

	return 1;
}
/* }}} */

/*
 * ITEM test_key [3 b; 1157099416 s]
 */
static int mmc_stats_parse_item(char *start, char *end, zval *result TSRMLS_DC)  /* {{{ */
{
	char *space, *value, *value_end, *key;
	zval *element;

	/* find space delimiting key and value */
	if ((space = php_memnstr(start, " ", 1, end)) == NULL) {
		return 0;
	}

	MAKE_STD_ZVAL(element);
	array_init(element);

	/* parse each contained value */
	for (value = php_memnstr(space, "[", 1, end); value != NULL && value <= end; value = php_memnstr(value + 1, ";", 1, end)) {
		do {
			value++;
		} while (*value == ' ' && value <= end);

		if (value <= end && (value_end = php_memnstr(value, " ", 1, end)) != NULL && value_end <= end) {
			add_next_index_stringl(element, value, value_end - value, 1);
		}
	}

	/* add parsed values under key */
	key = estrndup(start, space - start);
	add_assoc_zval_ex(result, key, space - start + 1, element);
	efree(key);

	return 1;
}
/* }}} */

static int mmc_stats_parse_generic(char *start, char *end, zval *result TSRMLS_DC)  /* {{{ */
{
	char *space, *key;

	/* "stats maps" returns "\n" delimited lines, other commands uses "\r\n" */
	if (*end == '\r') {
		end--;
	}

	if (start <= end) {
		if ((space = php_memnstr(start, " ", 1, end)) != NULL) {
			key = estrndup(start, space - start);
			add_assoc_stringl_ex(result, key, space - start + 1, space + 1, end - space, 1);
			efree(key);
		}
		else {
			add_next_index_stringl(result, start, end - start, 1);
		}
	}

	return 1;
}
/* }}} */

static int mmc_get_stats(mmc_t *mmc, char *type, int slabid, int limit, zval *result TSRMLS_DC) /* {{{ */
{
	char *command;
	int command_len, response_len;

	if (slabid) {
		command_len = spprintf(&command, 0, "stats %s %d %d", type, slabid, limit);
	}
	else if (type) {
		command_len = spprintf(&command, 0, "stats %s", type);
	}
	else {
		command_len = spprintf(&command, 0, "stats");
	}

	if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) < 0) {
		efree(command);
		return -1;
	}

	efree(command);
	array_init(result);

	while ((response_len = mmc_readline(mmc TSRMLS_CC)) >= 0) {
		if (mmc_str_left(mmc->inbuf, "ERROR", response_len, sizeof("ERROR") - 1) ||
			mmc_str_left(mmc->inbuf, "CLIENT_ERROR", response_len, sizeof("CLIENT_ERROR") - 1) ||
			mmc_str_left(mmc->inbuf, "SERVER_ERROR", response_len, sizeof("SERVER_ERROR") - 1)) {

			zend_hash_destroy(Z_ARRVAL_P(result));
			FREE_HASHTABLE(Z_ARRVAL_P(result));

			ZVAL_FALSE(result);
			return 0;
		}
		else if (mmc_str_left(mmc->inbuf, "RESET", response_len, sizeof("RESET") - 1)) {
			zend_hash_destroy(Z_ARRVAL_P(result));
			FREE_HASHTABLE(Z_ARRVAL_P(result));

			ZVAL_TRUE(result);
			return 1;
		}
		else if (mmc_str_left(mmc->inbuf, "ITEM ", response_len, sizeof("ITEM ") - 1)) {
			if (!mmc_stats_parse_item(mmc->inbuf + (sizeof("ITEM ") - 1), mmc->inbuf + response_len - sizeof("\r\n"), result TSRMLS_CC)) {
				zend_hash_destroy(Z_ARRVAL_P(result));
				FREE_HASHTABLE(Z_ARRVAL_P(result));
				return -1;
			}
		}
		else if (mmc_str_left(mmc->inbuf, "STAT ", response_len, sizeof("STAT ") - 1)) {
			if (!mmc_stats_parse_stat(mmc->inbuf + (sizeof("STAT ") - 1), mmc->inbuf + response_len - sizeof("\r\n"), result TSRMLS_CC)) {
				zend_hash_destroy(Z_ARRVAL_P(result));
				FREE_HASHTABLE(Z_ARRVAL_P(result));
				return -1;
			}
		}
		else if (mmc_str_left(mmc->inbuf, "END", response_len, sizeof("END") - 1)) {
			break;
		}
		else if (!mmc_stats_parse_generic(mmc->inbuf, mmc->inbuf + response_len - sizeof("\n"), result TSRMLS_CC)) {
			zend_hash_destroy(Z_ARRVAL_P(result));
			FREE_HASHTABLE(Z_ARRVAL_P(result));
			return -1;
		}
	}

	if (response_len < 0) {
		zend_hash_destroy(Z_ARRVAL_P(result));
		FREE_HASHTABLE(Z_ARRVAL_P(result));
		return -1;
	}

	return 1;
}
/* }}} */

static int mmc_incr_decr(mmc_t *mmc, int cmd, char *key, int key_len, int value, long *number TSRMLS_DC) /* {{{ */
{
	char *command;
	int  command_len, response_len;

	LogManager::getLogger()->setCommandType(SET);
	LogManager::getLogger()->setHost(mmc->host);

	if (cmd > 0) {
		command_len = spprintf(&command, 0, "incr %s %d", key, value);
	}
	else {
		command_len = spprintf(&command, 0, "decr %s %d", key, value);
	}

	if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) < 0) {
		efree(command);
		return -1;
	}
	efree(command);

	if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0) {
		MMC_DEBUG(("failed to read the server's response"));
		return -1;
	}



	MMC_DEBUG(("mmc_incr_decr: server's answer is: '%s'", mmc->inbuf));
	if (mmc_str_left(mmc->inbuf, "NOT_FOUND", response_len, sizeof("NOT_FOUND") - 1)) {
		LogManager::getLogger()->setCode(MC_NOT_FOUND);
		MMC_DEBUG(("failed to %sement variable - item with such key not found", cmd > 0 ? "incr" : "decr"));
		return 0;
	} else if (mmc_str(mmc->inbuf, "non-numeric value", response_len, sizeof("non-numeric value") - 1)) {
		LogManager::getLogger()->setCode(MC_NON_NUMERIC_VALUE);
		MMC_DEBUG(("failed to %sement variable - item is non numeric value", cmd > 0 ? "incr" : "decr"));
		return 0;
	} else if (mmc_str(mmc->inbuf, "temporary failure", response_len, sizeof("temporary failure") - 1)) {
		LogManager::getLogger()->setCode(MC_TMP_FAIL);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to incr/decr, temporary failure. Item may be locked");
		return 0;
	} else if (mmc_str_left(mmc->inbuf, "ERROR", response_len, sizeof("ERROR") - 1) ||
			 mmc_str_left(mmc->inbuf, "CLIENT_ERROR", response_len, sizeof("CLIENT_ERROR") - 1) ||
			 mmc_str_left(mmc->inbuf, "SERVER_ERROR", response_len, sizeof("SERVER_ERROR") - 1)) {
		LogManager::getLogger()->setCode(MC_ERROR);
		mmc_server_received_error(mmc, response_len);
		return -1;
	}

	*number = (long)atol(mmc->inbuf);
	return 1;
}
/* }}} */

static int php_mmc_store(zval * mmc_object, char *key, int key_len, zval *value, int flags, int expire, unsigned long &cas, char *shard_key, int shard_key_len, char *command, int command_len, zend_bool by_key, zval *val_len) /* {{{ */
{
	mmc_pool_t *pool;
	int result;
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	char shard_key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int shard_key_tmp_len = 0;

	php_serialize_data_t value_hash;
	smart_str buf = {0};

	// If by key is true then lets validate the input
	if (by_key) {
		// Not sure why they validate input like this, but hey... if it works, why not...
		if (mmc_prepare_key_ex(shard_key, shard_key_len, shard_key_tmp, &shard_key_tmp_len TSRMLS_CC) != MMC_OK) {
			LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
			return 0;
		}
	}

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		return 0;
	}

	LogManager::getLogger()->setKey(key);

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		return 0;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	/* If the cas == 0 and the command is 'cas', error */
	if (cas == 0 && command[0] == 'c') {
		mmc_t *mmc = NULL;
		if (by_key) {
			mmc = mmc_pool_find(pool, shard_key, shard_key_len TSRMLS_CC);
		} else {
			mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC);
		}
		if (mmc) {
			LogManager::getLogger()->setHost(mmc->host);
		}
		LogManager::getLogger()->setCode(INVALID_CAS);
		return 0;
	}


	switch (Z_TYPE_P(value)) {
		case IS_STRING:
			flags = flags & ~(MMC_SERIALIZED_IGBINARY);
			result = mmc_pool_store(
				pool, command, command_len, key_tmp, key_tmp_len, flags, expire, cas,
				Z_STRVAL_P(value), Z_STRLEN_P(value), by_key, shard_key_tmp, shard_key_tmp_len, val_len TSRMLS_CC);
			break;

		case IS_LONG:
		case IS_DOUBLE:
		case IS_BOOL: {
			zval value_copy;
			flags = flags & ~(MMC_SERIALIZED_IGBINARY);

			/* FIXME: we should be using 'Z' instead of this, but unfortunately it's PHP5-only */
			value_copy = *value;
			zval_copy_ctor(&value_copy);
			convert_to_string(&value_copy);

			result = mmc_pool_store(
				pool, command, command_len, key_tmp, key_tmp_len, flags, expire, cas,
				Z_STRVAL(value_copy), Z_STRLEN(value_copy), by_key, shard_key_tmp, shard_key_tmp_len, val_len TSRMLS_CC);

			zval_dtor(&value_copy);
			break;
		}

		default: {
			zval value_copy, *value_copy_ptr;

			/* FIXME: we should be using 'Z' instead of this, but unfortunately it's PHP5-only */
			value_copy = *value;
			zval_copy_ctor(&value_copy);
			value_copy_ptr = &value_copy;

			LogManager::getLogger()->startSerialTime();
			LogManager::saveLogger();

 			if(flags & MMC_SERIALIZED_IGBINARY) {
 			#ifdef HAVE_MEMCACHE_IGBINARY
 				result = igbinary_serialize((uint8_t **) &buf.c, &buf.len, value TSRMLS_CC);
 				if( result != 0 ) {
 					smart_str_free(&buf);
 					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to igbinary_serialize value");
 					return 0;
 				}
 				flags |= MMC_SERIALIZED_IGBINARY; /* clarity's sake */
 			#else
 				php_error_docref(NULL TSRMLS_CC, E_WARNING, "Missing igbinary support");
 				return 0;
 			#endif
 			}
 			else {
 				PHP_VAR_SERIALIZE_INIT(value_hash);
 				php_var_serialize(&buf, &value_copy_ptr, &value_hash TSRMLS_CC);
 				PHP_VAR_SERIALIZE_DESTROY(value_hash);

 				if (!buf.c) {
 					/* something went really wrong */
 					zval_dtor(&value_copy);
 					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to serialize value");
 					return 0;
 				}

 				flags |= MMC_SERIALIZED;
 			}

			LogManager::restoreLogger();
			LogManager::getLogger()->stopSerialTime();

			zval_dtor(&value_copy);

			result = mmc_pool_store(
				pool, command, command_len, key_tmp, key_tmp_len, flags, expire, cas,
				buf.c, buf.len, by_key, shard_key_tmp, shard_key_tmp_len, val_len TSRMLS_CC);
		}
	}
	if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
		smart_str_free(&buf);
	}

	if (result > 0) {
		return 1;
	}

	return 0;
}
/* }}} */

static void php_mmc_incr_decr(mmc_pool_t *pool, char *key, int key_len, char *shard_key, int shard_key_len, long value, zend_bool by_key, int cmd, zval **return_value) /* {{{ */
{
	mmc_t *mmc;
	int result = -1;
	long number;

	while (result < 0) {
		if (by_key) {
			mmc = mmc_pool_find(pool, shard_key, shard_key_len TSRMLS_CC);
		} else {
			mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC);
		}

		if(mmc == NULL || mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
			if (mmc) {
				LogManager::getLogger()->setHost(mmc->host);
			} else {
				LogManager::getLogger()->setHost(PROXY_STR);
			}

			break;
		}

		if ((result = mmc_incr_decr(mmc, cmd, key, key_len, value, &number TSRMLS_CC)) < 0) {
			mmc_server_failure(mmc TSRMLS_CC);
		}
	}

	if (result > 0) {
		ZVAL_LONG(*return_value, number);
	} else {
		ZVAL_BOOL(*return_value, 0)
	}

	return;
}
/* }}} */

static void php_mmc_connect (INTERNAL_FUNCTION_PARAMETERS, int persistent) /* {{{ */
{
	zval **connection, *mmc_object = getThis();
	mmc_t *mmc = NULL;
	mmc_pool_t *pool;
	int resource_type, host_len, errnum = 0, list_id, failed = 0;
	char *host, *error_string = NULL;
	long port = MEMCACHE_G(default_port), timeout = MEMCACHE_G(default_timeout_ms) / 1000, timeoutms = 0;
	zend_bool use_binary = 0;
	int retry_interval = MEMCACHE_G(retry_interval);
	mc_logger_t logData;
	LogManager lm(&logData);
	char *version_str = NULL;

	LogManager::getLogger()->setCmd("connect");

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lllb", &host, &host_len, &port, &timeout, &timeoutms, &use_binary) == FAILURE) {
		LogManager::getLogger()->setCode(PARSE_ERROR);
		return;
	}

	if (timeoutms < 1) {
		timeoutms = MEMCACHE_G(default_timeout_ms);
	}

	/* initialize and connect server struct */
	if (persistent) {
		mmc = mmc_find_persistent(host, host_len, port, timeout, retry_interval, use_binary TSRMLS_CC);
	}
	else {
		MMC_DEBUG(("php_mmc_connect: creating regular connection"));
		mmc = mmc_server_new(host, host_len, port, 0, timeout, retry_interval, use_binary TSRMLS_CC);
	}

	LogManager::getLogger()->setHost(host);

	mmc->timeout = timeout;
	mmc->connect_timeoutms = timeoutms;

	failed = 0;
	if (MEMCACHE_G(proxy_enabled)) {
		mmc->proxy = mmc_get_proxy(mmc TSRMLS_C);
		if (mmc->proxy == NULL) {
		    failed = 1;
			LogManager::getLogger()->setCode(PROXY_CONNECT_FAILED);
		} else {
		    failed = ((version_str = mmc_get_version(mmc TSRMLS_CC)) == NULL)? 1: 0;
			if (failed) {
				LogManager::getLogger()->setCode(VERSION_FAILED);
			}
			if (version_str) {
				efree(version_str);
			}
		}
	}

	if (!failed && mmc->proxy == NULL) {
		failed = mmc_open(mmc, 1, &error_string, &errnum TSRMLS_CC)? 0: 1;
		if (failed) {
			LogManager::getLogger()->setCode(PROXY_CONNECT_FAILED);
		}
	}

	if (failed) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't connect to %s:%ld, %s (%d)", host, port, error_string ? error_string : "Unknown error", errnum);

		mmc_server_failure(mmc TSRMLS_CC);

		if (!persistent) {
			mmc_server_free(mmc TSRMLS_CC);
		} else {
			mmc_server_sleep(mmc TSRMLS_CC);
		}

		if (error_string) {
			efree(error_string);
		}
		RETURN_FALSE;
	}

	/* initialize pool and object if need be */
	if (!mmc_object) {
		pool = mmc_pool_new(TSRMLS_C);
		mmc_pool_add(pool, mmc, 1);

		object_init_ex(return_value, memcache_class_entry_ptr);
		list_id = zend_list_insert(pool, le_memcache_pool);
		add_property_resource(return_value, "connection", list_id);
		LogManager::getLogger()->setLogName(pool->log_name);

	}
	else if (zend_hash_find(Z_OBJPROP_P(mmc_object), "connection", sizeof("connection"), (void **) &connection) != FAILURE) {
		pool = (mmc_pool_t *) zend_list_find(Z_LVAL_PP(connection), &resource_type);
		if (!pool || resource_type != le_memcache_pool) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unknown connection identifier");
			LogManager::getLogger()->setCode(INTERNAL_ERROR);
			RETURN_FALSE;
		}
		mmc_pool_add(pool, mmc, 1);
		LogManager::getLogger()->setLogName(pool->log_name);

		RETURN_TRUE;
	}
	else {
		pool = mmc_pool_new(TSRMLS_C);
		mmc_pool_add(pool, mmc, 1);

		list_id = zend_list_insert(pool, le_memcache_pool);
		add_property_resource(mmc_object, "connection", list_id);
		LogManager::getLogger()->setLogName(pool->log_name);

		RETURN_TRUE;
	}

}
/* }}} */

static void mmc_init_multi(TSRMLS_D)
{
	MEMCACHE_G(in_multi) = 1;
}

static void mmc_free_multi(TSRMLS_D)
{
	std::map<std::string, mmc_t*>::iterator itr = temp_multiget_proxy_mmc.begin();
	while (itr != temp_multiget_proxy_mmc.end()) {
		mmc_t *mmc = itr->second;
		mmc_server_free(mmc TSRMLS_CC);
		temp_multiget_proxy_mmc.erase(itr++);
	}

	MEMCACHE_G(in_multi) = 0;
}


mmc_t* get_proxy_for_multi(mmc_t *mmc TSRMLS_DC)
{
	char *host = MEMCACHE_G(proxy_host);
	long port = MEMCACHE_G(proxy_port);
	int host_len = MEMCACHE_G(proxy_hostlen);
	mmc_t *proxy_mmc;
	char *error_string = NULL;
	int errnum = 0;
	int timeout = 1000;
	std::map<std::string, mmc_t*>::iterator itr;
	std::string skey;

	if (MEMCACHE_G(proxy_connect_failed) || !mmc) {
		return NULL;
	}

	skey.assign(mmc->proxy_str);
	itr = temp_multiget_proxy_mmc.find(skey);

	if(itr != temp_multiget_proxy_mmc.end()) {
		return itr->second;
	}

	proxy_mmc = mmc_server_new(host, host_len, port, 0, timeout, 0, 0 TSRMLS_CC);

   if (!mmc_open(proxy_mmc, 1, &error_string, &errnum TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't connect to mc proxy: %s:%ld, %s (%d)", host, port, error_string ? error_string : "Unknown error", errnum);
		mmc_server_sleep(proxy_mmc TSRMLS_CC);

		if (error_string) {
		   efree(error_string);
		}

	   MEMCACHE_G(proxy_connect_failed) = 1;
	   mmc_server_free(proxy_mmc TSRMLS_CC);
	   proxy_mmc = NULL;
	}
	else {
		temp_multiget_proxy_mmc[skey] = proxy_mmc;
	}

	return proxy_mmc;
}

mmc_t *mmc_get_proxy(mmc_t *mmc TSRMLS_DC) /* {{{ */
{
	char *host, *error_string = NULL;
	int host_len, timeout = 1000;
	long port;
	int errnum = 0;
	mmc_t *proxy_mmc;

	host = MEMCACHE_G(proxy_host);
	port = MEMCACHE_G(proxy_port);
	host_len = MEMCACHE_G(proxy_hostlen);

	if (!host) return NULL;

	if (MEMCACHE_G(in_multi)) {
		return get_proxy_for_multi(mmc TSRMLS_DC);
	}

	proxy_mmc = mmc_find_persistent(host, host_len, port, timeout, 0, 0 TSRMLS_CC);

   if (!mmc_open(proxy_mmc, 1, &error_string, &errnum TSRMLS_CC)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Can't connect to mc proxy: %s:%ld, %s (%d)", host, port, error_string ? error_string : "Unknown error", errnum);
		mmc_server_sleep(proxy_mmc TSRMLS_CC);

		if (error_string) {
		   efree(error_string);
		}

		proxy_mmc = NULL;
   }

   return proxy_mmc;
}
/* }}} */

/* ----------------
   module functions
   ---------------- */

/* {{{ proto object memcache_connect( string host [, int port [, int timeout ] ])
   Connects to server and returns a Memcache object */
PHP_FUNCTION(memcache_connect)
{
	php_mmc_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */

/* {{{ proto object memcache_pconnect( string host [, int port [, int timeout ] ])
   Connects to server and returns a Memcache object */
PHP_FUNCTION(memcache_pconnect)
{
	php_mmc_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}

/* }}} */

/* {{{ proto bool memcache_add_server( string host [, int port [, bool persistent [, int weight [, int timeout [, int retry_interval [, bool status [, callback failure_callback , [ timeout ms, use_binary] ] ] ] ] ] ] ] ])
   Adds a connection to the pool. The order in which this function is called is significant */
PHP_FUNCTION(memcache_add_server)
{
	zval **connection, *mmc_object = getThis(), *failure_callback = NULL;
	mmc_pool_t *pool;
	mmc_t *mmc;
	long port = MEMCACHE_G(default_port), weight = 1, timeout = MEMCACHE_G(default_timeout_ms) / 1000, retry_interval = MEMCACHE_G(retry_interval), timeoutms = 0;
	zend_bool persistent = 1, status = 1;
	int resource_type, host_len, list_id;
	char *host;
	zend_bool use_binary = 0;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("addServer");

	if (mmc_object) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lblllbzlb", &host, &host_len, &port, &persistent, &weight, &timeout, &retry_interval, &status, &failure_callback, &timeoutms, &use_binary) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|lblllbzlb", &mmc_object, memcache_class_entry_ptr, &host, &host_len, &port, &persistent, &weight, &timeout, &retry_interval, &status, &failure_callback, &timeoutms, &use_binary) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	LogManager::getLogger()->setHost(host);

	if (timeoutms < 1) {
		timeoutms = MEMCACHE_G(default_timeout_ms);
	}

	if (weight <= 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "weight must be a positive integer");
		RETURN_FALSE;
	}

	if (failure_callback != NULL && Z_TYPE_P(failure_callback) != IS_NULL) {
		if (!IS_CALLABLE(failure_callback, 0, NULL)) {
			LogManager::getLogger()->setCode(INVALID_CB);
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid failure callback");
			RETURN_FALSE;
		}
	}

	if (retry_interval < 3 || retry_interval > 30) {
		retry_interval = MEMCACHE_G(retry_interval);
	}

	/* lazy initialization of server struct */
	if (persistent) {
		mmc = mmc_find_persistent(host, host_len, port, timeout, retry_interval, use_binary TSRMLS_CC);
	}
	else {
		MMC_DEBUG(("memcache_add_server: initializing regular struct"));
		mmc = mmc_server_new(host, host_len, port, 0, timeout, retry_interval, use_binary TSRMLS_CC);
	}

	mmc->connect_timeoutms = timeoutms;

	/* add server in failed mode */
	if (!status) {
		mmc->status = MMC_STATUS_FAILED;
	}

	if (failure_callback != NULL && Z_TYPE_P(failure_callback) != IS_NULL) {
		mmc->failure_callback = failure_callback;
		mmc_server_callback_ctor(&mmc->failure_callback TSRMLS_CC);
	}

	/* initialize pool if need be */
	if (zend_hash_find(Z_OBJPROP_P(mmc_object), "connection", sizeof("connection"), (void **) &connection) == FAILURE) {
		pool = mmc_pool_new(TSRMLS_C);
		list_id = zend_list_insert(pool, le_memcache_pool);
		add_property_resource(mmc_object, "connection", list_id);
	}
	else {
		pool = (mmc_pool_t *) zend_list_find(Z_LVAL_PP(connection), &resource_type);
		if (!pool || resource_type != le_memcache_pool) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Failed to extract 'connection' variable from object");
			LogManager::getLogger()->setCode(INTERNAL_ERROR);
			RETURN_FALSE;
		}
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	mmc_pool_add(pool, mmc, weight);
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_set_server_params( string host [, int port [, int timeout [, int retry_interval [, bool status [, callback failure_callback ] ] ] ] ])
   Changes server parameters at runtime */
PHP_FUNCTION(memcache_set_server_params)
{
	zval *mmc_object = getThis(), *failure_callback = NULL;
	mmc_pool_t *pool;
	mmc_t *mmc = NULL;
	long port = MEMCACHE_G(default_port), timeout = MEMCACHE_G(default_timeout_ms) / 1000, retry_interval = MEMCACHE_G(retry_interval);
	zend_bool status = 1;
	int host_len, i;
	char *host;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("setserverparams");

	if (mmc_object) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|lllbz", &host, &host_len, &port, &timeout, &retry_interval, &status, &failure_callback) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|lllbz", &mmc_object, memcache_class_entry_ptr, &host, &host_len, &port, &timeout, &retry_interval, &status, &failure_callback) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	LogManager::getLogger()->setHost(host);

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	for (i=0; i<pool->num_servers; i++) {
		if (!strcmp(pool->servers[i]->host, host) && pool->servers[i]->port == port) {
			mmc = pool->servers[i];
			break;
		}
	}

	if (!mmc) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Server not found in pool");
		LogManager::getLogger()->setCode(SVR_NT_FOUND);
		RETURN_FALSE;
	}

	if (failure_callback != NULL && Z_TYPE_P(failure_callback) != IS_NULL) {
		if (!IS_CALLABLE(failure_callback, 0, NULL)) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Invalid failure callback");
			LogManager::getLogger()->setCode(SVR_NT_FOUND);
			RETURN_FALSE;
		}
	}

	if (retry_interval < 3 || retry_interval > 30) {
		retry_interval = MEMCACHE_G(retry_interval);
	}

	if (timeout < 1) {
		timeout = MEMCACHE_G(default_timeout_ms);
	}

	mmc->timeout = timeout;
	mmc->retry_interval = retry_interval;

	if (!status) {
		mmc->status = MMC_STATUS_FAILED;
	}
	else if (mmc->status == MMC_STATUS_FAILED) {
		mmc->status = MMC_STATUS_DISCONNECTED;
	}

	if (failure_callback != NULL) {
		if (mmc->failure_callback != NULL) {
			mmc_server_callback_dtor(&mmc->failure_callback TSRMLS_CC);
		}

		if (Z_TYPE_P(failure_callback) != IS_NULL) {
			mmc->failure_callback = failure_callback;
			mmc_server_callback_ctor(&mmc->failure_callback TSRMLS_CC);
		}
		else {
			mmc->failure_callback = NULL;
		}
	}


	RETURN_TRUE;
}
/* }}} */

/* {{{ proto int memcache_get_server_status( string host [, int port ])
   Returns server status (0 if server is failed, otherwise non-zero) */
PHP_FUNCTION(memcache_get_server_status)
{
	zval *mmc_object = getThis();
	mmc_pool_t *pool;
	mmc_t *mmc = NULL;
	long port = MEMCACHE_G(default_port);
	int host_len, i;
	char *host;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("getserverstatus");

	if (mmc_object) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &host, &host_len, &port) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|l", &mmc_object, memcache_class_entry_ptr, &host, &host_len, &port) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	for (i=0; i<pool->num_servers; i++) {
		if (!strcmp(pool->servers[i]->host, host) && pool->servers[i]->port == port) {
			mmc = pool->servers[i];
			break;
		}
	}

	if (!mmc) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Server not found in pool");
		LogManager::getLogger()->setCode(SVR_NT_FOUND);
		RETURN_FALSE;
	}


	RETURN_LONG(mmc->status);
}
/* }}} */

mmc_t *mmc_find_persistent(char *host, int host_len, int port, int timeout, int retry_interval, int use_binary TSRMLS_DC) /* {{{ */
{
	mmc_t *mmc;
	zend_rsrc_list_entry *le;
	char *hash_key;
	int hash_key_len;

	MMC_DEBUG(("mmc_find_persistent: seeking for persistent connection"));
	hash_key_len = spprintf(&hash_key, 0, "mmc_connect___%d:%s:%d", getpid(), host, port);

	if (zend_hash_find(&EG(persistent_list), hash_key, hash_key_len+1, (void **) &le) == FAILURE) {
		zend_rsrc_list_entry new_le;
		MMC_DEBUG(("mmc_find_persistent: connection wasn't found in the hash"));

		mmc = mmc_server_new(host, host_len, port, 1, timeout, retry_interval, use_binary TSRMLS_CC);
		new_le.type = le_pmemcache;
		new_le.ptr  = mmc;

		/* register new persistent connection */
		if (zend_hash_update(&EG(persistent_list), hash_key, hash_key_len+1, (void *) &new_le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
			mmc_server_free(mmc TSRMLS_CC);
			mmc = NULL;
		} else {
			zend_list_insert(mmc, le_pmemcache);
		}
	}
	else if (le->type != le_pmemcache || le->ptr == NULL) {
		zend_rsrc_list_entry new_le;
		MMC_DEBUG(("mmc_find_persistent: something was wrong, reconnecting.."));
		zend_hash_del(&EG(persistent_list), hash_key, hash_key_len+1);

		mmc = mmc_server_new(host, host_len, port, 1, timeout, retry_interval, use_binary TSRMLS_CC);
		new_le.type = le_pmemcache;
		new_le.ptr  = mmc;

		/* register new persistent connection */
		if (zend_hash_update(&EG(persistent_list), hash_key, hash_key_len+1, (void *) &new_le, sizeof(zend_rsrc_list_entry), NULL) == FAILURE) {
			mmc_server_free(mmc TSRMLS_CC);
			mmc = NULL;
		}
		else {
			zend_list_insert(mmc, le_pmemcache);
		}
	}
	else {
		MMC_DEBUG(("mmc_find_persistent: connection found in the hash"));
		mmc = (mmc_t *)le->ptr;
		mmc->timeout = timeout;
		mmc->retry_interval = retry_interval;

		/* attempt to reconnect this node before failover in case connection has gone away */
		if (mmc->status == MMC_STATUS_CONNECTED) {
			mmc->status = MMC_STATUS_UNKNOWN;
		}
	}

	efree(hash_key);
	return mmc;
}
/* }}} */

/* {{{ proto string memcache_get_version( object memcache )
   Returns server's version */
PHP_FUNCTION(memcache_get_version)
{
	mmc_pool_t *pool;
	char *version;
	int i;
	zval *mmc_object = getThis();
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("version");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &mmc_object, memcache_class_entry_ptr) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	for (i=0; i<pool->num_servers; i++) {

		if (MEMCACHE_G(proxy_enabled)) {
			pool->servers[i]->proxy = mmc_get_proxy(pool->servers[i] TSRMLS_C);
		}

		if (mmc_open(pool->servers[i], 1, NULL, NULL TSRMLS_CC)) {
			if ((version = mmc_get_version(pool->servers[i] TSRMLS_CC)) != NULL) {
				RETURN_STRING(version, 0);
			}
			else {
				mmc_server_failure(pool->servers[i] TSRMLS_CC);
			}
		}
	}

	LogManager::getLogger()->setCode(SVR_OPN_FAILED);
	RETURN_FALSE;
}
/* }}} */

static void php_handle_store_command(INTERNAL_FUNCTION_PARAMETERS, char *command, int command_len, zend_bool by_key TSRMLS_DC) {
	zval *value;
	zval *mmc_object = getThis();
	int key_len;
	int shard_key_len;
	char *key;
	char *shard_key;
	long flag = 0;
	long expire = 0;
	unsigned long cas = 0;
	zval *val_len = 0, *zval_cas = 0;
	mc_logger_t logData;
	LogManager lm(&logData);
	char bykey_cmd[MMC_KEY_MAX_SIZE];

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Osz|llzsz", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &value, &flag, &expire, &zval_cas, &shard_key, &shard_key_len, &val_len) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz|llzsz", &key, &key_len, &value, &flag, &expire, &zval_cas, &shard_key, &shard_key_len, &val_len) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (by_key) {
		strncpy(bykey_cmd, command, command_len);
		strncpy(bykey_cmd + command_len, "ByKey", sizeof("ByKey"));
		LogManager::getLogger()->setCmd(bykey_cmd);
	} else {
		LogManager::getLogger()->setCmd(command);
	}

	LogManager::getLogger()->setFlags(flag);
	LogManager::getLogger()->setExpiry(expire);
	LogManager::getLogger()->setCommandType(SET);

	if (zval_cas) {
		switch(Z_TYPE_P(zval_cas)) {
			case IS_LONG:
				cas = Z_LVAL_P(zval_cas);
				break;
			case IS_STRING:
				double dval;
				if (is_numeric_string(Z_STRVAL_P(zval_cas), Z_STRLEN_P(zval_cas), (long *)&cas, &dval, 0) == IS_LONG) {
					break;
				}
			default:
				LogManager::getLogger()->setCode(PARSE_ERROR);
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Invalid CAS value");
				RETURN_FALSE;
		}
	}

	if (val_len) {
		convert_to_long(val_len);
	}

	if(php_mmc_store(mmc_object, key, key_len, value, flag, expire, cas, shard_key, shard_key_len, command, command_len, by_key, val_len)) {
		if (cas > 0 && zval_cas) {
			if (Z_TYPE_P(zval_cas) == IS_STRING) {
				efree(Z_STRVAL_P(zval_cas));
			}
			ZVAL_LONG(zval_cas, cas);
		}
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}

	return;
}

static void php_handle_multi_store_command(INTERNAL_FUNCTION_PARAMETERS, char * command, int command_len TSRMLS_DC) {
	zval *zkey_array;
	zval *mmc_object = getThis();
	zval *val_len = 0;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|z", &mmc_object, memcache_class_entry_ptr, &zkey_array, &val_len) == FAILURE) {
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|z", &zkey_array, &val_len) == FAILURE) {
			return;
		}
	}

	//Validate inputs
	if (zkey_array != NULL) {
		if (Z_TYPE_P(zkey_array) == IS_ARRAY) {
			if (val_len) {
				zval *tmp;
				MAKE_STD_ZVAL(tmp);
				ZVAL_NULL(tmp);
				php_mmc_store_multi_by_key(mmc_object, zkey_array, command, command_len, &return_value, &tmp TSRMLS_CC);
				REPLACE_ZVAL_VALUE(&val_len, tmp, 0);
				FREE_ZVAL(tmp);
			}
			else {
				php_mmc_store_multi_by_key(mmc_object, zkey_array, command, command_len, &return_value, 0 TSRMLS_CC);
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was not of expected array type");
			RETURN_FALSE;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was null");
		RETURN_FALSE;
	}

	return;
}

/* {{{ proto bool memcache_add( object memcache, string key, mixed var [, int flag [, int expire ] ] )
   Adds new item. Item with such key should not exist. */
PHP_FUNCTION(memcache_add)
{
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "add", sizeof("add") - 1, 0);
}
/* }}} */

/* {{{ proto bool memcache_set( object memcache, string key, mixed var [, int flag [, int expire ] ] )
   Sets the value of an item. Item may exist or not */
PHP_FUNCTION(memcache_set)
{
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "set", sizeof("set") - 1, 0);
}
/* }}} */

/* {{{ proto bool memcache_append( object memcache, string key, mixed var [, int flag [, int expire ] ] )
   Appends to the value of an item. Item may exist or not */
PHP_FUNCTION(memcache_append)
{
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "append", sizeof("append") - 1, 0);
}
/* }}} */

/**
 * "By key" version of memcache_append.
 * true will be returned if successful and false will be returned if key doesn't
 * exist or if there was some other issue storing the key/value in memcache.
 */
 /*
php_function(memcache_appendByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "append", sizeof("append") - 1, 0);
}
*/

/* {{{ proto bool memcache_prepend( object memcache, string key, mixed var [, int flag [, int expire ] ] )
   Prepends to the value of an item. Item may exist or not */
PHP_FUNCTION(memcache_prepend)
{
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "prepend", sizeof("prepend") - 1, 0);
}
/* }}} */

/* {{{ proto bool memcache_cas(object memcache, mixed key [, mixed var [, int flag [, int exptime [, long cas ] ] ] ])
   Sets the value of an item if the CAS value is the same (Compare-And-Swap)  */
PHP_FUNCTION(memcache_cas)
{
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "cas", sizeof("cas") - 1, 0);
}
/* }}} */

/* {{{ proto bool memcache_replace( object memcache, string key, mixed var [, int flag [, int expire ] ] )
   Replaces existing item. Returns false if item doesn't exist */
PHP_FUNCTION(memcache_replace)
{
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "replace", sizeof("replace") - 1, 0);
}
/* }}} */

/* {{{ proto bool memcache_get2( object memcache, mixed key, mixed &val [, mixed &flags [, mixed &cas ] ] )
   gets the value of existing item or false. Returns false if the command fails */
PHP_FUNCTION(memcache_get2)
{
	mmc_pool_t *pool;
	zval *zkey, *zvalue, *mmc_object = getThis(), *flags = NULL, *cas = NULL;
	mc_logger_t logData;
	LogManager lm(&logData);

	char key[MMC_KEY_MAX_SIZE];

	LogManager::getLogger()->setCmd("get2");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ozz|zz", &mmc_object, memcache_class_entry_ptr, &zkey, &zvalue, &flags, &cas) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz|zz", &zkey, &zvalue, &flags, &cas) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);
	ZVAL_NULL(return_value);

	zval *tmp;
	MAKE_STD_ZVAL(tmp);
	ZVAL_NULL(tmp);

	zend_bool old_false_on_failure = pool->false_on_error;
	pool->false_on_error = 1;
	php_mmc_get(pool, zkey, key, &tmp, &return_value, flags, cas, false);
	pool->false_on_error = old_false_on_failure;

	REPLACE_ZVAL_VALUE(&zvalue, tmp, 0);
	FREE_ZVAL(tmp);

	if (IS_ARRAY == Z_TYPE_P(return_value))
		return;

	if (Z_TYPE_P(zvalue) == IS_BOOL && Z_BVAL_P(zvalue) == 0) {
		RETURN_FALSE;
	} else if (!old_false_on_failure && Z_TYPE_P(zvalue) == IS_NULL) {
		zval_dtor(zvalue);
		ZVAL_FALSE(zvalue);
	}

	RETURN_TRUE;
}

/**
 * Stores a key/values in memcache.  If key already exists then it is overridden.
 */
PHP_FUNCTION(memcache_setByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "set", sizeof("set") - 1, 1);
}

/**
 * Stores an array of key/values in memcache.  If key already exists then it is overridden.
 *
 * See php_mmc_store_multi_by_key method documentation for example inputs and expected return values.
 */
PHP_FUNCTION(memcache_setMultiByKey) {
	php_handle_multi_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "set", sizeof("set") - 1);
}

/**
 * Adds a key/values in memcache.  True will be returned if successful and false will be returned if key already exists
 * or if there was some other issue storing the key/value in memcache.
 */
PHP_FUNCTION(memcache_addByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "add", sizeof("add") - 1, 1);
}

/**
 * Adds an array of key/values in memcache.
 *
 * See php_mmc_store_multi_by_key method documentation for example inputs and expected return values.
 */
PHP_FUNCTION(memcache_addMultiByKey) {
	php_handle_multi_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "add", sizeof("add") - 1);
}

/**
 * Replace a key/values in memcache.  True will be returned if successful and false will be returned if key doesn't
 * exist or if there was some other issue storing the key/value in memcache.
 */
PHP_FUNCTION(memcache_replaceByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "replace", sizeof("replace") - 1, 1);
}

/**
 * Replaces an array of key/values in memcache.
 *
 * See php_mmc_store_multi_by_key method documentation for example inputs and expected return values.
 */
PHP_FUNCTION(memcache_replaceMultiByKey) {
	php_handle_multi_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "replace", sizeof("replace") - 1);
}

/**
 * Compare and Swap a key/values in memcache.  True if successful and the cas value is the same. False will be returned
 * if key doesn't exist or if there was some other issue storing the key/value in memcache.
 */
PHP_FUNCTION(memcache_casByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "cas", sizeof("cas") - 1, 1);
}

/**
 * Compre and Swap an array of key/values in memcache.
 *
 * See php_mmc_store_multi_by_key method documentation for example inputs and expected return values.
 */
PHP_FUNCTION(memcache_casMultiByKey) {
	php_handle_multi_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "cas", sizeof("cas") - 1);
}

/**
 * "By key" version of memcache_append.
 * true will be returned if successful and false will be returned if key doesn't
 * exist or if there was some other issue storing the key/value in memcache.
 */
PHP_FUNCTION(memcache_appendByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "append", sizeof("append") - 1, 1);
}

/**
 * "By Key" version of memcache_prepend.
 * true will be returned if successful and false will be returned if key doesn't
 * exist or if there was some other issue storing the key/value in memcache.
 */
PHP_FUNCTION(memcache_prependByKey) {
	php_handle_store_command(INTERNAL_FUNCTION_PARAM_PASSTHRU, "prepend", sizeof("prepend") - 1, 1);
}

/**
 * Executes a store (aka memcache_set) command for every key in zkey_array.  The command determines the actual behavior
 * of the set.  See the following example format of the zkey_array.
 *
 * Example Input array:
 * Array
 * (
 *     [foo] => Array
 *         (
 *             [value] => "value"
 *             [shardKey] => 123
 *             [flag] => 0
 *             [cas] => 0
 *             [expire] => 0
 *         )
 *
 *     [foo1] => Array
 *         (
 *             [value] => "value1"
 *             [shardKey] => 123
 *             [flag] => 0
 *             [cas] => 0
 *             [expire] => 0
 *         )
 *
 *     [foo2] => Array
 *         (
 *             [value] => "value2"
 *             [shardKey] => 123
 *             [flag] => 0
 *             [cas] => 0
 *             [expire] => 0
 *         )
 * )
 *
 * Example Result array:
 * Array
 * (
 *     [foo] => 1
 *     [foo1] => 1
 *     [foo2] => 0
 * )
 *
 * @param mmc_object The mmc object containing an instance of this memcache object
 * @param zkey_array The key to shardKey associative array map.
 * @param command The command to be executed on each key in the array.
 * @param command_len The length of the command
 * @param return_value The actual result of this call will be placed into this return value as an associative array.
 * @return array An associative array of key => status:
 *		status   - The status of true or false depending on whether or not the memcache transaction was successful.
 */
static void php_mmc_store_multi_by_key(zval *mmc_object, zval *zkey_array, char *command, int command_len, zval **return_value, zval **val_len TSRMLS_DC) {
	MMC_DEBUG(("php_mmc_store_multi_by_key: Entry"));

	HashTable *key_hash;
	int key_array_count = 0;
	mc_logger_t logData;
	char multi_cmd[MMC_KEY_MAX_SIZE];
	mmc_init_multi(TSRMLS_C);

	// Start with a clean return array
	array_init(*return_value);
	if (val_len) {
		array_init(*val_len);
	}
	zval *tmp;
	MAKE_STD_ZVAL(tmp);

	strncpy(multi_cmd, command, command_len);
	strncpy(multi_cmd + command_len, "MultiByKey", sizeof("MultiByKey"));

	key_hash = Z_ARRVAL_P(zkey_array);
	key_array_count = zend_hash_num_elements(key_hash);

	MMC_DEBUG(("php_mmc_store_multi_by_key: The key array passed contains %d elements ", key_array_count));

	// Go through all of the key => value pairs and send the request without waiting for the responses just yet for performance improvements
	for (zend_hash_internal_pointer_reset(key_hash); zend_hash_has_more_elements(key_hash) == SUCCESS; zend_hash_move_forward(key_hash)) {

		LogManager lm(&logData);
		char *input_key;
		unsigned int input_key_len;
		ulong idx;
		zval **data;

		if (zend_hash_get_current_key_ex(key_hash, &input_key, &input_key_len, &idx, 0, NULL) == HASH_KEY_IS_STRING) {
			LogManager::getLogger()->setKey(input_key);
			if (zend_hash_get_current_data(key_hash, (void**) &data) == FAILURE) {
				MMC_DEBUG(("php_mmc_store_multi_by_key: No data for key '%s'", input_key));
				// Should never actually fail since the key is known to exist.
				add_assoc_bool(*return_value, input_key, 0);
				LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
				continue;
			}

			// Verify that data contains an array
			if (Z_TYPE_PP(data) == IS_ARRAY) {
				MMC_DEBUG(("php_mmc_store_multi_by_key: Sending command for key '%s'", input_key));

				HashTable *value_hash;
				value_hash = Z_ARRVAL_PP(data);
				char shard_key[MMC_KEY_MAX_SIZE];
				unsigned int shard_key_len;
				int flag = 0;
				int expire = 0;
				unsigned long cas = 0;
				zval **zvalue;
				zval **zshardKey;
				zval **zflag;
				zval **zexpire;
				zval **zcas;

				if(zend_hash_find(value_hash, "value", sizeof("value"), (void **)&zvalue) == FAILURE) {
					MMC_DEBUG(("php_mmc_store_multi_by_key: value not found"));
				} else {
					if(Z_TYPE_PP(zvalue) == IS_STRING) {
						MMC_DEBUG(("php_mmc_store_multi_by_key: value found '%s'", Z_STRVAL_PP(zvalue)));
					} else {
						MMC_DEBUG(("php_mmc_store_multi_by_key: value found"));
					}
				}

				if(zend_hash_find(value_hash, "shardKey", sizeof("shardKey"), (void **)&zshardKey) == FAILURE) {
					MMC_DEBUG(("php_mmc_store_multi_by_key: shardKey not found"));
				} else {
					mmc_prepare_key(*zshardKey, shard_key, &shard_key_len TSRMLS_CC);
					MMC_DEBUG(("php_mmc_store_multi_by_key: shardKey found '%s'", shard_key));
				}

				if(zend_hash_find(value_hash, "flag", sizeof("flag"), (void **)&zflag) == FAILURE) {
					MMC_DEBUG(("php_mmc_store_multi_by_key: flag not found"));
				} else {
					flag = Z_LVAL_PP(zflag);
					LogManager::getLogger()->setFlags(flag);
					MMC_DEBUG(("php_mmc_store_multi_by_key: flag found '%d'", flag));
				}

				if(zend_hash_find(value_hash, "expire", sizeof("expire"), (void **)&zexpire) == FAILURE) {
					MMC_DEBUG(("php_mmc_store_multi_by_key: expire not found"));
				} else {
					expire = Z_LVAL_PP(zexpire);
					LogManager::getLogger()->setExpiry(expire);
					MMC_DEBUG(("php_mmc_store_multi_by_key: expire found '%d'", expire));
				}

				if(zend_hash_find(value_hash, "cas", sizeof("cas"), (void **)&zcas) == FAILURE) {
					MMC_DEBUG(("php_mmc_store_multi_by_key: cas not found"));
				} else {
					cas = Z_LVAL_PP(zcas);
					MMC_DEBUG(("php_mmc_store_multi_by_key: cas found '%lu'", cas));
				}

				if (php_mmc_store(mmc_object, input_key, input_key_len-1, *zvalue, flag, expire, cas, shard_key, shard_key_len, command, command_len, 1, tmp)) {
					MMC_DEBUG(("php_mmc_store_multi_by_key: Command succeeded for key '%s'", input_key));
					add_assoc_bool(*return_value, input_key, 1);
					if (val_len) {
						add_assoc_long(*val_len, input_key, Z_LVAL_P(tmp));
					}
				} else {
					MMC_DEBUG(("php_mmc_store_multi_by_key: Command failed for key '%s'", input_key));
					add_assoc_bool(*return_value, input_key, 0);
				}
			} else {
				add_assoc_bool(*return_value, input_key, 0);
				php_error_docref(NULL TSRMLS_CC, E_ERROR, "value passed in for key '%s' wasn't an array type", input_key);
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Key passed in wasn't a string type");
		}
		LogManager::getLogger()->setCmd(multi_cmd);
	}

	mmc_free_multi(TSRMLS_C);
	FREE_ZVAL(tmp);
	MMC_DEBUG(("php_mmc_store_multi_by_key: Exit"));
}

/**
 * Returns a value of an existing item using the key.  The shardKey is used to determine which server in the pool is
 * used for the operation.  If there is any communication errors or failures then false is returned.  If command is
 * successful then true is returned.
 *
 * @param zkey The key to retrieve
 * @param zshardKey The key is used for determining the server to use in the memcached pool
 * @param zvalue The item returned by the memcached get will be stored here if the return value is true.
 * @param flags Flags to identify compression or other memcached specific operation flags
 * @param cas The compare and swap token
 * @return zend_bool true if operation was successful and false otherwise.
 */
PHP_FUNCTION(memcache_getByKey)
{
	mmc_pool_t *pool;
	zval *zkey;
	zval *zshardKey;
	zval *zvalue;
	zval *mmc_object = getThis();
	zval *flags = NULL;
	zval *cas = NULL;
	mc_logger_t logData;
	LogManager lm(&logData);

	char key[MMC_KEY_MAX_SIZE];

	LogManager::getLogger()->setCmd("getBykey");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ozzz|zz", &mmc_object, memcache_class_entry_ptr, &zkey, &zshardKey, &zvalue, &flags, &cas) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zzz|zz", &zkey, &zshardKey, &zvalue, &flags, &cas) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setCommandType(GET);
	LogManager::getLogger()->setLogName(pool->log_name);
/*
	zval *tmp;
	MAKE_STD_ZVAL(tmp);
	ZVAL_NULL(tmp);
*/
	int result = php_mmc_get_by_key(pool, zkey, key, zshardKey, zvalue, flags, cas TSRMLS_CC);
/*
	REPLACE_ZVAL_VALUE(&zvalue, tmp, 0);
	FREE_ZVAL(tmp);
*/
	if(result < 0) {
		zval_dtor(zvalue);
		LogManager::getLogger()->setCode(CMD_FAILED);
		RETURN_FALSE;
	} else {

		RETURN_TRUE;
	}

	return;
}

/**
 * A utility method for retrieving an existing item using the key.  The shardKey is used to determine which server in
 * the pool is used for the operation.  If there is any communication errors or failures then -1 is returned.  If
 * command is successful then >= 0 is returned.
 *
 * @param pool The memcached pool
 * @param zkey The key to retrieve
 * @param zshardKey The key is used for determining the server to use in the memcached pool
 * @param zvalue The item returned by the memcached get will be stored here if the return value is true.
 * @param flags Flags to identify compression or other memcached specific operation flags
 * @param cas The compare and swap token
 * @return int -1 on failures, otherwise >= 0.
 */
static int php_mmc_get_by_key(mmc_pool_t *pool, zval *zkey, char *key, zval *zshardKey, zval *zvalue, zval *return_flags,
	zval *return_cas TSRMLS_DC) {

	char shardKey[MMC_KEY_MAX_SIZE];
	unsigned int key_len;
	unsigned int shardKey_len;
	mmc_t *mmc;
	char *command;
	char *value;
	int result = -1;
	int command_len;
	int response_len;
	int value_len;
	int flags = 0;
	unsigned long cas;
	unsigned long *pcas = (return_cas != NULL) ? &cas : NULL;
	int chksum_error_retry_cnt = 0;

	if (return_flags != NULL) {
		array_init(return_flags);
	}

	if (return_cas != NULL) {
		array_init(return_cas);
	}

	if (mmc_prepare_key(zkey, key, &key_len TSRMLS_CC) == MMC_OK && mmc_prepare_key(zshardKey, shardKey, &shardKey_len TSRMLS_CC) == MMC_OK) {
		MMC_DEBUG(("php_mmc_get_by_key: getting key '%s' using shardKey '%s'", key, shardKey));

		LogManager::getLogger()->setKey(key);

		command_len = (pcas != NULL) ? spprintf(&command, 0, "gets %s", key) :
			spprintf(&command, 0, "get %s", key);
		ZVAL_NULL(zvalue);

		while (result < 0 && (mmc = mmc_pool_find(pool, shardKey, shardKey_len TSRMLS_CC)) != NULL &&
			mmc->status != MMC_STATUS_FAILED) {
			MMC_DEBUG(("php_mmc_get_by_key: found server '%s:%d' for key '%s' and shardKey '%s'", mmc->host, mmc->port, key, shardKey));

			value = NULL;
			/* send command and read value */
			// This loop is to retry just the sending and receiving part in case of checksum errors
			do {
				if (value)
					efree(value);
				if (mmc_sendcmd(mmc, command, command_len TSRMLS_CC) <= 0)
					break;
				result = mmc_read_value(mmc, NULL, NULL, &value, &value_len, &flags, pcas TSRMLS_CC);
				if (result != 0 || result == CHKSUM_MISMATCH_DETECTED) {
					if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 || !mmc_str_left(mmc->inbuf, "END", response_len, sizeof ("END") - 1)) {
						mmc_server_seterror(mmc, "Malformed END line", 0);
						LogManager::getLogger()->setCode(MC_MALFORMD);
						result = -1;
					}
				}
			}while(result == CHKSUM_MISMATCH_DETECTED && ++chksum_error_retry_cnt < MEMCACHE_G(integrity_error_retry_count));

			MMC_DEBUG(("php_mmc_get_by_key: Result: '%d'", result));

			if (result > 0) {
				if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
					result = mmc_postprocess_value(key, flags, mmc->host, &zvalue, value, value_len TSRMLS_CC);
				} else {
					ZVAL_STRINGL(zvalue, value, value_len, 0);
				}
			}
			else if (result == -2) {
				//clear out the buffer from a failure to uncompress in mmc_read_value
				if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 ||
						!mmc_str_left(mmc->inbuf, "END", response_len, sizeof ("END") - 1)) {
					LogManager::getLogger()->setCode(MC_MALFORMD);
					mmc_server_seterror(mmc, "Malformed END line", 0);
					result = -1;
				} else {
					LogManager::getLogger()->setCode(MC_ONLY_END);
					break;
				}

			} else if (result < 0) {
				if (result == CHKSUM_MISMATCH_DETECTED) {
					LogManager::getLogger()->setCode(DI_CHECKSUM_GET_FAILED_BYKEY);
					result = 0;
					efree(value);
				}
				else
					mmc_server_failure(mmc TSRMLS_CC);
			}
		}

		if (!mmc) {
			LogManager::getLogger()->setHost(PROXY_STR);
			LogManager::getLogger()->setCode(CONNECT_FAILED);
		} else {
			if (mmc->status == MMC_STATUS_FAILED) {
				LogManager::getLogger()->setCode(CONNECT_FAILED);
			}
			LogManager::getLogger()->setHost(mmc->host);
		}

		if (return_flags != NULL) {
			zval_dtor(return_flags);
			ZVAL_LONG(return_flags, flags);
		}

		if (return_cas != NULL) {
			zval_dtor(return_cas);
			ZVAL_LONG(return_cas, cas);
		}
		efree(command);

		return result;
	} else {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		MMC_DEBUG(("php_mmc_get_by_key: Unknown problem with the key or shardKey"));
		return -1;
	}
}

/**
 * Returns an associative array containing the status of each operation.
 *
 * Example input array key => value where key is the key to retrieve and value is the shardKey to use:
 * Array
 * (
 *     [foo] => 123
 *     [test] => 123
 *     [brent] => 567
 * )
 *
 * The shardKey is used to determine which memcache node is used in the cluster instead of the key itself.
 *
 * Example return for above input where key => array.
 * Array
 * (
 *     [foo] => Array
 *         (
 *             [shardKey] => 123
 *             [status] => 1
 *             [value] => bar
 *             [flag] => 0
 *             [cas] => 1
 *         )
 *
 *     [test] => Array
 *         (
 *             [shardKey] => 123
 *             [status] => 1
 *             [value] =>
 *         )
 *
 *     [brent] => Array
 *         (
 *             [shardKey] => 567
 *             [status] =>
 *         )
 *
 * )
 *
 * @param zkey_array The key to shardKey associative array map.  See above example for expected format.
 * @return array An associative array of key => array.  Where the array contains 5 possible fields:
 *		shardKey - The shard key passed in is echo'd back in response.
 *		status   - The status of true or false depending on whether or not the memcache transaction was successful.
 *		value    - The value returned by memcache or null if there was no record for the given key.
 *		flag     - The flag used for storing this key in memcache.
 *		cas		 - The compare and swap token for this key if any.
 */
PHP_FUNCTION(memcache_getMultiByKey) {
	mmc_pool_t *pool;
	zval *zkey_array;
	zval *mmc_object = getThis();
	mc_logger_t logData;
	LogManager lm(&logData);

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz", &mmc_object, memcache_class_entry_ptr, &zkey_array) == FAILURE) {
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &zkey_array) == FAILURE) {
			return;
		}
	}

	//Validate inputs
	if (zkey_array != NULL) {
		if (Z_TYPE_P(zkey_array) == IS_ARRAY) {
			if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
				RETURN_FALSE;
			}

			php_mmc_get_multi_by_key(pool, zkey_array, &return_value TSRMLS_CC);
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was not of expected array type");
			RETURN_FALSE;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was null");
		RETURN_FALSE;
	}

	return;
}


static void update_multibykey_result_array(zval **result_array, char *line, int res_code) {
	if (result_array == NULL) {
		return;
	}
	token_t tokens[MAX_TOKENS];
	int i;
	int ntokens = tokenize_command(line, (token_t *)&tokens, MAX_TOKENS);

	for (i = 1; i < ntokens; i++) {
		zval **value_array;
		if (tokens[i].value && zend_hash_find(Z_ARRVAL_P(*result_array), tokens[i].value, tokens[i].length+1, (void **)&(value_array)) != FAILURE) {
			add_assoc_bool(*value_array, "status", 0);
			LogManager::getLogger(tokens[i].value)->setCode(res_code);
		}
	}
}


/**
 * See PHP_FUNCTION(memcache_getMultiByKey)
 *
 * @param pool The memcached pool
 * @param zkey_array The key to shardKey associative array map.  See above example for expected format.
 * @param return_value The actual result of this call will be placed into this return value as an associative array.
 *			See above example in PHP_FUNCTION(memcache_getMultiByKey)
 * @return array An associative array of key => array.  Where the array contains 5 possible fields:
 *		shardKey - The shard key passed in is echo'd back in response.
 *		status   - The status of true or false depending on whether or not the memcache transaction was successful.
 *		value    - The value returned by memcache or null if there was no record for the given key.
 *		flag     - The flag used for storing this key in memcache.
 *		cas		 - The compare and swap token for this key if any.
 */
static void php_mmc_get_multi_by_key(mmc_pool_t *pool, zval *zkey_array, zval **return_value TSRMLS_DC) {
	MMC_DEBUG(("php_mmc_get_multi_by_key: Entry"));

	HashTable *key_hash;
	int i = 0, j;
	char *command_line[pool->num_servers];
	int flags;
	unsigned long cas;
	int result;
	char *value, *result_key;
	int value_len, result_key_len;
	mmc_t *mmc;
	zval **test_value;
	int result_status = 0, num_requests = 0;
	zval *chksum_error_retry_hash;
	int retry_count = 0;
	bool chksum_failed = false, do_failover = false;
	LogManager::setMultiLogger();

	mmc_queue_t serialized = {0};		/* mmc_queue_t<zval *>, pointers to zvals which need unserializing */
	mmc_queue_t serialized_key = {0};	/* pointers to corresponding keys */
	mmc_queue_t serialized_flags = {0};	/* pointers to corresponding flags */
	mmc_queue_t serialized_key_mmc = {0};   /* pointers to corresponding mmc */

	// Start with a clean return array
	array_init(*return_value);
	MAKE_STD_ZVAL(chksum_error_retry_hash);
	array_init(chksum_error_retry_hash);

	key_hash = Z_ARRVAL_P(zkey_array);

	mmc_init_multi(TSRMLS_C);

	do
	{
		result_status = 0, num_requests = 0;
		// Go through all of the key => value pairs and send the request to each server without waiting for response
		for (zend_hash_internal_pointer_reset(key_hash); zend_hash_has_more_elements(key_hash) == SUCCESS; zend_hash_move_forward(key_hash)) {

			char *input_key;
			unsigned int input_key_len;
			ulong idx;
			zval **zshardkey;
			zval *zkey;
			zval **value_array;
			char key[MMC_KEY_MAX_SIZE];
			char shardkey[MMC_KEY_MAX_SIZE];
			unsigned int key_len, shardkey_len;

			if (zend_hash_get_current_key_ex(key_hash, &input_key, &input_key_len, &idx, 0, NULL) != HASH_KEY_IS_STRING) {

				php_error_docref(NULL TSRMLS_CC, E_ERROR, "Key passed in wasn't a string type");
				continue;
			}


			if (mmc_prepare_key_ex(input_key, strlen(input_key), key, &key_len TSRMLS_CC) != MMC_OK) {
				MMC_DEBUG(("Problem with the key"));
				continue;
			}

			LogManager::createLogger(key);
			LogManager::setLogger(key);
			LogManager::getLogger()->setCmd("getMultiByKey");
			LogManager::getLogger()->setLogName(pool->log_name);

			// Add key if it is not already there
			if (!zend_hash_exists(Z_ARRVAL_PP(return_value), key, key_len+1)) {
				zval *add_value_array;
				ALLOC_INIT_ZVAL(add_value_array);
				array_init(add_value_array);
				zend_hash_add(Z_ARRVAL_PP(return_value), key, key_len + 1 , &add_value_array, sizeof(add_value_array), NULL);
				value_array = &add_value_array;
				add_assoc_bool(*value_array, "status", 1);
				add_assoc_null(*value_array, "value");
				LogManager::getLogger()->setCode(MC_SUCCESS);
			}
			else {
				if (zend_hash_find(Z_ARRVAL_PP(return_value), key, key_len + 1, (void **)&(value_array)) == FAILURE) {
					continue;
				}
			}

			if (zend_hash_get_current_data(key_hash, (void**) &zshardkey) == FAILURE) {
				MMC_DEBUG(("php_mmc_get_multi_by_key: No data for key '%s'", key));
				// Should never actually fail since the key is known to exist.
				add_assoc_bool(*value_array, "status", 0);
				continue;
			}

			if (mmc_prepare_key(*zshardkey, shardkey, &shardkey_len TSRMLS_CC) != MMC_OK) {
				MMC_DEBUG(("Problem with the shardkey"));
				add_assoc_bool(*value_array, "status", 0);
				continue;
			}

			if (!i || !zend_hash_exists(Z_ARRVAL_PP(value_array), "shardKey", sizeof("shardKey"))) {
				ZVAL_ADDREF(*zshardkey);
				add_assoc_zval(*value_array, "shardKey", *zshardkey);
			}

			/* schedule key if
			   - first round OR
			   - if missing from result OR
			   - If we have a checksum mismatch error for it (and we have not exceeded the checksum error retries)
			 */
			if ( !i || (do_failover && zend_hash_find(Z_ARRVAL_PP(value_array), "value", sizeof("value"),
							(void **)&test_value) == SUCCESS && ZVAL_IS_NULL(*test_value)) ||
					(chksum_failed && zend_hash_exists(Z_ARRVAL_P(chksum_error_retry_hash), key, key_len + 1)) ) {
				if ((mmc = mmc_pool_find(pool, shardkey, shardkey_len TSRMLS_CC)) != NULL &&
					mmc->status != MMC_STATUS_FAILED) {
					LogManager::getLogger()->setHost(mmc->host);
					if (!(mmc->outbuf.len)) {
						append_php_smart_string(&(mmc->outbuf), "gets", sizeof("gets")-1);
						pool->requests[num_requests++] = mmc;
					}

					append_php_smart_string(&(mmc->outbuf), " ", 1);
					append_php_smart_string(&(mmc->outbuf), key, key_len);
					MMC_DEBUG(("mmc_exec_retrieval_cmd_multi: scheduled key '%s' for '%s:%d' request length '%d'", key, mmc->host, mmc->port, mmc->outbuf.len));
				}
				else {
					/*key belongs to a failed server */
					add_assoc_bool(*value_array, "status", 0);
					if (mmc) {
						LogManager::getLogger()->setHost(mmc->host);
					} else {
						LogManager::getLogger()->setHost(PROXY_STR);
					}
					LogManager::getLogger()->setCode(CONNECT_FAILED);
				}
			}
		}

		/* second pass to send requests in parallel */
		for (j=0; j<num_requests; j++) {
			smart_str_0(&(pool->requests[j]->outbuf));

			command_line[j] = (char *)emalloc(pool->requests[j]->outbuf.len + 1);
			memcpy(command_line[j], pool->requests[j]->outbuf.c, pool->requests[j]->outbuf.len);
			command_line[j][pool->requests[j]->outbuf.len] = '\0';

			if ((result = mmc_sendcmd(pool->requests[j], pool->requests[j]->outbuf.c, pool->requests[j]->outbuf.len TSRMLS_CC)) < 0) {
				mmc_server_failure(pool->requests[j] TSRMLS_CC);
				result_status = result;
				update_multibykey_result_array(&(*return_value), command_line[j], WRITE_TO_SERVER_FAILED);
			}
		}

		if (zend_hash_num_elements(Z_ARRVAL_P(chksum_error_retry_hash)) > 0) {
			zend_hash_clean(Z_ARRVAL_P(chksum_error_retry_hash));
		}

		LogManager::createLogger("TMP_KEY");

		/* third pass to read responses */
		for (j=0; j<num_requests; j++) {
			if (pool->requests[j]->status != MMC_STATUS_FAILED) {
				do {
					int free_key = 1;
					zval **value_array;
					value = NULL;
					result_key = NULL;
					result_key_len = 0;
					LogManager::setLogger("TMP_KEY");

					result = mmc_read_value(pool->requests[j], &result_key, &result_key_len, &value, &value_len, &flags, &cas TSRMLS_CC);
					if (result <= 0 && result != -2 && result != CHKSUM_MISMATCH_DETECTED) {
						break;
					}

					if (zend_hash_find(Z_ARRVAL_PP(return_value), result_key, result_key_len + 1, (void **)&(value_array)) == FAILURE) {
						if (result_key != NULL)
							efree(result_key);
						continue;
					}

					if (result == -2) {
						/* uncompression failed */
						LogManager::getLogger(result_key)->setCode(UNCOMPRESS_FAILED);
						add_assoc_bool(*value_array, "status", 0);
					}
					else if (result == CHKSUM_MISMATCH_DETECTED) {
						LogManager::getLogger(result_key)->setCode(DI_CHECKSUM_GET_FAILED_GET);
						add_assoc_null_ex(chksum_error_retry_hash, result_key, result_key_len + 1);
						add_assoc_bool(*value_array, "status", 0);
						efree(value);
					}
					else {
						if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
							/* don't store duplicate values */
							if ((zend_hash_find(Z_ARRVAL_PP(value_array), "value", sizeof("value"),
											(void **)&test_value)) == SUCCESS && ZVAL_IS_NULL(*test_value)) {
								zval *zvalue;
								MAKE_STD_ZVAL(zvalue);
								ZVAL_STRINGL(zvalue, value, value_len, 0);
								add_assoc_zval(*value_array, "value", zvalue);
								mmc_queue_push(&serialized, zvalue);
								mmc_queue_push(&serialized_key, result_key);
								mmc_queue_push(&serialized_flags, (void*)(unsigned long)flags);
								mmc_queue_push(&serialized_key_mmc, (void*)(pool->requests[j]));
								free_key = 0;
							}
						}
						else {
							zval *zvalue;
							MAKE_STD_ZVAL(zvalue);
							ZVAL_STRINGL(zvalue, value, value_len, 0);
							add_assoc_zval(*value_array, "value", zvalue);
						}

						LogManager::setLogger(result_key);
						LogManager::getLogger()->setResLen(value_len);
						LogManager::getLogger()->setFlags(flags);
						LogManager::getLogger()->setCas(cas);

						add_assoc_bool(*value_array, "status", 1);
						add_assoc_long_ex(*value_array, "flag", sizeof("flag"), flags);
						add_assoc_long_ex(*value_array, "cas", sizeof("cas"), cas);
					}
					if (free_key) {
						efree(result_key);
					}
				}while(1);

				/* check for server failure */
				if (result < 0 && result != CHKSUM_MISMATCH_DETECTED) {
					mmc_server_failure(pool->requests[j] TSRMLS_CC);
					result_status = result;
					update_multibykey_result_array(&(*return_value), command_line[j], LogManager::getLogger()->getCode());
				}
			}

			smart_str_free(&(pool->requests[j]->outbuf));
			efree(command_line[j]);
		}

		chksum_failed = zend_hash_num_elements(Z_ARRVAL_P(chksum_error_retry_hash)) > 0 &&
							++retry_count < MEMCACHE_G(integrity_error_retry_count);
		do_failover = result_status < 0 && MEMCACHE_G(allow_failover) && i <= MEMCACHE_G(max_failover_attempts);
		i++;

	} while (do_failover || chksum_failed);

	zval_ptr_dtor(&chksum_error_retry_hash);

	/* post-process serialized values */
	if (serialized.len) {
		zval *value;
		char *key;
		int flag;

		while ((value = (zval *)mmc_queue_pop(&serialized)) != NULL) {
			key = (char *)mmc_queue_pop(&serialized_key);
			LogManager::setLogger(key);
 			flag = (int) (unsigned long) (void*) mmc_queue_pop(&serialized_flags);
			mmc_t *popped_mmc = (mmc_t *)(mmc_queue_pop(&serialized_key_mmc));
			const char *hostname = (popped_mmc == NULL) ? "UNKNOWN" : popped_mmc->host;
			if (popped_mmc == NULL) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "MMC NULL for (serialized) key %s", key);
			}
			if (result = mmc_postprocess_value(key, flag, hostname, &value, Z_STRVAL_P(value), Z_STRLEN_P(value) TSRMLS_CC) == 0) {
				zval **value_array;
				/* unserialize failed */
				if (zend_hash_find(Z_ARRVAL_PP(return_value), key, strlen(key)+1, (void **)&(value_array)) == SUCCESS) {
					add_assoc_bool(*value_array, "status", 0);
					LogManager::getLogger()->setCode(UNSERIALIZE_FAILED);
				}
			}
			efree(key);
		}

		mmc_queue_free(&serialized);
		mmc_queue_free(&serialized_key);
		mmc_queue_free(&serialized_flags);
		mmc_queue_free(&serialized_key_mmc);
	}

	mmc_free_multi(TSRMLS_C);

	MMC_DEBUG(("php_mmc_get_multi_by_key: Exit"));
}

/* {{{ proto mixed memcache_get( object memcache, mixed key, [ mixed flag ])
   Returns the item with a lock on the object for the specified timeout period */
PHP_FUNCTION(memcache_getl)
{
	mmc_pool_t *pool;
	zval *zkey, *mmc_object = getThis(), *flags = NULL, *cas = NULL, *zval_metadata = NULL;
	int timeout = 15;
	char metadata[MAX_METADATA_LEN + 1];
	int metadata_len = -1;		// We set this to -1 so as to be able to return even 0 length metadata.
								// In case of lock error, mmc_exec_getl_cmd will set the metadata_len to something >= 0
								// (=0 if there is a lock error but the lock has no metadata)

	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("getl");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|lzz",
			&mmc_object, memcache_class_entry_ptr, &zkey, &timeout, &flags, &zval_metadata) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|lzz", &zkey, &timeout, &flags, &zval_metadata) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);
	LogManager::getLogger()->setCommandType(GET);

	if (zval_metadata != NULL) {
		if (Z_TYPE_P(zval_metadata) == IS_STRING) {
			metadata_len = Z_STRLEN_P(zval_metadata);
			if (metadata_len > MAX_METADATA_LEN) {
				metadata_len = MAX_METADATA_LEN;
			}
			memcpy(metadata, Z_STRVAL_P(zval_metadata), metadata_len);
			metadata[metadata_len] = 0;
		}
		else {
			RETURN_FALSE;
		}
	}

	php_mmc_getl(pool, zkey, &return_value, flags, cas, timeout, metadata, metadata_len);
	if (metadata_len >= 0 && zval_metadata != NULL) {
		zval_dtor(zval_metadata);
		ZVAL_STRINGL(zval_metadata, metadata, metadata_len, 1);
	}
}

/* {{{ proto mixed memcache_getl_Multi_by_key( object memcache, mixed key, [ mixed flag ])
   Returns the item with a lock on the object for the specified timeout period */
PHP_FUNCTION(memcache_getlMultiByKey)
{
	mmc_pool_t *pool;
	zval *zkey, *mmc_object = getThis(), *flags = NULL, *cas = NULL, *zval_metadata = NULL;
	zval *zvalue = NULL;
	char metadata[MAX_METADATA_LEN + 1];
	int metadata_len = -1;		// We set this to -1 so as to be able to return even 0 length metadata.
	// In case of lock error, mmc_exec_getl_cmd will set the metadata_len to something >= 0
	// (=0 if there is a lock error but the lock has no metadata)

	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("getlMultiByKey");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ozz|zz",
					&mmc_object, memcache_class_entry_ptr, &zkey, &zvalue, &flags, &zval_metadata) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zz|zz", &zkey, &zvalue, &flags,
					&zval_metadata) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	if (zkey != NULL) {
		if (Z_TYPE_P(zkey) == IS_ARRAY) {
			if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
				LogManager::getLogger()->setCode(POOL_NT_FOUND);
				RETURN_FALSE;
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was not of expected array type");
			RETURN_FALSE;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was null");
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);
	LogManager::getLogger()->setCommandType(GET);

	php_mmc_getl_multi_by_key(pool, zkey, &zvalue, &return_value, flags, cas, zval_metadata);
}

/* {{{ proto string memcache_findserver(object memcache, mixed key)
   Computes the hash of the key and finds the exact server in the pool to which this key will be mapped. Returns that host as a string.
*/
PHP_FUNCTION(memcache_findserver)
{
	mmc_pool_t *pool;
	zval *zkey, *mmc_object = getThis();

	char key[MMC_KEY_MAX_SIZE];
	unsigned int key_len;

    mmc_t *mmc;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("findserver");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz",
			&mmc_object, memcache_class_entry_ptr, &zkey) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &zkey) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (Z_TYPE_P(zkey) != IS_ARRAY) {
		if (mmc_prepare_key(zkey, key, &key_len TSRMLS_CC) == MMC_OK) {
			if ((mmc = mmc_pool_find(pool, key, key_len TSRMLS_CC)) == NULL ||
				mmc->status == MMC_STATUS_FAILED) {
				zval_dtor(return_value);
				ZVAL_FALSE(return_value);
				if (mmc) {
					LogManager::getLogger()->setHost(mmc->host);
				} else {
					LogManager::getLogger()->setHost(PROXY_STR);
				}
				LogManager::getLogger()->setCode(CONNECT_FAILED);
			} else { // we found the server
				LogManager::getLogger()->setHost(mmc->host);
                RETVAL_STRING(mmc->host, 1);
				LogManager::getLogger()->setCode(MC_SUCCESS);
            }
		}
		else {
			LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
			ZVAL_FALSE(return_value);
		}
	}  else {
		LogManager::getLogger()->setCode(INVALID_PARAM);
		ZVAL_FALSE(return_value);
	}
}
/* }}}*/

/* {{{ proto mixed memcache_unlock( object memcache, mixed key, [ mixed cas ] )
   Returns true if the item was unlocked successfully, with the given cas
   value or the one remembered in the internal cas array, else returns false */
PHP_FUNCTION(memcache_unlock)
{
	zval *mmc_object = getThis(), *zkey = NULL;
	unsigned long cas = 0;
	mmc_pool_t *pool;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("unlock");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|l",
			&mmc_object, memcache_class_entry_ptr, &zkey, &cas) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|l", &zkey, &cas) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	php_mmc_unlock(pool, zkey, cas, INTERNAL_FUNCTION_PARAM_PASSTHRU);

	LogManager::getLogger()->setLogName(pool->log_name);

}
/* }}}*/


/* {{{ proto mixed memcache_unlockMultiByKey( object memcache, mixed key)
   Returns true if the item was unlocked successfully, with the given cas
   value or the one remembered in the internal cas array, else returns false */
PHP_FUNCTION(memcache_unlockMultiByKey)
{
	zval *mmc_object = getThis(), *zkey = NULL;
	mmc_pool_t *pool;
	mc_logger_t logData;
	LogManager lm(&logData);
	LogManager::getLogger()->setCmd("unlockMultiByKey");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz",
			&mmc_object, memcache_class_entry_ptr, &zkey) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &zkey) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (zkey != NULL) {
		if (Z_TYPE_P(zkey) == IS_ARRAY) {
			if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
				LogManager::getLogger()->setCode(POOL_NT_FOUND);
				RETURN_FALSE;
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was not of expected array type");
			RETURN_FALSE;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was null");
		RETURN_FALSE;
	}

	php_mmc_unlock_multi_by_key(pool, zkey, &return_value, INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}}*/


/* {{{ proto mixed memcache_get( object memcache, mixed key [, mixed &flags [, mixed &cas ] [, bool return_corrupted] ] )
   Returns value of existing item or false */
PHP_FUNCTION(memcache_get)
{
	mmc_pool_t *pool;
	zval *zkey, *mmc_object = getThis(), *flags = NULL, *cas = NULL;
	mc_logger_t logData;
	LogManager lm(&logData);

	zval **data;
	bool return_corrupted = false;
	char key[MMC_KEY_MAX_SIZE];

	LogManager::getLogger()->setCmd("get");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|zzb", &mmc_object, memcache_class_entry_ptr, &zkey, &flags, &cas, &return_corrupted) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|zzb", &zkey, &flags, &cas, &return_corrupted) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	php_mmc_get(pool, zkey, key, &return_value, NULL, flags, cas, return_corrupted);
}

static void php_mmc_getl(mmc_pool_t *pool, zval *zkey, zval **return_value, zval *flags, zval *cas, int timeout, char *metadata, int &metadata_len) /* {{{ */
{
	char key[MMC_KEY_MAX_SIZE];
	unsigned int key_len;

	if (Z_TYPE_P(zkey) != IS_ARRAY) {
		if (mmc_prepare_key(zkey, key, &key_len TSRMLS_CC) == MMC_OK) {
			if (mmc_exec_getl_cmd(pool, key, key_len, return_value, flags, cas, timeout , metadata, metadata_len TSRMLS_CC) < 0) {
				zval_dtor(*return_value);
				ZVAL_FALSE(*return_value);
			}
		}
		else {
			LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
			ZVAL_FALSE(*return_value);
		}
	}  else {
		LogManager::getLogger()->setCode(INVALID_PARAM);
		ZVAL_FALSE(*return_value);
	}
}


void php_mmc_getl_multi_by_key(mmc_pool_t *pool, zval *zkeys, zval **return_value, zval **status_array,
		zval *return_flags, zval *return_cas, zval *return_metadata) /* {{{ */
{
	mmc_t *mmc;
	HashTable *key_hash;
	zval **zkey;
	char *result_key, *value;
	char *key;
	unsigned int key_len;
	unsigned long cas;
	smart_str command_line[pool->num_servers];
	int done = 0;
	zval *chksum_error_retry_hash;
	int retry_count = 0;
	bool chksum_failed = false;
	zval **data;
	char shard_key[MMC_KEY_MAX_SIZE];
	char lock_meta_data[MAX_METADATA_LEN + 1];
	unsigned int shard_key_len, timeout_len, lock_meta_data_len;
	LogManager::setMultiLogger();

	int	i = 0, j, num_requests, result, result_status, result_key_len, value_len, flags;
	mmc_queue_t serialized = {0};		/* mmc_queue_t<zval *>, pointers to zvals which need unserializing */
	mmc_queue_t serialized_key = {0};	/* pointers to corresponding keys */
	mmc_queue_t serialized_flags = {0};	/* pointers to corresponding flags */
	mmc_queue_t serialized_key_mmc = {0};	/* pointers to corresponding mmc */

	array_init(*return_value);

	if (status_array != NULL) {
		array_init(*status_array);
	}

	if (return_flags != NULL) {
		zval_dtor(return_flags);
		array_init(return_flags);
	}

	if (return_cas != NULL) {
		zval_dtor(return_cas);
		array_init(return_cas);
	}

	if (return_metadata != NULL) {
		zval_dtor(return_metadata);
		array_init(return_metadata);
	}

	MAKE_STD_ZVAL(chksum_error_retry_hash);
	array_init(chksum_error_retry_hash);

	key_hash = Z_ARRVAL_P(zkeys);

	mmc_init_multi(TSRMLS_C);

	do {
		result_status = 0; num_requests = 0;

		for (zend_hash_internal_pointer_reset(key_hash);zend_hash_has_more_elements(key_hash) == SUCCESS;
				zend_hash_move_forward(key_hash)) {
			ulong idx;
			char *timeout = "15";
			timeout_len = sizeof("15")-1;
			lock_meta_data_len = 0;
			if (zend_hash_get_current_key_ex(key_hash, &key, &key_len,
						&idx, 0, NULL) == HASH_KEY_IS_STRING) {
				bool free_timeout = false;
				zval **zvalue;
				LogManager::createLogger(key);
				LogManager::setLogger(key);
				LogManager::getLogger()->setCmd("getlMultiByKey");
				LogManager::getLogger()->setLogName(pool->log_name);

				if (zend_hash_get_current_data(key_hash, (void **)&data) == FAILURE) {
					add_assoc_bool(*return_value, key, 0);
					LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
					continue;
				}

				if (Z_TYPE_PP(data) == IS_ARRAY) {
					HashTable *value_hash;
					value_hash = Z_ARRVAL_PP(data);
					if (zend_hash_find(value_hash, "shardKey", sizeof("shardKey"),
								(void **)&zvalue) == FAILURE) {
						LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
						continue;
					} else {
						mmc_prepare_key(*zvalue, shard_key, &shard_key_len TSRMLS_CC);
					}

					if (zend_hash_find(value_hash, "lockMetaData", sizeof("lockMetaData"),
								(void **)&zvalue) == SUCCESS) {
						mmc_prepare_key(*zvalue, lock_meta_data, &lock_meta_data_len TSRMLS_CC);
					}

					if (zend_hash_find(value_hash, "timeout", sizeof("timeout"),
								(void **)&zvalue) == SUCCESS) {
						if (Z_TYPE_PP(zvalue) == IS_LONG) {
							timeout = new char[MAX_LENGTH_OF_LONG];
							timeout_len = sprintf(timeout, "%d", Z_LVAL_PP(zvalue));
							free_timeout = true;
						} else if (Z_TYPE_PP(zvalue) == IS_STRING) {
							timeout = Z_STRVAL_PP(zvalue);
							timeout_len = Z_STRLEN_PP(zvalue);
						}
					}
				} else if (!(mmc_prepare_key(*data, shard_key, &shard_key_len TSRMLS_CC)
						== MMC_OK)) {
					add_assoc_bool(*return_value, key, 0);
					LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
					continue;
				}

				if (!i || chksum_failed && zend_hash_exists(Z_ARRVAL_P(chksum_error_retry_hash),
							key, key_len +1)) {
					if (chksum_failed) {
						mmc_exec_unl_cmd_by_key(pool, mmc, key, key_len+1, shard_key,
								shard_key_len+1);
					}
					if ((mmc = mmc_pool_find(pool, shard_key, shard_key_len TSRMLS_CC)) != NULL
							&& mmc->status != MMC_STATUS_FAILED) {
						if (mmc->outbuf.len) {
							append_php_smart_string(&(mmc->outbuf), TERM, sizeof(TERM)-1);
							//To fix mcmux
							if (mmc->proxy && mmc->proxy_str_len) {
								append_php_smart_string(&(mmc->outbuf), mmc->proxy_str, mmc->proxy_str_len);
							}
						} else {
							mmc->index = num_requests;
							memset(&command_line[mmc->index], 0 , sizeof(smart_str));
							append_php_smart_string(&(command_line[mmc->index]), "getl", sizeof("getl")-1);
							pool->requests[num_requests++] = mmc;
						}
						append_php_smart_string(&(mmc->outbuf), "getl", sizeof("getl")-1);
						LogManager::getLogger()->setHost(mmc->host);
						append_php_smart_string(&(mmc->outbuf), " ", 1);
						append_php_smart_string(&(mmc->outbuf), key, key_len-1);
						append_php_smart_string(&(mmc->outbuf), " ", 1);
						append_php_smart_string(&(mmc->outbuf), timeout, timeout_len);
						if (lock_meta_data_len) {
							append_php_smart_string(&(mmc->outbuf), " ", 1);
							append_php_smart_string(&(mmc->outbuf), lock_meta_data, lock_meta_data_len);
						}
						if (status_array && !done) {
							add_assoc_bool_ex(*status_array, key, key_len, 1);
						}

						append_php_smart_string(&(command_line[mmc->index]), " ", 1);
						append_php_smart_string(&(command_line[mmc->index]), key, key_len-1);
						LogManager::getLogger()->setCode(MC_SUCCESS);
					}
					else {
						if (status_array) {
							add_assoc_bool_ex(*status_array, key, key_len, 0);
						}
						LogManager::getLogger()->setCode(CONNECT_FAILED);
						if (mmc) {
							LogManager::getLogger()->setHost(mmc->host);
						} else {
							LogManager::getLogger()->setHost(PROXY_STR);
						}
					}
				}
				if (free_timeout) {
					delete []timeout;
				}
			}
		}
		done = 1;

		for (j=0; j<num_requests; j++) {
			smart_str_0(&(command_line[j]));

			if ((result = mmc_sendcmd(pool->requests[j], pool->requests[j]->outbuf.c,
							pool->requests[j]->outbuf.len TSRMLS_CC)) < 0) {
				mmc_server_failure(pool->requests[j] TSRMLS_CC);
				result_status = result;
				if (status_array) {
					update_result_array(&(*status_array), command_line[j].c, WRITE_TO_SERVER_FAILED);
				}
				pool->requests[j]->index = 0;
			}
		}

		if (zend_hash_num_elements(Z_ARRVAL_P(chksum_error_retry_hash)) > 0) {
			zend_hash_clean(Z_ARRVAL_P(chksum_error_retry_hash));
		}

		for (j=0; j<num_requests; j++) {
			if (pool->requests[j]->status != MMC_STATUS_FAILED) {
				token_t tokens[MAX_TOKENS];
				int ntokens = tokenize_command(command_line[j].c, (token_t *)&tokens, MAX_TOKENS)-1;
				int tok_index = 1;
				mmc_t *mmc = pool->requests[j];

				do {
					value = NULL;
					result_key = NULL;
					value_len = flags = result_key_len = cas = 0;
					int free_key = 1, response_len = 0, cas_key_len = 0;
					char *cas_key = NULL;
					zval **zshard, **zvalue;
					//shard key buffer is used for storing the key for logger
					memcpy(shard_key, tokens[tok_index].value, tokens[tok_index].length+1);
					LogManager::setLogger(shard_key);

					result = mmc_read_value(mmc, &result_key, &result_key_len, &value,
							&value_len, &flags, &cas TSRMLS_CC);

					if (result == -2) {
						LogManager::getLogger(result_key)->setCode(UNCOMPRESS_FAILED);
						if (status_array) {
							add_assoc_bool_ex(*status_array, result_key, result_key_len + 1, 0);
						}
					} else if (result == CHKSUM_MISMATCH_DETECTED) {
						/*run unlock before doing it again*/
						LogManager::getLogger(result_key)->setCode(DI_CHECKSUM_GET_FAILED_GET);
						add_assoc_null_ex(chksum_error_retry_hash, result_key, result_key_len+1);
						add_assoc_bool_ex(*status_array, result_key, result_key_len+1, 0);
						efree(value);
					} else if (result < 0) {
						if (result_key) {
							free(result_key);
						}
						efree(value);
						tokens[tok_index].value[tokens[tok_index].length] = 0;
						add_assoc_bool_ex(*status_array, tokens[tok_index].value, tokens[tok_index].length+1, 0);

						if (mmc_str_left(mmc->inbuf, "LOCK_ERROR", strlen(mmc->inbuf),
									sizeof("LOCK_ERROR")-1)) {
							char *metadata_start = mmc->inbuf + (sizeof("LOCK_ERROR ") - 1);
							int metadata_len = strlen(mmc->inbuf) - (sizeof("LOCK_ERROR \r\n") - 1);
							LogManager::getLogger()->setCode(MC_LOCK_ERROR);
							if (metadata_len > 0 && return_metadata) {
								add_assoc_stringl_ex(return_metadata, tokens[tok_index].value, tokens[tok_index].length + 1,
										metadata_start, metadata_len, 1);
							}
						} else if (mmc_str_left(mmc->inbuf, "NOT_FOUND", strlen(mmc->inbuf),
									sizeof("NOT_FOUND")-1)) {
							LogManager::getLogger()->setCode(MC_NOT_FOUND);
							//set the status to true if key is not found
							add_assoc_bool_ex(*status_array, tokens[tok_index].value, tokens[tok_index].length+1, 1);
						} else {
							break;
						}
						tok_index++;
						result = 0;
						continue;
					} else if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0 ||
							!mmc_str_left(mmc->inbuf, "END", response_len, sizeof ("END") - 1)) {
						mmc_server_seterror(mmc, "Malformed END line", 0);
						LogManager::getLogger()->setCode(MC_MALFORMD);
						if (result_key) {
							free(result_key);
						}
						result = -1;
						break;
					} else if (flags & (MMC_SERIALIZED | MMC_SERIALIZED_IGBINARY)) {
						zval *result;
						MAKE_STD_ZVAL(result);
						ZVAL_STRINGL(result, value, value_len, 0);

						/* don't store duplicate values */
						if (zend_hash_add(Z_ARRVAL_PP(return_value), result_key, result_key_len + 1, &result, sizeof(result), NULL) == SUCCESS) {
							mmc_queue_push(&serialized, result);
							mmc_queue_push(&serialized_key, result_key);
							mmc_queue_push(&serialized_flags, (void*)(unsigned long)flags);
							mmc_queue_push(&serialized_key_mmc, (void*)(pool->requests[j]));
							free_key = 0;
						} else {
							zval_ptr_dtor(&result);
						}
					} else {
						add_assoc_stringl_ex(*return_value, result_key, result_key_len + 1, value,
								value_len, 0);
					}

					LogManager::getLogger(result_key)->setResLen(value_len);
					LogManager::getLogger(result_key)->setFlags(flags);
					LogManager::getLogger(result_key)->setCas(cas);

					zend_hash_find(Z_ARRVAL_P(zkeys), tokens[tok_index].value, tokens[tok_index].length+1,
							(void **)&zvalue);
					if (Z_TYPE_PP(data) == IS_ARRAY) {
						zend_hash_find(Z_ARRVAL_PP(zvalue), "shardKey", sizeof("shardKey"), (void **)&zshard);
					} else {
						zshard = zvalue;
					}
					mmc_prepare_key(*zshard, shard_key, &shard_key_len TSRMLS_CC);
					cas_key_len = tokens[tok_index].length+shard_key_len+1;
					cas_key = new char[cas_key_len];
					memcpy(cas_key, tokens[tok_index].value, tokens[tok_index].length);
					memcpy(cas_key+tokens[tok_index].length, shard_key, shard_key_len+1);
					add_assoc_long_ex(pool->cas_array, cas_key, cas_key_len, cas);

					if (return_flags != NULL) {
						add_assoc_long_ex(return_flags, result_key, result_key_len + 1, flags);
					}

					if (return_cas != NULL) {
						add_assoc_long_ex(return_cas, result_key, result_key_len + 1, cas);
					}

					if (free_key) {
						efree(result_key);
					}
					tok_index++;
				} while (tok_index < ntokens);

				/* check for server failure */
				if (result < 0) {
					mmc_server_failure(pool->requests[j] TSRMLS_CC);
					result_status = result;
					if (status_array) {
						for (i = 1; i < ntokens; i = i+1) {
							if (tokens[i].value) {
								add_assoc_bool_ex(*status_array, tokens[i].value, tokens[i].length + 1, 0);
								LogManager::getLogger(tokens[i].value)->setCode(READ_FROM_SERVER_FAILED);
							}
						}
					}
				}
			}

			smart_str_free(&(pool->requests[j]->outbuf));
			smart_str_free(&(command_line[j]));
		}

		chksum_failed = zend_hash_num_elements(Z_ARRVAL_P(chksum_error_retry_hash)) > 0 &&
			++retry_count < MEMCACHE_G(integrity_error_retry_count);
		i++;
	} while (chksum_failed);

	zval_ptr_dtor(&chksum_error_retry_hash);

	/* post-process serialized values */
	if (serialized.len) {
		zval *value;
		char *key;
		int flag;

		while ((value = (zval *)mmc_queue_pop(&serialized)) != NULL) {
			key = (char *)mmc_queue_pop(&serialized_key);
			LogManager::setLogger(key);
			flag = (int) (unsigned long) (void*) mmc_queue_pop(&serialized_flags);
			mmc_t *popped_mmc = (mmc_t *)(mmc_queue_pop(&serialized_key_mmc));
			const char *hostname = (popped_mmc == NULL) ? "UNKNOWN" : popped_mmc->host;
			if (popped_mmc == NULL) {
				php_error_docref(NULL TSRMLS_CC, E_WARNING, "MMC NULL for (serialized) key %s", key);
			}
			if (result = mmc_postprocess_value(key, flag, hostname, &value, Z_STRVAL_P(value), Z_STRLEN_P(value) TSRMLS_CC) == 0) {
				/* unserialize failed */
				if (status_array) {
					add_assoc_bool_ex(*status_array, key, strlen(key) + 1, 0);
				}
				LogManager::getLogger()->setCode(UNSERIALIZE_FAILED);
			}
			efree(key);
		}

		mmc_queue_free(&serialized);
		mmc_queue_free(&serialized_key);
		mmc_queue_free(&serialized_flags);
		mmc_queue_free(&serialized_key_mmc);
	}

	mmc_free_multi(TSRMLS_C);
}

static void php_mmc_unlock(mmc_pool_t *pool, zval *zkey, unsigned long cas, INTERNAL_FUNCTION_PARAMETERS) {
	char key[MMC_KEY_MAX_SIZE];
	unsigned int key_len;
	unsigned long **cas_lookup;
	int ret = -1;
	int cas_delete = 0;

	if (Z_TYPE_P(zkey) != IS_ARRAY) {
		if (mmc_prepare_key(zkey, key, &key_len TSRMLS_CC) == MMC_OK) {
			if (pool->cas_array) {
				if (FAILURE != zend_hash_find(Z_ARRVAL_P(pool->cas_array), key, key_len + 1, (void**)&cas_lookup)) {
					cas = **cas_lookup;
					cas_delete = 1;
				}
			}

			ret = mmc_exec_unl_cmd(pool, key, key_len, cas, INTERNAL_FUNCTION_PARAM_PASSTHRU);

		    if (ret < 0) {
				RETVAL_FALSE;
				if (ret == -3)			// Failed to send command to the server.
					cas_delete = 0; // so lets not clear the cas just yet.
		    } else {
				RETVAL_TRUE;
			}
		}
		else {
			RETVAL_FALSE;
		}
	}  else {
		RETVAL_FALSE;
	}

	if (cas_delete)
		zend_hash_del(Z_ARRVAL_P(pool->cas_array), (char *)key, key_len + 1);
}

void php_mmc_unlock_multi_by_key(mmc_pool_t *pool, zval *zkeys, zval **status_array,INTERNAL_FUNCTION_PARAMETERS)
{
	mmc_t *mmc;
	HashTable *key_hash;
	char *result_key, *value;
	char *key;
	unsigned int key_len;
	int retry_count = 0;
	zval **data;
	char shard_key[MMC_KEY_MAX_SIZE];
	char *cas_key, cas[MAX_LENGTH_OF_LONG];
	int cas_key_len = 0, cas_len = 0;
	unsigned int shard_key_len;
	bool is_cas = false;
	smart_str command_line[pool->num_servers];
	LogManager::setMultiLogger();

	int	i = 0, j, num_requests, result;

	array_init(*status_array);
	key_hash = Z_ARRVAL_P(zkeys);
	mmc_init_multi(TSRMLS_C);
	num_requests = 0;
	unsigned long **cas_lookup;

	for (zend_hash_internal_pointer_reset(key_hash);zend_hash_has_more_elements(key_hash) == SUCCESS;
			zend_hash_move_forward(key_hash)) {
		ulong idx;
		if (zend_hash_get_current_key_ex(key_hash, &key, &key_len,
					&idx, 0, NULL) == HASH_KEY_IS_STRING) {
			zval **zvalue;
			cas_len = 0;
			cas_key = NULL;
			LogManager::createLogger(key);
			LogManager::setLogger(key);
			LogManager::getLogger()->setCmd("unlockMultiByKey");
			LogManager::getLogger()->setLogName(pool->log_name);

			if (zend_hash_get_current_data(key_hash, (void **)&data) == FAILURE) {
				add_assoc_bool(*status_array, key, 0);
				LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
				continue;
			}

			if (Z_TYPE_PP(data) == IS_ARRAY) {
				HashTable *value_hash;
				value_hash = Z_ARRVAL_PP(data);
				if (zend_hash_find(value_hash, "shardKey", sizeof("shardKey"),
							(void **)&zvalue) == FAILURE) {
					zend_hash_move_forward(key_hash);
					LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
					continue;
				} else {
					mmc_prepare_key(*zvalue, shard_key, &shard_key_len TSRMLS_CC);
				}

				if (zend_hash_find(value_hash, "cas", sizeof("cas"),
							(void **)&zvalue) == SUCCESS) {
					if (Z_TYPE_PP(data) == IS_LONG) {
						cas_len = sprintf(cas, "%d", Z_LVAL_PP(zvalue));
					} else if (Z_TYPE_PP(data) == IS_STRING) {
						strcpy(cas, Z_STRVAL_PP(zvalue));
						cas_len = Z_STRLEN_PP(zvalue);
					}
				}
			} else if (!(mmc_prepare_key(*data, shard_key, &shard_key_len
					TSRMLS_CC) == MMC_OK)) {
				add_assoc_bool(*status_array, key, 0);
				LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
				continue;
			}

			if ((mmc = mmc_pool_find(pool, shard_key, shard_key_len TSRMLS_CC)) != NULL
					&& mmc->status != MMC_STATUS_FAILED) {

				LogManager::getLogger()->setHost(mmc->host);
				if (cas_len == 0) {
					cas_key_len = shard_key_len+key_len;
					cas_key = new char[cas_key_len];
					memcpy(cas_key, key, key_len-1);
					memcpy(cas_key+key_len-1, shard_key, shard_key_len+1);

					if (pool->cas_array && FAILURE != zend_hash_find(Z_ARRVAL_P(pool->cas_array),
								(char *)cas_key, cas_key_len , (void**)&cas_lookup)) {
						cas_len = sprintf(cas, "%d", **cas_lookup);
						zend_hash_del(Z_ARRVAL_P(pool->cas_array), (char *)cas_key, cas_key_len);
					} else {
						LogManager::getLogger()->setCode(VALUE_NOT_FOUND);
						add_assoc_bool_ex(*status_array, key, key_len, 0);
						continue;
					}
				}

				if (mmc->outbuf.len) {
					append_php_smart_string(&(mmc->outbuf), TERM, sizeof(TERM)-1);
					if (mmc->proxy && mmc->proxy_str_len) {
						append_php_smart_string(&(mmc->outbuf), mmc->proxy_str, mmc->proxy_str_len);
					}
				} else {
					mmc->index = num_requests;
					memset(&command_line[mmc->index], 0 , sizeof(smart_str));
					append_php_smart_string(&(command_line[mmc->index]), "unl", sizeof("unl")-1);
					pool->requests[num_requests++] = mmc;
				}

				append_php_smart_string(&(mmc->outbuf), "unl", sizeof("unl")-1);
				append_php_smart_string(&(mmc->outbuf), " ", 1);
				append_php_smart_string(&(mmc->outbuf), key, key_len-1);
				append_php_smart_string(&(mmc->outbuf), " ", 1);
				append_php_smart_string(&(mmc->outbuf), cas, cas_len);
				add_assoc_bool_ex(*status_array, key, key_len, 1);
				append_php_smart_string(&(command_line[mmc->index]), " ", 1);
				append_php_smart_string(&(command_line[mmc->index]), key, key_len-1);
				LogManager::getLogger()->setCode(MC_SUCCESS);
			} else {
				add_assoc_bool_ex(*status_array, key, key_len, 0);
				LogManager::getLogger()->setCode(CONNECT_FAILED);
				if (mmc) {
					LogManager::getLogger()->setHost(mmc->host);
				} else {
					LogManager::getLogger()->setHost(PROXY_STR);
				}
			}
			if (cas_key) {
				delete []cas_key;
			}
		}
	}

	for (j=0; j<num_requests; j++) {
		smart_str_0(&(pool->requests[j]->outbuf));
		smart_str_0(&(command_line[j]));

		if ((result = mmc_sendcmd(pool->requests[j], pool->requests[j]->outbuf.c,
						pool->requests[j]->outbuf.len TSRMLS_CC)) < 0) {
			mmc_server_failure(pool->requests[j] TSRMLS_CC);
			update_result_array(&(*status_array), command_line[j].c, WRITE_TO_SERVER_FAILED);
		}
	}

	for (j=0; j<num_requests; j++) {
		if (pool->requests[j]->status != MMC_STATUS_FAILED) {
			token_t tokens[MAX_TOKENS];
			int ntokens = tokenize_command(command_line[j].c, (token_t *)&tokens, MAX_TOKENS)-1;
			int tok_index = 1, response_len;
			mmc_t *mmc = pool->requests[j];

			do {
				tokens[tok_index].value[tokens[tok_index].length] = 0;
				LogManager::setLogger(tokens[tok_index].value);

				if ((response_len = mmc_readline(mmc TSRMLS_CC)) < 0){
					LogManager::getLogger()->setCode(READLINE_FAILED);
					MMC_DEBUG(("failed to read the server's response"));
					result = -2;
					break;
				}

				if (mmc_str_left(mmc->inbuf,"UNLOCKED", response_len, sizeof("UNLOCKED") - 1)) {
					LogManager::getLogger()->setCode(MC_UNLOCKED);
					add_assoc_bool_ex(*status_array, tokens[tok_index].value,
							tokens[tok_index].length + 1, 1);
				} else {
					LogManager::getLogger()->setCode(CMD_FAILED);
					add_assoc_bool_ex(*status_array, tokens[tok_index].value,
							tokens[tok_index].length + 1, 0);
				}
				tok_index++;
			} while (tok_index < ntokens);

			/* check for server failure */
			if (result < 0) {
				mmc_server_failure(mmc TSRMLS_CC);
				if (status_array) {
					for (i = 1; i < ntokens; i = i+1) {
						if (tokens[i].value) {
							add_assoc_bool_ex(*status_array, tokens[i].value, tokens[i].length + 1, 0);
							LogManager::getLogger(tokens[i].value)->setCode(READ_FROM_SERVER_FAILED);
						}
					}
				}
			}
		}
		smart_str_free(&(pool->requests[j]->outbuf));
		smart_str_free(&(command_line[j]));
	}

	mmc_free_multi(TSRMLS_C);
}

static void php_mmc_get(mmc_pool_t *pool, zval *zkey, char *key, zval **return_value, zval **status_array, zval *flags, zval *cas, bool return_corrupted) /* {{{ */
{
	unsigned int key_len;

	if (Z_TYPE_P(zkey) != IS_ARRAY) {
		LogManager::getLogger()->setCommandType(GET);
		if (mmc_prepare_key(zkey, key, &key_len TSRMLS_CC) == MMC_OK) {
			if (mmc_exec_retrieval_cmd(pool, key, key_len, return_value, flags, cas, return_corrupted TSRMLS_CC) < 0) {
				zval_dtor(*return_value);
				ZVAL_FALSE(*return_value);
			}
		}
		else {
			ZVAL_FALSE(*return_value);
		}
	} else if (zend_hash_num_elements(Z_ARRVAL_P(zkey))){
		if (mmc_exec_retrieval_cmd_multi(pool, zkey, return_value, status_array, flags, cas TSRMLS_CC) < 0) {
			zval_dtor(*return_value);
			ZVAL_FALSE(*return_value);
		}
	} else {
		ZVAL_FALSE(*return_value);
	}
}
/* }}} */

/* {{{ proto bool memcache_delete( object memcache, string key [, int expire ])
   Deletes existing item */
PHP_FUNCTION(memcache_delete)
{
	mmc_t *mmc;
	mmc_pool_t *pool;
	int result = -1, key_len;
	zval *mmc_object = getThis();
	char *key;
	long time = 0;
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("delete");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|l", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &time) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &key, &key_len, &time) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	LogManager::getLogger()->setKey(key);

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	while (result < 0 && (mmc = mmc_pool_find(pool, key_tmp, key_tmp_len TSRMLS_CC)) != NULL &&
		mmc->status != MMC_STATUS_FAILED) {
		LogManager::getLogger()->setHost(mmc->host);
		if ((result = mmc_delete(mmc, key_tmp, key_tmp_len, time TSRMLS_CC)) < 0) {
			LogManager::getLogger()->setCode(CMD_FAILED);
			mmc_server_failure(mmc TSRMLS_CC);
		}
	}

	if (!mmc) {
		LogManager::getLogger()->setCode(CONNECT_FAILED);
		LogManager::getLogger()->setHost(PROXY_STR);
	} else {
		if (mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
		}
		LogManager::getLogger()->setHost(mmc->host);
	}

	if (result > 0) {

		RETURN_TRUE;
	}


	RETURN_FALSE;
}

/* }}} */

PHP_FUNCTION(memcache_deleteByKey) {
	mmc_pool_t *pool;
	int key_len;
	int shard_key_len;
	zval *mmc_object = getThis();
	char *key;
	char *shard_key;
	long time = 0;
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	char shard_key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int shard_key_tmp_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("deleteByKey");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oss|l", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &shard_key, &shard_key_len, &time) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|l", &key, &key_len, &shard_key, &shard_key_len, &time) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "No pool defined");
		RETURN_FALSE;
	}

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Key not defined correctly");
		RETURN_FALSE;
	}

	if (mmc_prepare_key_ex(shard_key, shard_key_len, shard_key_tmp, &shard_key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Shard key not defined correctly");
		RETURN_FALSE;
	}

	if(php_mmc_delete_by_key(pool, key_tmp, key_tmp_len, shard_key_tmp, shard_key_tmp_len, time TSRMLS_CC) > 0) {
		RETURN_TRUE;
	} else {
		RETURN_FALSE;
	}

	return;
}

/**
 * Deletes an array of key/values in memcache.
 *
 * Returns true if delete was successful and false otherwise.
 */
PHP_FUNCTION(memcache_deleteMultiByKey) {
	zval *zkey_array;
	zval *mmc_object = getThis();
	long time = 0;
	mmc_pool_t *pool;

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oz|l", &mmc_object, memcache_class_entry_ptr, &zkey_array, &time) == FAILURE) {
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z|l", &zkey_array, &time) == FAILURE) {
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "No pool defined");
		RETURN_FALSE;
	}

	//Validate inputs
	if (zkey_array != NULL) {
		if (Z_TYPE_P(zkey_array) == IS_ARRAY) {
			php_mmc_delete_multi_by_key(pool, zkey_array, time, &return_value TSRMLS_CC);
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was not of expected array type");
			RETURN_FALSE;
		}
	} else {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Input was null");
		RETURN_FALSE;
	}

	return;
}

/**
 * Delete a key/value from memcache using the shard key to determine the node in the memcache cluster to delete from.
 *
 * @param pool The pool of memcache objects to use which defines the servers in the memcache cluster.
 * @param key The key to delete
 * @param ken_len The length of the key
 * @param shard_key The shard key to use to determine the node in the memcache cluster to delete from.
 * @param shard_key_len The length of the shard key
 * @param time Execution time of the item. If it's equal to zero, the item will be deleted right away whereas if you set
 * it to 30, the item will be deleted in 30 seconds.
 * @return An int > 0 if delete was successful and <= 0 if it failed.
 */
static int php_mmc_delete_by_key(mmc_pool_t *pool, char *key, int key_len, char *shard_key, int shard_key_len, int time TSRMLS_DC) {
	mmc_t *mmc;
	int result = -1;

	while (result < 0) {
		mmc = mmc_pool_find(pool, shard_key, shard_key_len TSRMLS_CC);

		if (mmc == NULL || mmc->status == MMC_STATUS_FAILED) {
			LogManager::getLogger()->setCode(CONNECT_FAILED);
			if (mmc) {
				LogManager::getLogger()->setHost(mmc->host);
			} else {
				LogManager::getLogger()->setHost(PROXY_STR);
			}
			break;
		} else {
			LogManager::getLogger()->setHost(mmc->host);
		}

		if ((result = mmc_delete(mmc, key, key_len, time TSRMLS_CC)) < 0) {
			mmc_server_failure(mmc TSRMLS_CC);
		}
	}

	return result;
}

/**
 * Executes a delete for every key in zkey_array.
 *
 * Example Input array:
 * Array
 * (
 *     [foo] => 123
 *     [foo1] => 123
 *     [foo2] => 123
 * )
 *
 * Example Result array:
 * Array
 * (
 *     [foo] => 1
 *     [foo1] => 1
 *     [foo2] => 0
 * )
 *
 * @param pool The pool of memcache objects to use which defines the servers in the memcache cluster.
 * @param zkey_array The key to shardKey associative array map.
 * @param time Execution time of the item. If it's equal to zero, the item will be deleted right away whereas if you set
 * it to 30, the item will be deleted in 30 seconds.
 * @param return_value The actual result of this call will be placed into this return value as an associative array.
 * @return array An associative array of key => status:
 *		status   - The status of true or false depending on whether or not the memcache transaction was successful.
 */
static void php_mmc_delete_multi_by_key(mmc_pool_t *pool, zval *zkey_array, int time, zval **return_value TSRMLS_DC) {
	MMC_DEBUG(("php_mmc_delete_multi_by_key: Entry"));

	HashTable *key_hash;
	int key_array_count = 0;
	mc_logger_t logData;

	// Start with a clean return array
	array_init(*return_value);

	key_hash = Z_ARRVAL_P(zkey_array);
	key_array_count = zend_hash_num_elements(key_hash);

	MMC_DEBUG(("php_mmc_delete_multi_by_key: The key array passed contains %d elements ", key_array_count));

	// Go through all of the key => value pairs and send the request without waiting for the responses just yet for performance improvements
	for (zend_hash_internal_pointer_reset(key_hash); zend_hash_has_more_elements(key_hash) == SUCCESS; zend_hash_move_forward(key_hash)) {
		LogManager lm(&logData);
		LogManager::getLogger()->setCmd("deleteMultiByKey");
		LogManager::getLogger()->setLogName(pool->log_name);
		char *input_key;
		unsigned int input_key_len;
		ulong idx;
		zval **data;

		if (zend_hash_get_current_key_ex(key_hash, &input_key, &input_key_len, &idx, 0, NULL) == HASH_KEY_IS_STRING) {
			LogManager::getLogger()->setKey(input_key);
			if (zend_hash_get_current_data(key_hash, (void**) &data) == FAILURE) {
				MMC_DEBUG(("php_mmc_delete_multi_by_key: No data for key '%s'", input_key));
				// Should never actually fail since the key is known to exist.
				add_assoc_bool(*return_value, input_key, 0);
				LogManager::getLogger()->setCode(PARSE_ERROR);
				continue;
			}

			MMC_DEBUG(("php_mmc_delete_multi_by_key: Sending delete for key '%s'", input_key));

			char shard_key[MMC_KEY_MAX_SIZE];
			unsigned int shard_key_len;

			mmc_prepare_key(*data, shard_key, &shard_key_len TSRMLS_CC);

			if (php_mmc_delete_by_key(pool, input_key, input_key_len-1, shard_key, shard_key_len, time TSRMLS_CC) > 0) {
				MMC_DEBUG(("php_mmc_delete_multi_by_key: delete succeeded for key '%s'", input_key));
				add_assoc_bool(*return_value, input_key, 1);
				LogManager::getLogger()->setCode(MC_DELETED);
			} else {
				MMC_DEBUG(("php_mmc_delete_multi_by_key: delete failed for key '%s'", input_key));
				LogManager::getLogger()->setCode(CMD_FAILED);
				add_assoc_bool(*return_value, input_key, 0);
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Key passed in wasn't a string type");
		}
	}

	MMC_DEBUG(("php_mmc_delete_multi_by_key: Exit"));
}

/* {{{ proto bool memcache_debug( bool onoff )
   Turns on/off internal debugging */
PHP_FUNCTION(memcache_debug)
{
/*
#if ZEND_DEBUG
*/
	zend_bool onoff;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &onoff) == FAILURE) {
		return;
	}

	MEMCACHE_G(debug_mode) = onoff ? 1 : 0;

	RETURN_TRUE;
/*
#else
	RETURN_FALSE;
#endif
*/

}
/* }}} */

/* {{{ proto array memcache_get_stats( object memcache [, string type [, int slabid [, int limit ] ] ])
   Returns server's statistics */
PHP_FUNCTION(memcache_get_stats)
{
	mmc_pool_t *pool;
	int i, failures = 0;
	zval *mmc_object = getThis();

	char *type = NULL;
	int type_len = 0;
	long slabid = 0, limit = MMC_DEFAULT_CACHEDUMP_LIMIT;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("getstats");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|sll", &mmc_object, memcache_class_entry_ptr, &type, &type_len, &slabid, &limit) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|sll", &type, &type_len, &slabid, &limit) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	for (i=0; i<pool->num_servers; i++) {
		if (mmc_open(pool->servers[i], 1, NULL, NULL TSRMLS_CC)) {
			if (mmc_get_stats(pool->servers[i], type, slabid, limit, return_value TSRMLS_CC) < 0) {
				mmc_server_failure(pool->servers[i] TSRMLS_CC);
				failures++;
			}
			else {
				LogManager::getLogger()->setHost(pool->servers[i]->host);
				break;
			}
		}
		else {
			failures++;
		}
	}


	if (failures >= pool->num_servers) {
		RETURN_FALSE;
	}
}
/* }}} */

/* {{{ proto array memcache_get_extended_stats( object memcache [, string type [, int slabid [, int limit ] ] ])
   Returns statistics for each server in the pool */
PHP_FUNCTION(memcache_get_extended_stats)
{
	mmc_pool_t *pool;
	char *hostname;
	int i, hostname_len;
	zval *mmc_object = getThis(), *stats;

	char *type = NULL;
	int type_len = 0;
	long slabid = 0, limit = MMC_DEFAULT_CACHEDUMP_LIMIT;

	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("getextendedstats");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|sll", &mmc_object, memcache_class_entry_ptr, &type, &type_len, &slabid, &limit) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|sll", &type, &type_len, &slabid, &limit) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	array_init(return_value);
	for (i=0; i<pool->num_servers; i++) {
		MAKE_STD_ZVAL(stats);

		hostname_len = spprintf(&hostname, 0, "%s:%d", pool->servers[i]->host, pool->servers[i]->port);

		if (MEMCACHE_G(proxy_enabled)) {
			pool->servers[i]->proxy = mmc_get_proxy(pool->servers[i] TSRMLS_C);
		}

		if (mmc_open(pool->servers[i], 1, NULL, NULL TSRMLS_CC)) {
			if (mmc_get_stats(pool->servers[i], type, slabid, limit, stats TSRMLS_CC) < 0) {
				mmc_server_failure(pool->servers[i] TSRMLS_CC);
				ZVAL_FALSE(stats);
			}
		}
		else {
			ZVAL_FALSE(stats);
		}

		add_assoc_zval_ex(return_value, hostname, hostname_len + 1, stats);
		efree(hostname);
	}

}
/* }}} */

/* {{{ proto array memcache_set_compress_threshold( object memcache, int threshold [, float min_savings ] )
   Set automatic compress threshold */
PHP_FUNCTION(memcache_set_compress_threshold)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	long threshold;
	double min_savings = MMC_DEFAULT_SAVINGS;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("setcompressthreshold");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ol|d", &mmc_object, memcache_class_entry_ptr, &threshold, &min_savings) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l|d", &threshold, &min_savings) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (threshold < 0) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "threshold must be a positive integer");
		LogManager::getLogger()->setCode(PARSE_ERROR);
		RETURN_FALSE;
	}
	pool->compress_threshold = threshold;

	if (min_savings != MMC_DEFAULT_SAVINGS) {
		if (min_savings < 0 || min_savings > 1) {
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "min_savings must be a float in the 0..1 range");
			LogManager::getLogger()->setCode(PARSE_ERROR);
			RETURN_FALSE;
		}
		pool->min_compress_savings = min_savings;
	}
	else {
		pool->min_compress_savings = MMC_DEFAULT_SAVINGS;
	}


	RETURN_TRUE;
}
/* }}} */
/* {{{ proto int memcache_increment( object memcache, string key [, int value ] )
   Increments existing variable */
PHP_FUNCTION(memcache_increment) {
	mmc_pool_t *pool;
	int key_len;
	long value = 1;
	char *key;
	zval *mmc_object = getThis();
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("increment");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|l", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &key, &key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	php_mmc_incr_decr(pool, key_tmp, key_tmp_len, NULL, 0, value, 0, 1, &return_value);

	LogManager::getLogger()->setKey(key_tmp);

	return;
}
/* }}} */

/**
 * Increments an existing key.
 *
 * @param key The key to increment
 * @param shardKey The key to use for determining which node in the memcache cluster to use.
 * @param value (Optional) The increment value.  Defaults to 1 if not passed.
 * @return The value of the key after it was incremented or false if there was a problem or if the key doesn't exist.
 */
PHP_FUNCTION(memcache_incrementByKey) {
	mmc_pool_t *pool;
	int key_len;
	int shard_key_len;
	long value = 1;
	char *key;
	char *shard_key;
	zval *mmc_object = getThis();
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	char shard_key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int shard_key_tmp_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("incrementbykey");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oss|l", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &shard_key, &shard_key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|l", &key, &key_len, &shard_key, &shard_key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	if (mmc_prepare_key_ex(shard_key, shard_key_len, shard_key_tmp, &shard_key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	php_mmc_incr_decr(pool, key_tmp, key_tmp_len, shard_key_tmp, shard_key_tmp_len, value, 1, 1, &return_value);

	LogManager::getLogger()->setKey(key_tmp);

	return;
}

/* {{{ proto int memcache_decrement( object memcache, string key [, int value ] )
   Decrements existing variable */
PHP_FUNCTION(memcache_decrement) {
	mmc_pool_t *pool;
	int key_len;
	long value = 1;
	char *key;
	zval *mmc_object = getThis();
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("decrement");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os|l", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s|l", &key, &key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setKey(key_tmp);

	php_mmc_incr_decr(pool, key_tmp, key_tmp_len, NULL, 0, value, 0, 0, &return_value);

	return;
}
/* }}} */

/**
 * Decrements an existing key.
 *
 * @param key The key to decrement.
 * @param shardKey The key to use for determining which node in the memcache cluster to use.
 * @param value (Optional) The decrement value.  Defaults to 1 if not passed.
 * @return The value of the key after it was decremented or false if there was a problem or if the key doesn't exist.
 */
PHP_FUNCTION(memcache_decrementByKey) {
	mmc_pool_t *pool;
	int key_len;
	int shard_key_len;
	long value = 1;
	char *key;
	char *shard_key;
	zval *mmc_object = getThis();
	char key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int key_tmp_len;
	char shard_key_tmp[MMC_KEY_MAX_SIZE];
	unsigned int shard_key_tmp_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("decrementbykey");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Oss|l", &mmc_object, memcache_class_entry_ptr, &key, &key_len, &shard_key, &shard_key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	} else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|l", &key, &key_len, &shard_key, &shard_key_len, &value) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC) || !pool->num_servers) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (mmc_prepare_key_ex(key, key_len, key_tmp, &key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	if (mmc_prepare_key_ex(shard_key, shard_key_len, shard_key_tmp, &shard_key_tmp_len TSRMLS_CC) != MMC_OK) {
		LogManager::getLogger()->setCode(PREPARE_KEY_FAILED);
		RETURN_FALSE;
	}

	php_mmc_incr_decr(pool, key_tmp, key_tmp_len, shard_key_tmp, shard_key_tmp_len, value, 1, 0, &return_value);

	LogManager::getLogger()->setKey(key_tmp);

	return;
}

/* {{{ proto bool memcache_close( object memcache )
   Closes connection to memcached */
PHP_FUNCTION(memcache_close)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("close");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &mmc_object, memcache_class_entry_ptr) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (!mmc_pool_close(pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(CLOSE_FAILED);
		RETURN_FALSE;
	}


	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_flush( object memcache [, int timestamp ] )
   Flushes cache, optionally at the specified time */
PHP_FUNCTION(memcache_flush)
{
	mmc_pool_t *pool;
	int i, failures = 0;
	zval *mmc_object = getThis();
	long timestamp = 0;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("flush");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|l", &mmc_object, memcache_class_entry_ptr, &timestamp) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &timestamp) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);
	for (i=0; i<pool->num_servers; i++) {
		if (mmc_open(pool->servers[i], 1, NULL, NULL TSRMLS_CC)) {
			if (mmc_flush(pool->servers[i], timestamp TSRMLS_CC) < 0) {
				mmc_server_failure(pool->servers[i] TSRMLS_CC);
				failures++;
			}
		}
		else {
			failures++;
		}
	}

	if (failures && failures >= pool->num_servers) {
		LogManager::getLogger()->setCode(FLUSH_FAILED);
		RETURN_FALSE;
	}


	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_setproperty( object memcache , string property, mixed val )
   set a property on a memcache pool*/
PHP_FUNCTION(memcache_setproperty)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	mmc_t *mmc;
	zval *val;
	char *prop;
	int prop_len;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("setproperty");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Osz", &mmc_object, memcache_class_entry_ptr, &prop, &prop_len, &val) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz", &prop, &prop_len, &val) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (prop == NULL || prop_len <= 0 || !mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	if (strncasecmp(prop, "NullOnKeyMiss", prop_len) == 0) {
		if (val != NULL && Z_TYPE_P(val) == IS_BOOL) {
			pool->false_on_error = Z_BVAL_P(val);
		}
	}

	if (strncasecmp(prop, "ProtocolBinary", prop_len) == 0) {
		if (val != NULL && Z_TYPE_P(val) == IS_BOOL) {
			int use_binary = Z_BVAL_P(val);
			int i = 0;
			for (i = 0; i < pool->num_servers; i++) {
				mmc = pool->servers[i];
				if (mmc->proxy_str)
					mmc->proxy_str[0] = use_binary ? 'B' : 'A';
			}
		}
	}

	if (strncasecmp(prop, "EnableChecksum", prop_len) == 0) {
		if (val != NULL && Z_TYPE_P(val) == IS_BOOL) {
			pool->enable_checksum = Z_BVAL_P(val);
		}
	}


	RETURN_TRUE;
}
/* }}} */


/* {{{ proto bool memcache_enable_proxy( object memcache , bool enabled )
   Enable/disable proxy for a memcache pool*/
PHP_FUNCTION(memcache_enable_proxy)
{
	mmc_pool_t *pool;
	zval *mmc_object = getThis();
	zend_bool onoff;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("enableproxy");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ob", &mmc_object, memcache_class_entry_ptr, &onoff) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "b", &onoff) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);
	pool->proxy_enabled = onoff;



	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool memcache_setoptimeout( object memcache , int timeoutms )
   Set the timeout, in milliseconds, for subsequent operations on all open connections */
PHP_FUNCTION(memcache_setoptimeout)
{
	mmc_pool_t *pool;
	mmc_t *mmc;
	int i;
	zval *mmc_object = getThis();
	long timeoutms = 0;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("settimeout");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Ol", &mmc_object, memcache_class_entry_ptr, &timeoutms) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &timeoutms) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (timeoutms < 1) {
		timeoutms = MEMCACHE_G(default_timeout_ms);
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
		RETURN_FALSE;
	}

	LogManager::getLogger()->setLogName(pool->log_name);

	for (i = 0; i < pool->num_servers; i++) {
		mmc = pool->servers[i];
		mmc->timeoutms = timeoutms;
	}

	RETURN_TRUE;
}
 /* }}} */

/* {{{ setting the log name for a mc class, according to which logging will be done*/
PHP_FUNCTION(memcache_setlogname)
{
	mmc_pool_t *pool;
	mmc_t *mmc;
	int i;
	zval *mmc_object = getThis();
	char *log_name;
	int log_name_len = 0;
	mc_logger_t logData;
	LogManager lm(&logData);

	LogManager::getLogger()->setCmd("setlogname");

	if (mmc_object == NULL) {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "Os", &mmc_object, memcache_class_entry_ptr, &log_name,
			&log_name_len) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}
	else {
		if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &log_name, &log_name_len ) == FAILURE) {
			LogManager::getLogger()->setCode(PARSE_ERROR);
			return;
		}
	}

	if (!mmc_get_pool(mmc_object, &pool TSRMLS_CC)) {
		LogManager::getLogger()->setCode(POOL_NT_FOUND);
	    RETURN_FALSE;
   	}

	if (pool->log_name) {
		pefree(pool->log_name, 1);
	}

   	pool->log_name = pestrdup(log_name, 1);
	LogManager::getLogger()->setLogName(pool->log_name);

	RETURN_TRUE;
}
/*}}}*/

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
