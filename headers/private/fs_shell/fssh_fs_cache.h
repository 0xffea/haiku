/*
 * Copyright 2004-2007, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FSSH_FS_CACHE_H
#define _FSSH_FS_CACHE_H

//! File System File and Block Caches


#include "fssh_fs_interface.h"


typedef void (*fssh_transaction_notification_hook)(int32_t id, void *data);

#ifdef __cplusplus
extern "C" {
#endif 

/* transactions */
extern int32_t			fssh_cache_start_transaction(void *_cache);
extern fssh_status_t	fssh_cache_sync_transaction(void *_cache, int32_t id);
extern fssh_status_t	fssh_cache_end_transaction(void *_cache, int32_t id,
							fssh_transaction_notification_hook hook,
							void *data);
extern fssh_status_t	fssh_cache_abort_transaction(void *_cache, int32_t id);
extern int32_t			fssh_cache_detach_sub_transaction(void *_cache,
							int32_t id, fssh_transaction_notification_hook hook,
							void *data);
extern fssh_status_t	fssh_cache_abort_sub_transaction(void *_cache,
							int32_t id);
extern fssh_status_t	fssh_cache_start_sub_transaction(void *_cache,
							int32_t id);
extern fssh_status_t	fssh_cache_next_block_in_transaction(void *_cache,
							int32_t id, uint32_t *_cookie,
							fssh_off_t *_blockNumber, void **_data,
							void **_unchangedData);
extern int32_t			fssh_cache_blocks_in_transaction(void *_cache,
							int32_t id);
extern int32_t			fssh_cache_blocks_in_sub_transaction(void *_cache,
							int32_t id);

/* block cache */
extern void				fssh_block_cache_delete(void *_cache, bool allowWrites);
extern void *			fssh_block_cache_create(int fd, fssh_off_t numBlocks,
							fssh_size_t blockSize, bool readOnly);
extern fssh_status_t	fssh_block_cache_sync(void *_cache);
extern fssh_status_t	fssh_block_cache_sync_etc(void *_cache,
							fssh_off_t blockNumber, fssh_size_t numBlocks);
extern fssh_status_t	fssh_block_cache_make_writable(void *_cache,
							fssh_off_t blockNumber, int32_t transaction);
extern void *			fssh_block_cache_get_writable_etc(void *_cache,
							fssh_off_t blockNumber, fssh_off_t base,
							fssh_off_t length, int32_t transaction);
extern void *			fssh_block_cache_get_writable(void *_cache,
							fssh_off_t blockNumber, int32_t transaction);
extern void *			fssh_block_cache_get_empty(void *_cache,
							fssh_off_t blockNumber, int32_t transaction);
extern const void *		fssh_block_cache_get_etc(void *_cache,
							fssh_off_t blockNumber, fssh_off_t base,
							fssh_off_t length);
extern const void *		fssh_block_cache_get(void *_cache,
							fssh_off_t blockNumber);
extern fssh_status_t	fssh_block_cache_set_dirty(void *_cache,
							fssh_off_t blockNumber, bool isDirty,
							int32_t transaction);
extern void				fssh_block_cache_put(void *_cache,
							fssh_off_t blockNumber);

/* file cache */
extern void *			fssh_file_cache_create(fssh_mount_id mountID,
							fssh_vnode_id vnodeID, fssh_off_t size);
extern void				fssh_file_cache_delete(void *_cacheRef);
extern fssh_status_t	fssh_file_cache_set_size(void *_cacheRef,
							fssh_off_t size);
extern fssh_status_t	fssh_file_cache_sync(void *_cache);

extern fssh_status_t	fssh_file_cache_read(void *_cacheRef, void *cookie,
							fssh_off_t offset, void *bufferBase,
							fssh_size_t *_size);
extern fssh_status_t	fssh_file_cache_write(void *_cacheRef, void *cookie,
							fssh_off_t offset, const void *buffer,
							fssh_size_t *_size);

/* file map */
extern void *			fssh_file_map_create(fssh_mount_id mountID,
							fssh_vnode_id vnodeID, fssh_off_t size);
extern void				fssh_file_map_delete(void *_map);
extern void				fssh_file_map_set_size(void *_map, fssh_off_t size);
extern void				fssh_file_map_invalidate(void *_map, fssh_off_t offset,
							fssh_off_t size);
extern fssh_status_t	fssh_file_map_translate(void *_map, fssh_off_t offset,
							fssh_size_t size, struct fssh_file_io_vec *vecs,
							fssh_size_t *_count);

#ifdef __cplusplus
}
#endif 

#endif	/* _FSSH_FS_CACHE_H */
