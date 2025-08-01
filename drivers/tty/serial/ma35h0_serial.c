// SPDX-License-Identifier: GPL-2.0+
/*
 *  MA35H0 serial driver
 *
 *  Copyright (C) 2018 Nuvoton Technology Corp.
 *
 */

#if defined(CONFIG_SERIAL_MA35H0_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/clk.h>
#include <linux/serial_reg.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/nmi.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/serial.h>
#include <linux/platform_data/dma-ma35h0.h>
#include "ma35h0_serial.h"

//#define EN_UART_PDMA_RX

#define UART_NR 17
#define UART_RX_BUF_SIZE 4096
#define UART_TX_MAX_BUF_SIZE 128

/* PDMA mode time-out */
#define Time_Out_Frame_Count 2
#define Time_Out_Low_Baudrate 115200

#define RDA_TOUT_IF (RDA_IF | TOUT_IF)
#define RDA_PTO_IF  (RDA_IF | PTO_IF)

unsigned char UART_PDMA_TX_ID[UART_NR] = {
	PDMA_UART0_TX, PDMA_UART1_TX, PDMA_UART2_TX,
	PDMA_UART3_TX, PDMA_UART4_TX, PDMA_UART5_TX,
	PDMA_UART6_TX, PDMA_UART7_TX, PDMA_UART8_TX,
	PDMA_UART9_TX, PDMA_UART10_TX, PDMA_UART11_TX,
	PDMA_UART12_TX, PDMA_UART13_TX, PDMA_UART14_TX,
	PDMA_UART15_TX, PDMA_UART16_TX
};

#ifdef EN_UART_PDMA_RX
unsigned char UART_PDMA_RX_ID[UART_NR] = {
	PDMA_UART0_RX, PDMA_UART1_RX, PDMA_UART2_RX,
	PDMA_UART3_RX, PDMA_UART4_RX, PDMA_UART5_RX,
	PDMA_UART6_RX, PDMA_UART7_RX, PDMA_UART8_RX,
	PDMA_UART9_RX, PDMA_UART10_RX, PDMA_UART11_RX,
	PDMA_UART12_RX, PDMA_UART13_RX, PDMA_UART14_RX,
	PDMA_UART15_RX, PDMA_UART16_RX
};
#endif

static struct uart_driver ma35h0serial_reg;

struct clk      *clk;

struct uart_ma35h0_port {
	struct uart_port    port;

	unsigned short      capabilities; /* port capabilities */
	unsigned char       ier;
	unsigned char       lcr;
	unsigned char       mcr;
	unsigned char       mcr_mask; /* mask of user bits */
	unsigned char       mcr_force; /* mask of forced bits */

	struct serial_rs485 rs485; /* rs485 settings */

#ifdef EN_UART_PDMA_RX
	struct ma35h0_ip_rx_dma dma_rx;
#endif
	struct ma35h0_ip_tx_dma dma_tx;
	struct ma35h0_mem_alloc src_mem_p;
#ifdef EN_UART_PDMA_RX
	struct ma35h0_mem_alloc dest_mem_p;
#endif
	struct ma35h0_dma_done   dma_slave_done;

	unsigned char PDMA_UARTx_TX;
#ifdef EN_UART_PDMA_RX
	unsigned char PDMA_UARTx_RX;
#endif

#ifdef EN_UART_PDMA_RX
	struct ma35h0_dma_done   dma_Rx_done;
#endif
	struct ma35h0_dma_done   dma_Tx_done;

#ifdef EN_UART_PDMA_RX
	u64 pdma_rx_vir_addr1;
	u64 pdma_rx_vir_addr2;
	u64 pdma_rx_phy_addr1;
	u64 pdma_rx_phy_addr2;
#endif

	unsigned int tx_dma_len;

	unsigned char uart_pdma_enable_flag;
	unsigned char Tx_pdma_busy_flag;

#ifdef EN_UART_PDMA_RX
	unsigned int pdma_time_out_prescaler;
	unsigned int pdma_time_out_count;
#endif
	unsigned int baud_rate;

	unsigned int console_baud_rate;
	unsigned int console_line;
	unsigned int console_int;

	unsigned char wakeup_enable;

	unsigned int fifo_threshold;

	int max_count;

	/* We provide a per-port pm hook. */
	void    (*pm)(struct uart_port *port,
			unsigned int state, unsigned int old);
};

static struct uart_ma35h0_port ma35h0serial_ports[UART_NR] = {0};


static inline void __stop_tx(struct uart_ma35h0_port *p);

#ifdef EN_UART_PDMA_RX
static void ma35h0_prepare_RX_dma(struct uart_ma35h0_port *p);
#endif
static void ma35h0_prepare_TX_dma(struct uart_ma35h0_port *p);

static inline struct uart_ma35h0_port *
to_ma35h0_uart_port(struct uart_port *uart)
{
	return container_of(uart, struct uart_ma35h0_port, port);
}

static inline unsigned int serial_in(struct uart_ma35h0_port *p,
									int offset)
{
	return __raw_readl(p->port.membase + offset);
}

static inline void serial_out(struct uart_ma35h0_port *p, int offset,
								int value)
{
	__raw_writel(value, p->port.membase + offset);
}

#ifdef EN_UART_PDMA_RX
static void ma35h0_Rx_dma_callback(void *arg)
{
	struct ma35h0_dma_done *done = arg;
	struct uart_ma35h0_port *p =
			(struct uart_ma35h0_port *)done->callback_param;
	struct ma35h0_ip_rx_dma *pdma_rx = &(p->dma_rx);
	struct tty_port    *tty_port = &p->port.state->port;
	int count;
	int copied_count = 0;
	unsigned char rx_tmp_buf[65];
	uint32_t i = 0;
	struct dma_tx_state state;
	enum dma_status status;
	unsigned long flags;

	spin_lock_irqsave(&p->port.lock, flags);

	status = dmaengine_tx_status(pdma_rx->chan_rx, pdma_rx->cookie,
								&state);
	if (state.residue != 0)
		count = ((p->dest_mem_p.size) - (state.residue + 1));
	else
		count = (p->dest_mem_p.size);

	if (p->pdma_rx_phy_addr2 == p->dest_mem_p.phy_addr) {
		p->dest_mem_p.phy_addr = p->pdma_rx_phy_addr1;
		p->dest_mem_p.vir_addr = p->pdma_rx_vir_addr1;

		i = 0;
		serial_out(p, UART_REG_IER,
				(serial_in(p, UART_REG_IER) & ~RXPDMAEN));
		while (!(serial_in(p, UART_REG_FSR) & RX_EMPTY)) {
			rx_tmp_buf[i] = (unsigned char)serial_in(p, UART_REG_RBR);
			i++;

			if (i > 64)
				dev_err(p->port.dev, "rx_tmp_buf bull\n");
		}


		ma35h0_prepare_RX_dma(p);
		/* Trigger Rx dma again */
		serial_out(p, UART_REG_IER,
				(serial_in(p, UART_REG_IER)|RXPDMAEN|TIME_OUT_EN));

		dma_sync_single_for_cpu(pdma_rx->chan_rx->device->dev,
				p->pdma_rx_phy_addr2, UART_RX_BUF_SIZE, DMA_FROM_DEVICE);

		if (i > 0)
			memcpy((void *)(p->pdma_rx_vir_addr2 + count), rx_tmp_buf, i);

		copied_count = tty_insert_flip_string(tty_port,
					((unsigned char *)p->pdma_rx_vir_addr2), count + i);
	} else {
		p->dest_mem_p.phy_addr = p->pdma_rx_phy_addr2;
		p->dest_mem_p.vir_addr = p->pdma_rx_vir_addr2;

		i = 0;
		serial_out(p, UART_REG_IER,
				(serial_in(p, UART_REG_IER) & ~RXPDMAEN));
		while (!(serial_in(p, UART_REG_FSR) & RX_EMPTY)) {
			rx_tmp_buf[i] = (unsigned char)serial_in(p, UART_REG_RBR);
			i++;

			if (i > 64)
				dev_err(p->port.dev, "rx_tmp_buf bull\n");
		}

		ma35h0_prepare_RX_dma(p);
		/* Trigger Rx dma again */
		serial_out(p, UART_REG_IER,
				(serial_in(p, UART_REG_IER)|RXPDMAEN|TIME_OUT_EN));

		dma_sync_single_for_cpu(pdma_rx->chan_rx->device->dev,
			p->pdma_rx_phy_addr1, UART_RX_BUF_SIZE, DMA_FROM_DEVICE);

		if (i > 0)
			memcpy((void *)(p->pdma_rx_vir_addr1 + count), rx_tmp_buf, i);

		copied_count = tty_insert_flip_string(tty_port,
					((unsigned char *)p->pdma_rx_vir_addr1), count + i);
	}

	if (copied_count != (count + i)) {
		p->port.icount.overrun++;
	}

	p->port.icount.rx = p->port.icount.rx + copied_count;

	tty_flip_buffer_push(tty_port);

	spin_unlock_irqrestore(&p->port.lock, flags);
}
#endif

static void ma35h0_Tx_dma_callback(void *arg)
{
	struct ma35h0_dma_done *done = arg;
	struct uart_ma35h0_port *p =
			(struct uart_ma35h0_port *)done->callback_param;
	struct circ_buf *xmit = &p->port.state->xmit;
	unsigned long flags;

	spin_lock_irqsave(&p->port.lock, flags);

	p->port.icount.tx += p->tx_dma_len;

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&p->port);

	if (!uart_circ_empty(xmit) && !uart_tx_stopped(&p->port)) {
		p->Tx_pdma_busy_flag = 1;
		ma35h0_prepare_TX_dma(p);
		/* Trigger Tx dma again */
		serial_out(p, UART_REG_IER,
				(serial_in(p, UART_REG_IER) | TXPDMAEN));
	} else
		p->Tx_pdma_busy_flag = 0;

	spin_unlock_irqrestore(&p->port.lock, flags);
}

static void set_pdma_flag(struct uart_ma35h0_port *p, int id)
{
	p->uart_pdma_enable_flag = 1;
#ifdef EN_UART_PDMA_RX
	p->PDMA_UARTx_RX = UART_PDMA_RX_ID[id];
#endif
	p->PDMA_UARTx_TX = UART_PDMA_TX_ID[id];
}

#ifdef EN_UART_PDMA_RX
void ma35h0_uart_cal_pdma_time_out(struct uart_ma35h0_port *p, unsigned int baud)
{
	unsigned int lcr;
	unsigned int pdma_time_out_base = 180000000 * Time_Out_Frame_Count / 256;
	unsigned int time_out_prescaler = 1;
	unsigned int bit_length;
	unsigned int time_out;

	if (baud > Time_Out_Low_Baudrate) {
		p->pdma_time_out_count = 255 * 16;
		p->pdma_time_out_prescaler = 7 * 16;

		return;
	}

	bit_length = 2; /* 1 start + 1 stop bit */

	lcr = serial_in(p, UART_REG_LCR);
	switch (lcr & 0x3) {
	case 0:
		bit_length = bit_length + 5;
	break;
	case 1:
		bit_length = bit_length + 6;
	break;
	case 2:
		bit_length = bit_length + 7;
	break;
	case 3:
		bit_length = bit_length + 8;
	break;
	}

	if (lcr & 0x4)
		bit_length += 1;

	if (lcr & 0x8) /* Parity bit */
		bit_length += 1;

	time_out = pdma_time_out_base * bit_length;
	time_out = (time_out / baud) + 1;
	time_out = time_out * 16;

	/* pdma max. time-out count is 65535 */
	while (time_out > 65535) {
		time_out = time_out / 2;
		time_out_prescaler++;
	}

	if (time_out == 0)
		time_out = 1;

	if (time_out_prescaler > 7)
		time_out_prescaler = 7;

	p->pdma_time_out_count = time_out;
	p->pdma_time_out_prescaler = time_out_prescaler;
}

static void ma35h0_prepare_RX_dma(struct uart_ma35h0_port *p)
{
	struct ma35h0_ip_rx_dma *pdma_rx = &(p->dma_rx);
	struct ma35h0_peripheral pcfg;
	int ret;
	int sg_loop, sg_size, sg_addr;

	serial_out(p, UART_REG_IER, (serial_in(p, UART_REG_IER) & ~RXPDMAEN));

	if (p->dest_mem_p.size == 0) {
		p->dest_mem_p.size = UART_RX_BUF_SIZE;
		p->dest_mem_p.vir_addr =
			(u64)(kmalloc(((UART_RX_BUF_SIZE + 64) * 2), GFP_KERNEL));
		p->pdma_rx_vir_addr1 = p->dest_mem_p.vir_addr;

		p->dest_mem_p.phy_addr =
			dma_map_single(pdma_rx->chan_rx->device->dev,
				(void *)p->dest_mem_p.vir_addr,
				((UART_RX_BUF_SIZE + 64) * 2), DMA_FROM_DEVICE);
		ret = dma_mapping_error(pdma_rx->chan_rx->device->dev,
				p->dest_mem_p.phy_addr);
		if (ret)
			dev_err(p->port.dev, "dest mapping error.\n");

		p->pdma_rx_phy_addr1 = p->dest_mem_p.phy_addr;

		p->pdma_rx_vir_addr2 =
			p->pdma_rx_vir_addr1 + UART_RX_BUF_SIZE + 64;
		p->pdma_rx_phy_addr2 =
			p->pdma_rx_phy_addr1 + UART_RX_BUF_SIZE + 64;
	}

	pdma_rx->slave_config.src_addr = (unsigned int)(p->port.iobase);
	pdma_rx->slave_config.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	pdma_rx->slave_config.src_maxburst = 1;
	pdma_rx->slave_config.direction = DMA_DEV_TO_MEM;
	pdma_rx->slave_config.device_fc = false;
	//pdma_rx->slave_config.slave_id = p->PDMA_UARTx_RX; //schung

	pcfg.reqsel = p->PDMA_UARTx_RX;
	pcfg.timeout_prescaler = p->pdma_time_out_prescaler;
	pcfg.timeout_counter = p->pdma_time_out_count;
	pdma_rx->slave_config.peripheral_config = &pcfg;
	pdma_rx->slave_config.peripheral_size = sizeof(pcfg);

	dmaengine_terminate_all(pdma_rx->chan_rx);
	dmaengine_slave_config(pdma_rx->chan_rx, &(pdma_rx->slave_config));

	sg_loop = 1;
	sg_size = p->dest_mem_p.size;
	sg_addr = p->dest_mem_p.phy_addr;
	sg_init_table(pdma_rx->sgrx, sg_loop);

	pdma_rx->sgrx[0].dma_address = sg_addr;
	pdma_rx->sgrx[0].dma_length = sg_size;

	pdma_rx->rxdesc = dmaengine_prep_slave_sg(pdma_rx->chan_rx,
						pdma_rx->sgrx, sg_loop, DMA_DEV_TO_MEM,
						DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!pdma_rx->rxdesc) {
		dev_err(p->port.dev, "pdma->rxdes==NULL.\n");
		return;
	}

	pdma_rx->rxdesc->callback = ma35h0_Rx_dma_callback;
	p->dma_Rx_done.callback_param = p;
	p->dma_Rx_done.timeout = 0;
	pdma_rx->rxdesc->callback_param = &(p->dma_Rx_done);
	dmaengine_submit(pdma_rx->rxdesc);
	pdma_rx->cookie = dmaengine_submit(pdma_rx->rxdesc);
	dma_async_issue_pending(pdma_rx->chan_rx);
}
#endif

static void ma35h0_prepare_TX_dma(struct uart_ma35h0_port *p)
{
	struct ma35h0_ip_tx_dma *pdma_tx = &(p->dma_tx);
	struct ma35h0_peripheral pcfg;
	int ret;

	struct circ_buf *xmit = &p->port.state->xmit;

	if (p->src_mem_p.size == 0) {
		p->src_mem_p.size = UART_XMIT_SIZE;
		p->src_mem_p.vir_addr = (u64)(kmalloc(p->src_mem_p.size,
								GFP_KERNEL));

		p->src_mem_p.phy_addr =
			dma_map_single(pdma_tx->chan_tx->device->dev,
				(void *)p->src_mem_p.vir_addr,
				p->src_mem_p.size, DMA_TO_DEVICE);
		ret = dma_mapping_error(pdma_tx->chan_tx->device->dev,
				p->src_mem_p.phy_addr);
		if (ret)
			dev_err(p->port.dev, "src mapping error.\n");
	}

	p->tx_dma_len = uart_circ_chars_pending(xmit);

	if (xmit->tail < xmit->head)
		memcpy((unsigned char *)p->src_mem_p.vir_addr,
				&xmit->buf[xmit->tail], p->tx_dma_len);
	else {
		size_t first = UART_XMIT_SIZE - xmit->tail;
		size_t second = xmit->head;

		memcpy((unsigned char *)p->src_mem_p.vir_addr,
				&xmit->buf[xmit->tail], first);
		if (second)
			memcpy((unsigned char *)p->src_mem_p.vir_addr+first,
				&xmit->buf[0], second);
	}

	dma_sync_single_for_device(pdma_tx->chan_tx->device->dev,
			p->src_mem_p.phy_addr, UART_XMIT_SIZE, DMA_TO_DEVICE);

	xmit->tail = (xmit->tail +  p->tx_dma_len) & (UART_XMIT_SIZE - 1);

	serial_out(p, UART_REG_IER, (serial_in(p, UART_REG_IER) & ~TXPDMAEN));
	pdma_tx->slave_config.dst_addr = (unsigned int)(p->port.iobase);
	pdma_tx->slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	pdma_tx->slave_config.dst_maxburst = 1;
	pdma_tx->slave_config.direction = DMA_MEM_TO_DEV;
	//pdma_tx->slave_config.slave_id = p->PDMA_UARTx_TX; //schung

	pcfg.reqsel = p->PDMA_UARTx_TX;
	pcfg.timeout_prescaler = 0;
	pcfg.timeout_counter = 0;
	pdma_tx->slave_config.peripheral_config = &pcfg;
	pdma_tx->slave_config.peripheral_size = sizeof(pcfg);

	dmaengine_terminate_all(pdma_tx->chan_tx);
	dmaengine_slave_config(pdma_tx->chan_tx, &(pdma_tx->slave_config));
	sg_init_table(pdma_tx->sgtx, 1);
	pdma_tx->sgtx[0].dma_address = p->src_mem_p.phy_addr;
	pdma_tx->sgtx[0].dma_length = p->tx_dma_len;

	pdma_tx->txdesc =
		dmaengine_prep_slave_sg(pdma_tx->chan_tx, pdma_tx->sgtx, 1,
				DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	if (!pdma_tx->txdesc) {
		dev_err(p->port.dev, "pdma->txdes==NULL.\n");
		return;
	}

	pdma_tx->txdesc->callback = ma35h0_Tx_dma_callback;
	p->dma_Tx_done.callback_param = p;
	pdma_tx->txdesc->callback_param = &(p->dma_Tx_done);
	dmaengine_submit(pdma_tx->txdesc);
	dma_async_issue_pending(pdma_tx->chan_tx);
}

static void rs485_start_rx(struct uart_ma35h0_port *port)
{

}

static void rs485_stop_rx(struct uart_ma35h0_port *port)
{

}

static inline void __stop_tx(struct uart_ma35h0_port *p)
{
	unsigned int ier;

	ier = serial_in(p, UART_REG_IER);
	if (ier & THRE_IEN)
		serial_out(p, UART_REG_IER, ier & ~THRE_IEN);

	if (p->rs485.flags & SER_RS485_ENABLED)
		rs485_start_rx(p);
}

static void ma35h0serial_stop_tx(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;

	__stop_tx(up);
}

static void transmit_chars(struct uart_ma35h0_port *up);

static void ma35h0serial_start_tx(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	unsigned int ier;
	struct circ_buf *xmit = &up->port.state->xmit;

	if (up->rs485.flags & SER_RS485_ENABLED)
		rs485_stop_rx(up);

	if (up->uart_pdma_enable_flag == 1) {
		if (up->Tx_pdma_busy_flag == 1)
			return;

		if (uart_circ_empty(xmit)) {
			__stop_tx(up);
			return;
		}

		up->Tx_pdma_busy_flag = 1;
		ma35h0_prepare_TX_dma(up);
		serial_out(up, UART_REG_IER,
				(serial_in(up, UART_REG_IER)|TXPDMAEN));
	} else {
		struct circ_buf *xmit = &up->port.state->xmit;

		ier = serial_in(up, UART_REG_IER);
		serial_out(up, UART_REG_IER, ier & ~THRE_IEN);
		if (uart_circ_chars_pending(xmit) <
				(16-((serial_in(up, UART_REG_FSR)>>16)&0x3F)))
			transmit_chars(up);
		serial_out(up, UART_REG_IER, ier | THRE_IEN);
	}
}

static void ma35h0serial_stop_rx(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;

	serial_out(up, UART_REG_IER, serial_in(up, UART_REG_IER) & ~RDA_IEN);
}

static void ma35h0serial_enable_ms(struct uart_port *port)
{

}

static void
receive_chars(struct uart_ma35h0_port *up)
{
	unsigned char ch;
	unsigned int fsr;
	unsigned int isr;
	unsigned int dcnt;
	char flag;
	unsigned long flags;

	isr = serial_in(up, UART_REG_ISR);
	fsr = serial_in(up, UART_REG_FSR);

	while (!(fsr & RX_EMPTY)) {
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(fsr & (BIF | FEF | PEF | RX_OVER_IF))) {
			if (fsr & BIF) {
				serial_out(up, UART_REG_FSR, BIF);
				up->port.icount.brk++;
				if (uart_handle_break(&up->port))
					continue;
			}

			if (fsr & FEF) {
				serial_out(up, UART_REG_FSR, FEF);
				up->port.icount.frame++;
			}

			if (fsr & PEF) {
				serial_out(up, UART_REG_FSR, PEF);
				up->port.icount.parity++;
			}

			if (fsr & RX_OVER_IF) {
				serial_out(up, UART_REG_FSR, RX_OVER_IF);
				up->port.icount.overrun++;
			}

			if (fsr & BIF)
				flag = TTY_BREAK;
			if (fsr & PEF)
				flag = TTY_PARITY;
			if (fsr & FEF)
				flag = TTY_FRAME;
		}

		ch = (unsigned char)serial_in(up, UART_REG_RBR);

		if (uart_handle_sysrq_char(&up->port, ch))
			continue;

		uart_insert_char(&up->port, fsr, RX_OVER_IF, ch, flag);
		up->max_count++;
		dcnt = (serial_in(up, UART_REG_FSR) >> 8) & 0x3f;
		if (up->max_count > 1023) {
			spin_lock_irqsave(&up->port.lock, flags);
			tty_flip_buffer_push(&up->port.state->port);
			spin_unlock_irqrestore(&up->port.lock, flags);
			up->max_count = 0;
			if ((isr & TOUT_IF) && (dcnt == 0))
				goto tout_end;
		}

		if ((isr & RDA_TOUT_IF) == RDA_IF) {
			if (dcnt == 1)
				return; /* have remaining data, don't reset max_count */
		}
		fsr = serial_in(up, UART_REG_FSR);
	}

	spin_lock_irqsave(&up->port.lock, flags);
	tty_flip_buffer_push(&up->port.state->port);
	spin_unlock_irqrestore(&up->port.lock, flags);
tout_end:
	up->max_count = 0;
}

#ifndef EN_UART_PDMA_RX
static void
receive_chars_no_pdma(struct uart_ma35h0_port *up)
{
	unsigned char ch;
	unsigned int fsr;
	unsigned int isr;
	unsigned int dcnt;
	char flag;
	unsigned long flags;

	isr = serial_in(up, UART_REG_ISR);
	fsr = serial_in(up, UART_REG_FSR);

	while (!(fsr & RX_EMPTY)) {
		flag = TTY_NORMAL;
		up->port.icount.rx++;

		if (unlikely(fsr & (BIF | FEF | PEF | RX_OVER_IF))) {
			if (fsr & BIF) {
				serial_out(up, UART_REG_FSR, BIF);
				up->port.icount.brk++;
				if (uart_handle_break(&up->port))
					continue;
			}

			if (fsr & FEF) {
				serial_out(up, UART_REG_FSR, FEF);
				up->port.icount.frame++;
			}
			if (fsr & PEF) {
				serial_out(up, UART_REG_FSR, PEF);
				up->port.icount.parity++;
			}

			if (fsr & RX_OVER_IF) {
				serial_out(up, UART_REG_FSR, RX_OVER_IF);
				up->port.icount.overrun++;
			}

			if (fsr & BIF)
				flag = TTY_BREAK;
			if (fsr & PEF)
				flag = TTY_PARITY;
			if (fsr & FEF)
				flag = TTY_FRAME;
		}
		ch = (unsigned char)serial_in(up, UART_REG_RBR);

		if (uart_handle_sysrq_char(&up->port, ch))
			continue;

		uart_insert_char(&up->port, fsr, RX_OVER_IF, ch, flag);
		up->max_count++;
		dcnt = (serial_in(up, UART_REG_FSR) >> 8) & 0x3f;
		if (up->max_count > 1023) {
			spin_lock_irqsave(&up->port.lock, flags);
			tty_flip_buffer_push(&up->port.state->port);
			spin_unlock_irqrestore(&up->port.lock, flags);
			up->max_count = 0;
			if ((isr & PTO_IF) && (dcnt == 0))
				goto tout_end1;
		}
		if ((isr & RDA_PTO_IF) == RDA_IF) {
			if (dcnt == 1)
				return; /* have remaining data, don't reset max_count */
		}

		fsr = serial_in(up, UART_REG_FSR);
	}

	spin_lock_irqsave(&up->port.lock, flags);
	tty_flip_buffer_push(&up->port.state->port);
	spin_unlock_irqrestore(&up->port.lock, flags);
tout_end1:
	up->max_count = 0;
}
#endif


static void transmit_chars(struct uart_ma35h0_port *up)
{
	struct circ_buf *xmit = &up->port.state->xmit;
	int count = 16 - ((serial_in(up, UART_REG_FSR)>>16)&0xF);

	if (serial_in(up, UART_REG_FSR) & TX_FULL)
		count = 0;

	if (up->port.x_char) {
		do {
		} while (serial_in(up, UART_REG_FSR) & TX_FULL);
		serial_out(up, UART_REG_THR, up->port.x_char);
		up->port.icount.tx++;
		up->port.x_char = 0;
		return;
	}

	if (uart_tx_stopped(&up->port)) {
		ma35h0serial_stop_tx(&up->port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		__stop_tx(up);
		return;
	}

	while (count > 0) {
		do {
		} while (serial_in(up, UART_REG_FSR) & TX_FULL);
		serial_out(up, UART_REG_THR, xmit->buf[xmit->tail]);
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		up->port.icount.tx++;
		count--;
		if (uart_circ_empty(xmit))
			break;
	}

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&up->port);

	if (uart_circ_empty(xmit))
		__stop_tx(up);

}

static unsigned int check_modem_status(struct uart_ma35h0_port *up)
{
	unsigned int status = 0;

	if (0)
		wake_up_interruptible(&up->port.state->port.delta_msr_wait);

	return status;
}

static irqreturn_t ma35h0serial_interrupt(int irq, void *dev_id)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)dev_id;
	unsigned int isr, fsr;
#ifdef EN_UART_PDMA_RX
	unsigned char ch[64];
	uint32_t i = 0;
	struct tty_port    *tty_port = &up->port.state->port;
	int read_fifo_count = 0;
#endif

	isr = serial_in(up, UART_REG_ISR);
	fsr = serial_in(up, UART_REG_FSR);

	if (up->uart_pdma_enable_flag == 1) {
#ifdef EN_UART_PDMA_RX
		if (isr & PTO_IF) {
			serial_out(up, UART_REG_IER,
					(serial_in(up, UART_REG_IER) & ~RXPDMAEN));
			i = 0;
			while (!(serial_in(up, UART_REG_FSR) & RX_EMPTY)) {
				ch[i] = (unsigned char)serial_in(up, UART_REG_RBR);
				i++;
			}

			serial_out(up, UART_REG_IER,
					(serial_in(up, UART_REG_IER) | RXPDMAEN));

			if (i > 0) {
				read_fifo_count = tty_insert_flip_string(tty_port, ch, i);

				if (read_fifo_count != i)
					dev_err(up->port.dev, "Rx dropping %d bytes\n",
							(i - read_fifo_count));

				up->port.icount.rx += read_fifo_count;
				tty_flip_buffer_push(tty_port);
			}
		}
#else
		if (isr & (RDA_IF | TOUT_IF | PTO_IF))
			receive_chars_no_pdma(up);

		check_modem_status(up);
#endif

		if (fsr & (BIF | FEF | PEF | RX_OVER_IF | HWBUFE_IF | TX_OVER_IF))
			serial_out(up, UART_REG_FSR,
					(BIF | FEF | PEF | RX_OVER_IF | TX_OVER_IF));
	} else {
		if (isr & (RDA_IF | TOUT_IF))
			receive_chars(up);

		check_modem_status(up);

		if (isr & THRE_INT)
			transmit_chars(up);

		if (fsr & (BIF | FEF | PEF | RX_OVER_IF | TX_OVER_IF))
			serial_out(up, UART_REG_FSR,
					(BIF | FEF | PEF | RX_OVER_IF | TX_OVER_IF));
	}

	if(isr & WAKEUP_IF)
		serial_out(up, UART_REG_WKSTS, 0x1F);

	return IRQ_HANDLED;
}

static unsigned int ma35h0serial_tx_empty(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	unsigned int fsr;

	fsr = serial_in(up, UART_REG_FSR);

	return (fsr & (TE_FLAG | TX_EMPTY)) ==
				(TE_FLAG | TX_EMPTY) ? TIOCSER_TEMT : 0;
}

static unsigned int ma35h0serial_get_mctrl(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	unsigned int status;
	unsigned int ret = 0;

	status = serial_in(up, UART_REG_MSR);

	if (!(status & 0x10))
		ret |= TIOCM_CTS;

	return ret;
}

static void ma35h0serial_set_mctrl(struct uart_port *port,
									unsigned int mctrl)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	unsigned int mcr = 0;
	unsigned int ier = 0;

	if (mctrl & TIOCM_RTS) {
		/* set RTS high level trigger */
		mcr = serial_in(up, UART_REG_MCR);
		mcr |= 0x200;
		mcr &= ~(0x2);
	}

	if (up->mcr & UART_MCR_AFE) {
		/* set RTS high level trigger */
		mcr = serial_in(up, UART_REG_MCR);
		mcr |= 0x200;
		mcr &= ~(0x2);

		/* enable CTS/RTS auto-flow control */
		serial_out(up, UART_REG_IER,
				(serial_in(up, UART_REG_IER) | (0x3000)));

		/* Set hardware flow control */
		up->port.flags |= UPF_HARD_FLOW;
	} else {
		/* disable CTS/RTS auto-flow control */
		ier = serial_in(up, UART_REG_IER);
		ier &= ~(0x3000);
		serial_out(up, UART_REG_IER, ier);

		/* un-set hardware flow control */
		up->port.flags &= ~UPF_HARD_FLOW;
	}

	/* set CTS high level trigger */
	serial_out(up, UART_REG_MSR,
			(serial_in(up, UART_REG_MSR) | (0x100)));

	serial_out(up, UART_REG_MCR, mcr);
}

static void ma35h0serial_break_ctl(struct uart_port *port,
									int break_state)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	unsigned long flags;
	unsigned int lcr;

	spin_lock_irqsave(&up->port.lock, flags);
	lcr = serial_in(up, UART_REG_LCR);
	if (break_state != 0)
		lcr |= BCB; /* set break */
	else
		lcr &= ~BCB; /* clr break */
	serial_out(up, UART_REG_LCR, lcr);
	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int ma35h0serial_startup(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	struct tty_struct *tty = port->state->port.tty;
	int retval;
#ifdef EN_UART_PDMA_RX
	struct ma35h0_ip_rx_dma *pdma_rx = &(up->dma_rx);
#endif
	struct ma35h0_ip_tx_dma *pdma_tx = &(up->dma_tx);
	unsigned long flags;

	dma_cap_mask_t mask;

	spin_lock_irqsave(&up->port.lock, flags);

	if (up->uart_pdma_enable_flag == 1) {
		dma_cap_zero(mask);
		dma_cap_set(DMA_SLAVE, mask);
		dma_cap_set(DMA_PRIVATE, mask);

#ifdef EN_UART_PDMA_RX
		pdma_rx->chan_rx = dma_request_slave_channel(up->port.dev, "rx");
		if (!pdma_rx->chan_rx) {
			dev_err(up->port.dev, "RX DMA channel request error.\n");
			return -1;
		}
		pdma_rx->chan_rx->private = (void *)1;
#endif

		pdma_tx->chan_tx = dma_request_slave_channel(up->port.dev, "tx");
		if (!pdma_tx->chan_tx) {
			dev_err(up->port.dev, "TX DMA channel request error.\n");
			return -1;
		}
		pdma_tx->chan_tx->private = (void *)1;
	}

	/* Reset FIFO */
	serial_out(up, UART_REG_FCR, TFR | RFR /* | RX_DIS */);

	/* Clear pending interrupts */
	serial_out(up, UART_REG_ISR, 0xFFFFFFFF);

	spin_unlock_irqrestore(&up->port.lock, flags);

	retval = request_irq(port->irq, ma35h0serial_interrupt, 0,
						tty ? tty->name : "ma35h0_serial", port);

	if (retval) {
		dev_err(up->port.dev, "request irq failed.\n");
		return retval;
	}

	spin_lock_irqsave(&up->port.lock, flags);

	/* Now, initialize the UART */
	/* FIFO trigger level 4 byte */
	/* RTS trigger level 8 bytes */
	serial_out(up, UART_REG_FCR, serial_in(up,
			UART_REG_FCR) | 0x10 | 0x20000);

	serial_out(up, UART_REG_LCR, 0x7); /* 8 bit */
	serial_out(up, UART_REG_TOR, 0x40);

	if (up->uart_pdma_enable_flag == 1)
#ifdef EN_UART_PDMA_RX
		serial_out(up, UART_REG_IER,
				RTO_IEN | RLS_IEN | BUFERR_IEN | TIME_OUT_EN);
#else
		serial_out(up, UART_REG_IER,
				RTO_IEN | RDA_IEN | RLS_IEN | BUFERR_IEN | TIME_OUT_EN);

#endif
	else
		serial_out(up, UART_REG_IER,
				RTO_IEN | RDA_IEN | TIME_OUT_EN | BUFERR_IEN);

#ifdef EN_UART_PDMA_RX
	if (up->uart_pdma_enable_flag == 1)
		up->baud_rate = 0;
#endif

	if(up->wakeup_enable != 0) {
		/* Clear wakeup status */
		serial_out(up, UART_REG_WKSTS, 0x1f);
		/* Set wakeup mode */
		serial_out(up, UART_REG_WKCTL, up->wakeup_enable);

		serial_out(up, UART_REG_IER,
				serial_in(up, UART_REG_IER) | WAKEUP_IEN);

	}

	spin_unlock_irqrestore(&up->port.lock, flags);

	return 0;
}

static void ma35h0serial_shutdown(struct uart_port *port)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
#ifdef EN_UART_PDMA_RX
	struct ma35h0_ip_rx_dma *pdma_rx = &(up->dma_rx);
#endif
	struct ma35h0_ip_tx_dma *pdma_tx = &(up->dma_tx);

	if (up->uart_pdma_enable_flag == 1) {
#ifdef EN_UART_PDMA_RX
		dma_release_channel(pdma_rx->chan_rx);
#endif
		dma_release_channel(pdma_tx->chan_tx);

#ifdef EN_UART_PDMA_RX
		if (up->dest_mem_p.size != 0)
			kfree((void *)up->dest_mem_p.vir_addr);
#endif

		if (up->src_mem_p.size != 0)
			kfree((void *)up->src_mem_p.vir_addr);

		up->Tx_pdma_busy_flag = 0;
#ifdef EN_UART_PDMA_RX
		up->dest_mem_p.size = 0;
#endif
		up->src_mem_p.size = 0;
	}

	free_irq(port->irq, port);

	/* Disable interrupts from this port */
	serial_out(up, UART_REG_IER, 0);
}

static unsigned int ma35h0serial_get_divisor(struct uart_port *port,
											unsigned int baud)
{
	unsigned int quot;

	quot = (port->uartclk / baud) - 2;

	return quot;
}

static void
ma35h0serial_set_termios(struct uart_port *port,
						struct ktermios *termios, const struct ktermios *old)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;
	unsigned int lcr = 0;
	unsigned long flags;
	unsigned int baud, quot;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		lcr = 0;
		break;
	case CS6:
		lcr |= 1;
		break;
	case CS7:
		lcr |= 2;
		break;
	default:
	case CS8:
		lcr |= 3;
		break;
	}

	if (termios->c_cflag & CSTOPB)
		lcr |= NSB;
	if (termios->c_cflag & PARENB)
		lcr |= PBE;
	if (!(termios->c_cflag & PARODD))
		lcr |= EPE;
	if (termios->c_cflag & CMSPAR)
		lcr |= SPE;

	baud = uart_get_baud_rate(port, termios, old, port->uartclk / 0xffff,
								port->uartclk / 11);

	quot = ma35h0serial_get_divisor(port, baud);

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	spin_lock_irqsave(&up->port.lock, flags);

	up->port.read_status_mask = RX_OVER_IF;
	if (termios->c_iflag & INPCK)
		up->port.read_status_mask |= FEF | PEF;
	if (termios->c_iflag & (BRKINT | PARMRK))
		up->port.read_status_mask |= BIF;

	/*
	 * Characteres to ignore
	 */
	up->port.ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		up->port.ignore_status_mask |= FEF | PEF;
	if (termios->c_iflag & IGNBRK) {
		up->port.ignore_status_mask |= BIF;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (termios->c_iflag & IGNPAR)
			up->port.ignore_status_mask |= RX_OVER_IF;
	}

	if (termios->c_cflag & CRTSCTS)
		up->mcr |= UART_MCR_AFE;
	else
		up->mcr &= ~UART_MCR_AFE;

	ma35h0serial_set_mctrl(&up->port, up->port.mctrl);

	serial_out(up, UART_REG_BAUD, quot | 0x30000000);

	serial_out(up, UART_REG_LCR, lcr);

	spin_unlock_irqrestore(&up->port.lock, flags);

#ifdef EN_UART_PDMA_RX
	if (up->uart_pdma_enable_flag == 1) {
		if (up->baud_rate != baud) {
			up->baud_rate = baud;

			ma35h0_uart_cal_pdma_time_out(up, baud);

			ma35h0_prepare_RX_dma(up);

			spin_lock_irqsave(&up->port.lock, flags);

			/* trigger pdma */
			serial_out(up, UART_REG_IER,
					(serial_in(up, UART_REG_IER)|RXPDMAEN));

			spin_unlock_irqrestore(&up->port.lock, flags);
		}
	}
#endif
}

static void
ma35h0serial_pm(struct uart_port *port, unsigned int state,
				unsigned int oldstate)
{
	struct uart_ma35h0_port *p = (struct uart_ma35h0_port *)port;

	if (p->pm)
		p->pm(port, state, oldstate);
}

static void ma35h0serial_release_port(struct uart_port *port)
{
	struct uart_ma35h0_port *p = (struct uart_ma35h0_port *)port;
#ifdef EN_UART_PDMA_RX
	struct ma35h0_ip_rx_dma *pdma_rx = &(p->dma_rx);
#endif
	struct ma35h0_ip_tx_dma *pdma_tx = &(p->dma_tx);

	if (p->uart_pdma_enable_flag == 1) {
#ifdef EN_UART_PDMA_RX
		dma_unmap_single(pdma_rx->chan_rx->device->dev,
			p->dest_mem_p.phy_addr, UART_RX_BUF_SIZE, DMA_FROM_DEVICE);
#endif
		dma_unmap_single(pdma_tx->chan_tx->device->dev,
			p->src_mem_p.phy_addr, p->src_mem_p.size, DMA_TO_DEVICE);
	}

	iounmap(port->membase);
	port->membase = NULL;
}

static int ma35h0serial_request_port(struct uart_port *port)
{
	return 0;
}

static void ma35h0serial_config_port(struct uart_port *port, int flags)
{
	int ret;

	/*
	 * Find the region that we can probe for.  This in turn
	 * tells us whether we can probe for the type of port.
	 */
	ret = ma35h0serial_request_port(port);
	if (ret < 0)
		return;
	port->type = PORT_MA35;
}

static int ma35h0serial_verify_port(struct uart_port *port,
									struct serial_struct *ser)
{
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_MA35)
		return -EINVAL;
	return 0;
}

static const char *ma35h0serial_type(struct uart_port *port)
{
	return (port->type == PORT_MA35) ? "MA35H0" : NULL;
}

/* Enable or disable the rs485 support */
static int ma35h0serial_config_rs485(struct uart_port *port, struct ktermios *termios,
								struct serial_rs485 *rs485conf)
{
	struct uart_ma35h0_port *p = to_ma35h0_uart_port(port);

	p->rs485 = *rs485conf;
	port->rs485 = *rs485conf;

	if (p->rs485.delay_rts_before_send >= 1000)
		p->rs485.delay_rts_before_send = 1000;

	serial_out(p, UART_FUN_SEL,
			(serial_in(p, UART_FUN_SEL) & ~FUN_SEL_Msk));

	if (rs485conf->flags & SER_RS485_ENABLED) {
		serial_out(p, UART_FUN_SEL,
				(serial_in(p, UART_FUN_SEL) | FUN_SEL_RS485));

		if (rs485conf->flags & SER_RS485_RTS_ON_SEND)
			serial_out(p, UART_REG_MCR,
				(serial_in(p, UART_REG_MCR) & ~0x200));
		else
			serial_out(p, UART_REG_MCR,
				(serial_in(p, UART_REG_MCR) | 0x200));

		/* set auto direction mode */
		serial_out(p, UART_REG_ALT_CSR,
				(serial_in(p, UART_REG_ALT_CSR) | (1 << 10)));

		/* Reset FIFO */
		serial_out(p, UART_REG_FCR, serial_in(p, UART_REG_FCR) | TFR | RFR);
	}

	return 0;
}

static int ma35h0serial_ioctl(struct uart_port *port,
							unsigned int cmd, unsigned long arg)
{
	switch (cmd) {

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static const struct uart_ops ma35h0serial_ops = {
	.tx_empty     = ma35h0serial_tx_empty,
	.set_mctrl    = ma35h0serial_set_mctrl,
	.get_mctrl    = ma35h0serial_get_mctrl,
	.stop_tx      = ma35h0serial_stop_tx,
	.start_tx     = ma35h0serial_start_tx,
	.stop_rx      = ma35h0serial_stop_rx,
	.enable_ms    = ma35h0serial_enable_ms,
	.break_ctl    = ma35h0serial_break_ctl,
	.startup      = ma35h0serial_startup,
	.shutdown     = ma35h0serial_shutdown,
	.set_termios  = ma35h0serial_set_termios,
	.pm           = ma35h0serial_pm,
	.type         = ma35h0serial_type,
	.release_port = ma35h0serial_release_port,
	.request_port = ma35h0serial_request_port,
	.config_port  = ma35h0serial_config_port,
	.verify_port  = ma35h0serial_verify_port,
	.ioctl        = ma35h0serial_ioctl,
};

static const struct of_device_id ma35h0_serial_of_match[] = {
	{ .compatible = "nuvoton,ma35h0-uart" },
	{},
};
MODULE_DEVICE_TABLE(of, ma35h0_serial_of_match);

static void __init ma35h0serial_init_ports(void)
{
	struct device_node *np;
	struct device_node *clk_node;
	unsigned char __iomem *clk_base;
	struct uart_ma35h0_port *p = &ma35h0serial_ports[0];
	u32   val32[4];

	clk_node = of_find_compatible_node(NULL, NULL, "nuvoton,ma35h0-clk");
	clk_base = of_iomap(clk_node, 0);

	__raw_writel(__raw_readl(clk_base+0xC) | (0x3fff << 12),
					clk_base+0xC);
	__raw_writel(__raw_readl(clk_base+0x20) & ~(3 << 16),
					clk_base+0x20);

	for_each_matching_node(np, ma35h0_serial_of_match) {

		if (!of_find_property(np, "ma35h0-console", NULL))
			continue;

		if (of_property_read_u32_array(np, "reg", val32, 4) != 0)
			return;

		p->port.iobase = val32[1];
		p->port.membase = ioremap(p->port.iobase, 0x10000);
		p->port.ops = &ma35h0serial_ops;

		if (of_property_read_u32_array(np, "port-number", val32, 1) != 0)
			return;

		p->port.line = val32[0];
		p->port.uartclk = 24000000;

		break;
	}
}

#ifdef CONFIG_SERIAL_MA35H0_CONSOLE
static void ma35h0serial_console_putchar(struct uart_port *port, unsigned char ch)
{
	struct uart_ma35h0_port *up = (struct uart_ma35h0_port *)port;

	do {
	} while (!(serial_in(up, UART_REG_FSR) & TX_EMPTY));
	serial_out(up, UART_REG_THR, ch);
}

/*
 *  Print a string to the serial port trying not to disturb
 *  any possible real use of the port...
 *
 *  The console_lock must be held when we get here.
 */
static void ma35h0serial_console_write(struct console *co, const char *s,
										unsigned int count)
{
	struct uart_ma35h0_port *up = &ma35h0serial_ports[co->index];
	unsigned long flags;
	unsigned int ier;

	spin_lock_irqsave(&up->port.lock, flags);

	/*
	 *  First save the IER then disable the interrupts
	 */
	ier = serial_in(up, UART_REG_IER);
	serial_out(up, UART_REG_IER, 0);

	uart_console_write(&up->port, s, count, ma35h0serial_console_putchar);

	/*
	 *  Finally, wait for transmitter to become empty
	 *  and restore the IER
	 */
	do {
	} while (!(serial_in(up, UART_REG_FSR) & TX_EMPTY));
	serial_out(up, UART_REG_IER, ier);

	spin_unlock_irqrestore(&up->port.lock, flags);
}

static int __init ma35h0serial_console_setup(struct console *co,
											char *options)
{
	struct uart_port *port;
	int baud = 115200;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (co->index >= UART_NR)
		co->index = 0;
	port = &ma35h0serial_ports[co->index].port;

	if (!port->iobase && !port->membase)
		return -ENODEV;

	return uart_set_options(port, co, baud, parity, bits, flow);
}


static struct console ma35h0serial_console = {
	.name    = "ttyS",
	.write   = ma35h0serial_console_write,
	.device  = uart_console_device,
	.setup   = ma35h0serial_console_setup,
	.flags   = CON_PRINTBUFFER | CON_ENABLED,
	.index   = -1,
	.data    = &ma35h0serial_reg,
};

static int __init ma35h0serial_console_init(void)
{
	ma35h0serial_init_ports();
	register_console(&ma35h0serial_console);

	return 0;
}
console_initcall(ma35h0serial_console_init);

#define MA35H0SERIAL_CONSOLE    (&ma35h0serial_console)
#else
#define MA35H0SERIAL_CONSOLE    NULL
#endif

static struct uart_driver ma35h0serial_reg = {
	.owner        = THIS_MODULE,
	.driver_name  = "serial",
	.dev_name     = "ttyS",
	.major        = TTY_MAJOR,
	.minor        = 64,
	.cons         = MA35H0SERIAL_CONSOLE,
	.nr           = UART_NR,
};


/**
 *
 *  Suspend one serial port.
 */
void ma35h0serial_suspend_port(int line)
{
	uart_suspend_port(&ma35h0serial_reg, &ma35h0serial_ports[line].port);
}
EXPORT_SYMBOL(ma35h0serial_suspend_port);

/**
 *
 *  Resume one serial port.
 */
void ma35h0serial_resume_port(int line)
{
	struct uart_ma35h0_port *up = &ma35h0serial_ports[line];

	uart_resume_port(&ma35h0serial_reg, &up->port);
}
EXPORT_SYMBOL(ma35h0serial_resume_port);

static int  get_uart_port_number(struct platform_device *pdev)
{
	u32   val32[2];

	if (of_property_read_u32_array(pdev->dev.of_node, "port-number",
													val32, 1) != 0) {
		dev_err(&pdev->dev, "can not get port-number!\n");
		return -EINVAL;
	}

	return val32[0];
}

/*
 * Register a set of serial devices attached to a platform device.  The
 * list is terminated with a zero flags entry, which means we expect
 * all entries to have at least UPF_BOOT_AUTOCONF set.
 */
static int ma35h0serial_probe(struct platform_device *pdev)
{
	struct resource *res_mem;
	struct uart_ma35h0_port *up;
	int ret, i;
	u32   val32[3];
	struct clk *clk;
	int err;

	i = get_uart_port_number(pdev);
	if (i < 0)
		return -EINVAL;

	up = &ma35h0serial_ports[i];

	up->max_count = 0;

	up->port.line = i;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem)
		return -ENODEV;

	up->port.iobase = res_mem->start;
	up->port.membase = ioremap(up->port.iobase, 0x10000);
	up->port.ops = &ma35h0serial_ops;

	spin_lock_init(&up->port.lock);

	clk = of_clk_get(pdev->dev.of_node, 0);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		dev_err(&pdev->dev, "failed to get core clk: %d\n", err);
		return -ENOENT;
	}
	err = clk_prepare_enable(clk);
	if (err)
		return -ENOENT;

	if (up->port.line != 0)
		up->port.uartclk = clk_get_rate(clk);

	if (of_property_read_u32_array(pdev->dev.of_node, "pdma-enable",
													val32, 1) != 0) {
		dev_err(&pdev->dev, "can not get pdma-enable flag !\n");
		return -EINVAL;
	}

	if (val32[0] == 1)
		set_pdma_flag(up, i);

	up->wakeup_enable = 0;

	if (of_property_read_u32_array(pdev->dev.of_node, 
						"wakeup-enable", val32, 3) != 0) {
		up->wakeup_enable = 0;
	}
	else {
		if (val32[0] != 0)
			up->wakeup_enable = 1;

		if (val32[1] != 0)
			up->wakeup_enable |= (0x1 << 2);

		if (val32[2] != 0)
			up->wakeup_enable |= (0x1 << 4);

		if(up->wakeup_enable != 0)
			device_init_wakeup(&pdev->dev, true);
	}

	if (of_property_read_u32_array(pdev->dev.of_node, 
						"fifo-threshold", val32, 1) != 0) {
		up->fifo_threshold = 0;
	}
	else {
		if(val32[0] < 4)
			up->fifo_threshold = 0;
		else if(val32[0] < 8)
			up->fifo_threshold = (0x1 << 4);
		else if(val32[0] < 14)
			up->fifo_threshold = (0x2 << 4);
		else if(val32[0] < 30)
			up->fifo_threshold = (0x3 << 4);
		else
			up->fifo_threshold = (0x4 << 4);
	}

	up->port.irq = platform_get_irq(pdev, 0);
	up->port.dev = &pdev->dev;
	up->port.flags = UPF_BOOT_AUTOCONF;

	up->port.rs485_config = ma35h0serial_config_rs485;

	ret = uart_add_one_port(&ma35h0serial_reg, &up->port);

	platform_set_drvdata(pdev, up);

	return 0;
}

/*
 * Remove serial ports registered against a platform device.
 */
static int ma35h0serial_remove(struct platform_device *dev)
{
	int i;
	struct uart_port *port = platform_get_drvdata(dev);
	struct uart_ma35h0_port *up;

	i = get_uart_port_number(dev);
	if (i < 0)
		return i;

	up = &ma35h0serial_ports[i];

	if(up->wakeup_enable == 1)
		device_init_wakeup(&dev->dev, false);

	free_irq(port->irq, port);

	if (up->port.dev == &dev->dev)
			uart_remove_one_port(&ma35h0serial_reg, &up->port);

	return 0;
}

static int ma35h0serial_suspend(struct platform_device *dev,
								pm_message_t state)
{
	int i;
	struct uart_ma35h0_port *up;

	i = get_uart_port_number(dev);
	if (i < 0)
		return i;

	up = &ma35h0serial_ports[i];

	if (device_may_wakeup(&dev->dev)) {

		/* FIFO trigger from device tree */
		/* RTS trigger level 8 bytes */
		serial_out(up, UART_REG_FCR, serial_in(up,
					UART_REG_FCR) | up->fifo_threshold | 0x20000);

		enable_irq_wake(up->port.irq);
	}

	if (i == 0) {
		up->console_baud_rate = serial_in(up, UART_REG_BAUD);
		up->console_line = serial_in(up, UART_REG_LCR);
		up->console_int = serial_in(up, UART_REG_IER);
	}

	return 0;
}

static int ma35h0serial_resume(struct platform_device *dev)
{
	int i;
	struct uart_ma35h0_port *up;

	i = get_uart_port_number(dev);
	if (i < 0)
		return i;

	up = &ma35h0serial_ports[i];

	if (device_may_wakeup(&dev->dev)) {
		disable_irq_wake(up->port.irq);

		/* FIFO trigger level 4 byte */
		/* RTS trigger level 8 bytes */
		serial_out(up, UART_REG_FCR, serial_in(up,
					UART_REG_FCR) | 0x10 | 0x20000);

	}

	if (i == 0) {
		serial_out(up, UART_REG_BAUD, up->console_baud_rate);
		serial_out(up, UART_REG_LCR, up->console_line);
		serial_out(up, UART_REG_IER, up->console_int);
	}

	return 0;
}

static struct platform_driver ma35h0serial_driver = {
	.probe      = ma35h0serial_probe,
	.remove     = ma35h0serial_remove,
	.suspend    = ma35h0serial_suspend,
	.resume     = ma35h0serial_resume,
	.driver     = {
		.name   = "ma35h0-uart",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(ma35h0_serial_of_match),
	},
};

static int __init ma35h0serial_init(void)
{
	int ret;

	ret = uart_register_driver(&ma35h0serial_reg);
	if (ret)
		return ret;

	ret = platform_driver_register(&ma35h0serial_driver);
	if (ret)
		uart_unregister_driver(&ma35h0serial_reg);

	return ret;
}

static void __exit ma35h0serial_exit(void)
{
	platform_driver_unregister(&ma35h0serial_driver);
	uart_unregister_driver(&ma35h0serial_reg);
}

module_init(ma35h0serial_init);
module_exit(ma35h0serial_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MA35H0 serial driver");

MODULE_ALIAS_CHARDEV_MAJOR(TTY_MAJOR);
