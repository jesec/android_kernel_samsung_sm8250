/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __QRTR_H_
#define __QRTR_H_

#include <linux/types.h>

//Yuanfei.Liu@PSW.NW.DATA.2120730, 2019/07/11
//Add for: print qrtr debug msg and fix QMI wakeup statistics for QCOM platforms using glink.
#ifdef VENDOR_EDIT
#define MODEM_WAKEUP_SRC_NUM 3
#define MODEM_QMI_WS_INDEX 2
#define QRTR_FIRST_HEAD "QrtrFirst "
#define QRTR_FIRST_HEAD_COUNT 10
extern int modem_wakeup_src_count[MODEM_WAKEUP_SRC_NUM];
extern int qrtr_first_msg;
extern char qrtr_first_msg_details[256];
extern char *sub_qrtr_first_msg_details;
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
//Yuanfei.Liu@PSW.NW.DATA.2579544, 2019/11/20
//Add for: ipcc_x print
extern int ipcc_first_msg;
#endif

#ifdef VENDOR_EDIT
//weizhong.wu@bsp.power.basic 20191129 add for ipcc_1  count cal
extern u64 wakeup_source_count_cdsp;
extern u64 wakeup_source_count_adsp;
extern u64 wakeup_source_count_modem;
#endif



struct sk_buff;

/* endpoint node id auto assignment */
#define QRTR_EP_NID_AUTO (-1)
#define QRTR_EP_NET_ID_AUTO (1)

#define QRTR_DEL_PROC_MAGIC	0xe111

/**
 * struct qrtr_endpoint - endpoint handle
 * @xmit: Callback for outgoing packets
 *
 * The socket buffer passed to the xmit function becomes owned by the endpoint
 * driver.  As such, when the driver is done with the buffer, it should
 * call kfree_skb() on failure, or consume_skb() on success.
 */
struct qrtr_endpoint {
	int (*xmit)(struct qrtr_endpoint *ep, struct sk_buff *skb);
	/* private: not for endpoint use */
	struct qrtr_node *node;
};

int qrtr_endpoint_register(struct qrtr_endpoint *ep, unsigned int net_id,
			   bool rt);

void qrtr_endpoint_unregister(struct qrtr_endpoint *ep);

int qrtr_endpoint_post(struct qrtr_endpoint *ep, const void *data, size_t len);

int qrtr_peek_pkt_size(const void *data);
#endif
