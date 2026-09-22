/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _APPLE_DPIN_HANDSHAKE_H
#define _APPLE_DPIN_HANDSHAKE_H

#include <linux/errno.h>

#define APPLE_DPIN_HPD 0x00
#define APPLE_DPIN_CONTROL 0x0c
#define APPLE_DPIN_ACK 0x10
#define APPLE_DPIN_HPD_LEVEL (1U << 2)
#define APPLE_DPIN_INACTIVE 1U

/* Caller owns powered register access and provides a bounded wait. */
struct apple_dpin_io {
	unsigned int (*read)(void *ctx, unsigned int offset);
	void (*write)(void *ctx, unsigned int offset, unsigned int value);
	int (*wait)(void *ctx);
	void *ctx;
};

/* Only CONTROL bit 0 is written; HPD and ACK are read-only here. */
static inline int apple_dpin_handshake(const struct apple_dpin_io *io,
				      int active)
{
	unsigned int hpd, saved, value, ack;
	int ret;

	if (active) {
		hpd = io->read(io->ctx, APPLE_DPIN_HPD);
		if (hpd == ~0U)
			return -EIO;
		if (!(hpd & APPLE_DPIN_HPD_LEVEL))
			return -ENOLINK;
	}
	saved = io->read(io->ctx, APPLE_DPIN_CONTROL);
	if (saved == ~0U)
		return -EIO;
	value = active ? saved & ~APPLE_DPIN_INACTIVE :
			 saved | APPLE_DPIN_INACTIVE;
	io->write(io->ctx, APPLE_DPIN_CONTROL, value);
	for (;;) {
		ack = io->read(io->ctx, APPLE_DPIN_ACK);
		if (ack == ~0U) {
			ret = -EIO;
			break;
		}
		if ((ack & APPLE_DPIN_INACTIVE) ==
		    (value & APPLE_DPIN_INACTIVE))
			return 0;
		if (active) {
			hpd = io->read(io->ctx, APPLE_DPIN_HPD);
			if (hpd == ~0U || !(hpd & APPLE_DPIN_HPD_LEVEL)) {
				ret = hpd == ~0U ? -EIO : -ENOLINK;
				break;
			}
		}
		ret = io->wait(io->ctx);
		if (ret)
			break;
	}
	/* Restore only our bit; do not invent resets on a failed handshake. */
	ack = io->read(io->ctx, APPLE_DPIN_CONTROL);
	if (ack != ~0U)
		io->write(io->ctx, APPLE_DPIN_CONTROL,
			  (ack & ~APPLE_DPIN_INACTIVE) |
			  (saved & APPLE_DPIN_INACTIVE));
	return ret;
}
#endif
