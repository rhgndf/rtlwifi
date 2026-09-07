/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_92S_TRX_COMMON_H__
#define __REALTEK_92S_TRX_COMMON_H__

static inline bool rtl92s_rx_is_cmd(const __le32 *pdesc)
{
	return (le32_to_cpu(pdesc[1]) & 0x1ff) == 0x1ff;
}

u8 rtl92s_map_hwqueue_to_fwqueue(struct sk_buff *skb, u8 skb_queue);
bool rtl92s_rx_query_desc(struct ieee80211_hw *hw, struct rtl_stats *stats,
			  struct ieee80211_rx_status *rx_status, u8 *pdesc,
			  struct sk_buff *skb);

#endif
