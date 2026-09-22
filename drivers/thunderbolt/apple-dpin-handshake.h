/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _APPLE_DPIN_HANDSHAKE_H
#define _APPLE_DPIN_HANDSHAKE_H

#include <linux/errno.h>

#define APPLE_DPIN_HPD 0x00
#define APPLE_DPIN_CONTROL 0x0c
#define APPLE_DPIN_ACK 0x10
#define APPLE_DPIN_HPD_LEVEL (1U << 2)
#define APPLE_DPIN_INACTIVE 1U

/*
 * Set unconditionally by native AppleCIODPTX::bringConnectionUp on both the
 * HPD and CONTROL registers of this exact DPIN0 block when bringing a
 * connection up (single-lane-group case, which is what this port is). The
 * HPD-register write is additionally gated there on a single/multi-stream
 * check; every topology this driver drives through DPIN0 is a single
 * external monitor (SST), so it is unconditional here too. Neither write
 * clears any other bit (native masks both with 0, a pure OR). Not written
 * on deactivate: native's teardown path was not traced, so this preserves
 * the existing "do not invent resets" behavior there.
 */
#define APPLE_DPIN_CONNECTED (1U << 1)

/* Caller owns powered register access and provides a bounded wait. */
struct apple_dpin_io {
	unsigned int (*read)(void *ctx, unsigned int offset);
	void (*write)(void *ctx, unsigned int offset, unsigned int value);
	int (*wait)(void *ctx);
	void *ctx;
};

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
	if (active)
		io->write(io->ctx, APPLE_DPIN_HPD, hpd | APPLE_DPIN_CONNECTED);
	value = active ? (saved & ~APPLE_DPIN_INACTIVE) | APPLE_DPIN_CONNECTED :
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
	/*
	 * Restore only the bits we own (INACTIVE, and CONNECTED now that we
	 * set it above); do not invent resets on a failed handshake. HPD is
	 * left as-is here: native's teardown path for it was not traced, so
	 * no rollback is invented for a register this driver otherwise never
	 * wrote before this change.
	 */
	ack = io->read(io->ctx, APPLE_DPIN_CONTROL);
	if (ack != ~0U)
		io->write(io->ctx, APPLE_DPIN_CONTROL,
			  (ack & ~(APPLE_DPIN_INACTIVE | APPLE_DPIN_CONNECTED)) |
			  (saved & (APPLE_DPIN_INACTIVE | APPLE_DPIN_CONNECTED)));
	return ret;
}
#endif
