/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for the MA35H0 DMA Controller driver
 *
 * Copyright (C) 2020 Nuvoton Technology Corp
 */

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H

#include <linux/types.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
struct ma35h0_peripheral {
	u32 reqsel;
	u32 timeout_prescaler;
	u32 timeout_counter;
};

struct ma35h0_dma_done {
	bool	done;
	wait_queue_head_t	*wait;
	int	ch;
	u32	base_addr;
	int	timeout;
	int	remain;
	void	*callback_param;
};

#endif /* __ASM_ARCH_DMA_H */
