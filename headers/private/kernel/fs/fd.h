/* 
 * Copyright 2002-2005, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _FD_H
#define _FD_H


#include <vfs.h>
#include <team.h>
#include <thread.h>


#ifdef __cplusplus
extern "C" {
#endif

struct file_descriptor;
struct select_sync;

struct fd_ops {
	status_t	(*fd_read)(struct file_descriptor *, off_t pos, void *buffer, size_t *length);
	status_t	(*fd_write)(struct file_descriptor *, off_t pos, const void *buffer, size_t *length);
	off_t		(*fd_seek)(struct file_descriptor *, off_t pos, int seekType);
	status_t	(*fd_ioctl)(struct file_descriptor *, ulong op, void *buffer, size_t length);
	status_t	(*fd_select)(struct file_descriptor *, uint8 event, uint32 ref, struct select_sync *sync);
	status_t	(*fd_deselect)(struct file_descriptor *, uint8 event, struct select_sync *sync);
	status_t	(*fd_read_dir)(struct file_descriptor *, struct dirent *buffer, size_t bufferSize, uint32 *_count);
	status_t	(*fd_rewind_dir)(struct file_descriptor *);
	status_t	(*fd_read_stat)(struct file_descriptor *, struct stat *);
	status_t	(*fd_write_stat)(struct file_descriptor *, const struct stat *, int statMask);
	status_t	(*fd_close)(struct file_descriptor *);
	void		(*fd_free)(struct file_descriptor *);
};

struct file_descriptor {
	int32	type;               /* descriptor type */
	int32	ref_count;
	int32	open_count;
	struct fd_ops *ops;
	union {
		struct vnode *vnode;
		struct fs_mount *mount;
	} u;
	void	*cookie;
	int32	open_mode;
	off_t	pos;
};


/* Types of file descriptors we can create */

enum fd_types {
	FDTYPE_FILE	= 1,
	FDTYPE_ATTR,
	FDTYPE_DIR,
	FDTYPE_ATTR_DIR,
	FDTYPE_INDEX,
	FDTYPE_INDEX_DIR,
	FDTYPE_QUERY,
	FDTYPE_SOCKET
};


/* Prototypes */

extern struct file_descriptor *alloc_fd(void);
extern int new_fd_etc(struct io_context *, struct file_descriptor *, int firstIndex);
extern int new_fd(struct io_context *, struct file_descriptor *);
extern struct file_descriptor *get_fd(struct io_context *, int);
extern void close_fd(struct file_descriptor *descriptor);
extern void put_fd(struct file_descriptor *descriptor);
extern status_t select_fd(int fd, uint8 event, uint32 ref, struct select_sync *sync, bool kernel);
extern status_t deselect_fd(int fd, uint8 event, struct select_sync *sync, bool kernel);
extern bool fd_is_valid(int fd, bool kernel);

extern bool fd_close_on_exec(struct io_context *context, int fd);
extern void fd_set_close_on_exec(struct io_context *context, int fd, bool closeFD);

static struct io_context *get_current_io_context(bool kernel);

/* The prototypes of the (sys|user)_ functions are currently defined in vfs.h */


/* Inlines */

static inline struct io_context *
get_current_io_context(bool kernel)
{
	if (kernel)
		return (struct io_context *)team_get_kernel_team()->io_context;

	return (struct io_context *)thread_get_current_thread()->team->io_context;
}

#ifdef __cplusplus
}
#endif

#endif /* _FD_H */
