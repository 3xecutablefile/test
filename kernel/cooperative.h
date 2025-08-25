#ifndef BH_COOPERATIVE_H
#define BH_COOPERATIVE_H

#include <stddef.h>
#include <sys/types.h>

#ifdef __KERNEL__

struct file;

/* Allocate host-provided shared memory */
int coop_alloc_shared(struct file *file, size_t size);

/* Map shared pages into the guest kernel address space */
int coop_map_guest_pages(struct file *file, unsigned long addr, size_t size);

/* Character device for console I/O */
int coop_console_init(void);
void coop_console_exit(void);

/* Stub block device exposing /dev/coop_root */
int coop_block_init(void);
void coop_block_exit(void);

/* Stub network device exposing /dev/coop_net0 */
int coop_net_init(void);
void coop_net_exit(void);

#else /* !__KERNEL__ */

#include <sys/syscall.h>
#ifndef __NR_coop_yield
#define __NR_coop_yield 548
#endif

// Initialize a shared memory segment used for host/guest communication.
// `name` identifies the segment and should begin with a '/'.
// Returns 0 on success, -1 on failure.
int coop_init(const char *name, size_t size);

// Yield execution to the host operating system.
int coop_yield(void);

// Send bytes to the host through the shared ring buffer.
ssize_t coop_send(const void *buf, size_t len);

// Receive up to maxlen bytes from the shared ring buffer.
ssize_t coop_recv(void *buf, size_t maxlen);

// Tear down the shared memory segment.
void coop_cleanup(void);

#endif /* __KERNEL__ */

#endif // BH_COOPERATIVE_H
