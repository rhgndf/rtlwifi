// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../rtl8192s/reg.h"
#include "../rtl8192s/def.h"
#include "../rtl8192s/fw_common.h"
#include "fw.h"

void rtl92se_fw_set_rqpn(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_dword(rtlpriv, RQPN, 0xffffffff);
	rtl_write_dword(rtlpriv, RQPN + 4, 0xffffffff);
	rtl_write_byte(rtlpriv, RQPN + 8, 0xff);
	rtl_write_byte(rtlpriv, RQPN + 0xB, 0x80);
}
bool rtl92se_cmd_send_packet(struct ieee80211_hw *hw,
		struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	struct rtl_tcb_desc *tcb_desc = (struct rtl_tcb_desc *)skb->cb;
	unsigned long flags;
	u8 idx = 0;

	tcb_desc->queue_index = TXCMD_QUEUE;
	ring = &rtlpci->tx_ring[TXCMD_QUEUE];

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	idx = (ring->idx + skb_queue_len(&ring->queue)) % ring->entries;
	pdesc = &ring->desc[idx];
	rtlpriv->cfg->ops->fill_tx_cmddesc(hw, (u8 *)pdesc, skb);
	__skb_queue_tail(&ring->queue, skb);

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	return true;
}

void rtl92se_fw_download_poll(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtl_write_byte(rtlpriv, TP_POLL, TPPOLL_CQ);
}
