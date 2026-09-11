// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../base.h"
#include "../rtl8192s/reg.h"
#include "../rtl8192s/def.h"
#include "../rtl8192s/phy_common.h"
#include "../rtl8192s/trx_common.h"
#include "fw.h"
#include "trx.h"
#include "led.h"

void rtl92se_tx_fill_desc(struct ieee80211_hw *hw,
		struct ieee80211_hdr *hdr, u8 *pdesc8,
		u8 *pbd_desc_tx, struct ieee80211_tx_info *info,
		struct ieee80211_sta *sta,
		struct sk_buff *skb,
		u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	__le32 *pdesc = (__le32 *)pdesc8;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	u8 reserved_macid = 0;
	u8 fw_qsel = rtl92s_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool firstseg = (!(hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)));
	bool lastseg = (!(hdr->frame_control &
			cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)));
	dma_addr_t mapping = dma_map_single(&rtlpci->pdev->dev, skb->data,
					    skb->len, DMA_TO_DEVICE);
	u8 bw_40 = 0;

	if (dma_mapping_error(&rtlpci->pdev->dev, mapping)) {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"DMA mapping error\n");
		return;
	}
	if (mac->opmode == NL80211_IFTYPE_STATION) {
		bw_40 = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (sta)
			bw_40 = sta->deflink.bandwidth >= IEEE80211_STA_RX_BW_40;
	}

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);

	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_DESC_SIZE_RTL8192S);

	if (ieee80211_is_nullfunc(fc) || ieee80211_is_ctl(fc)) {
		firstseg = true;
		lastseg = true;
	}

	if (firstseg) {
		if (rtlpriv->dm.useramask) {
			/* set txdesc macId */
			if (ptcb_desc->mac_id < 32) {
				set_tx_desc_macid(pdesc, ptcb_desc->mac_id);
				reserved_macid |= ptcb_desc->mac_id;
			}
		}
		set_tx_desc_rsvd_macid(pdesc, reserved_macid);

		set_tx_desc_txht(pdesc, ((ptcb_desc->hw_rate >=
				 DESC_RATEMCS0) ? 1 : 0));

		if (rtlhal->version == VERSION_8192S_ACUT) {
			if (ptcb_desc->hw_rate == DESC_RATE1M ||
			    ptcb_desc->hw_rate  == DESC_RATE2M ||
			    ptcb_desc->hw_rate == DESC_RATE5_5M ||
			    ptcb_desc->hw_rate == DESC_RATE11M) {
				ptcb_desc->hw_rate = DESC_RATE12M;
			}
		}

		set_tx_desc_tx_rate(pdesc, ptcb_desc->hw_rate);

		if (ptcb_desc->use_shortgi || ptcb_desc->use_shortpreamble)
			set_tx_desc_tx_short(pdesc, 0);

		/* Aggregation related */
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			set_tx_desc_agg_enable(pdesc, 1);

		/* For AMPDU, we must insert SSN into TX_DESC */
		set_tx_desc_seq(pdesc, seq_number);

		/* Protection mode related */
		/* For 92S, if RTS/CTS are set, HW will execute RTS. */
		/* We choose only one protection mode to execute */
		set_tx_desc_rts_enable(pdesc, ((ptcb_desc->rts_enable &&
						!ptcb_desc->cts_enable) ?
					       1 : 0));
		set_tx_desc_cts_enable(pdesc, ((ptcb_desc->cts_enable) ?
				       1 : 0));
		set_tx_desc_rts_stbc(pdesc, ((ptcb_desc->rts_stbc) ? 1 : 0));

		set_tx_desc_rts_rate(pdesc, ptcb_desc->rts_rate);
		set_tx_desc_rts_bandwidth(pdesc, 0);
		set_tx_desc_rts_sub_carrier(pdesc, ptcb_desc->rts_sc);
		set_tx_desc_rts_short(pdesc, ((ptcb_desc->rts_rate <=
		       DESC_RATE54M) ?
		       (ptcb_desc->rts_use_shortpreamble ? 1 : 0)
		       : (ptcb_desc->rts_use_shortgi ? 1 : 0)));


		/* Set Bandwidth and sub-channel settings. */
		if (bw_40) {
			if (ptcb_desc->packet_bw) {
				set_tx_desc_tx_bandwidth(pdesc, 1);
				/* use duplicated mode */
				set_tx_desc_tx_sub_carrier(pdesc, 0);
			} else {
				set_tx_desc_tx_bandwidth(pdesc, 0);
				set_tx_desc_tx_sub_carrier(pdesc,
						   mac->cur_40_prime_sc);
			}
		} else {
			set_tx_desc_tx_bandwidth(pdesc, 0);
			set_tx_desc_tx_sub_carrier(pdesc, 0);
		}

		/* 3 Fill necessary field in First Descriptor */
		/*DWORD 0*/
		set_tx_desc_linip(pdesc, 0);
		set_tx_desc_offset(pdesc, 32);
		set_tx_desc_pkt_size(pdesc, (u16)skb->len);

		/*DWORD 1*/
		set_tx_desc_ra_brsr_id(pdesc, ptcb_desc->ratr_index);

		/* Fill security related */
		if (info->control.hw_key) {
			struct ieee80211_key_conf *keyconf;

			keyconf = info->control.hw_key;
			switch (keyconf->cipher) {
			case WLAN_CIPHER_SUITE_WEP40:
			case WLAN_CIPHER_SUITE_WEP104:
				set_tx_desc_sec_type(pdesc, 0x1);
				break;
			case WLAN_CIPHER_SUITE_TKIP:
				set_tx_desc_sec_type(pdesc, 0x2);
				break;
			case WLAN_CIPHER_SUITE_CCMP:
				set_tx_desc_sec_type(pdesc, 0x3);
				break;
			default:
				set_tx_desc_sec_type(pdesc, 0x0);
				break;

			}
		}

		/* Set Packet ID */
		set_tx_desc_packet_id(pdesc, 0);

		/* We will assign magement queue to BK. */
		set_tx_desc_queue_sel(pdesc, fw_qsel);

		/* Alwasy enable all rate fallback range */
		set_tx_desc_data_rate_fb_limit(pdesc, 0x1F);

		/* Fix: I don't kown why hw use 6.5M to tx when set it */
		set_tx_desc_user_rate(pdesc,
				      ptcb_desc->use_driver_rate ? 1 : 0);

		/* Set NON_QOS bit. */
		if (!ieee80211_is_data_qos(fc))
			set_tx_desc_non_qos(pdesc, 1);

	}

	/* Fill fields that are required to be initialized
	 * in all of the descriptors */
	/*DWORD 0 */
	set_tx_desc_first_seg(pdesc, (firstseg ? 1 : 0));
	set_tx_desc_last_seg(pdesc, (lastseg ? 1 : 0));

	/* DWORD 7 */
	set_tx_desc_tx_buffer_size(pdesc, (u16)skb->len);

	/* DOWRD 8 */
	set_tx_desc_tx_buffer_address(pdesc, mapping);

	rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE, "\n");
}

void rtl92se_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc8,
			     struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_tcb_desc *tcb_desc = (struct rtl_tcb_desc *)(skb->cb);
	__le32 *pdesc = (__le32 *)pdesc8;

	dma_addr_t mapping = dma_map_single(&rtlpci->pdev->dev, skb->data,
					    skb->len, DMA_TO_DEVICE);

	if (dma_mapping_error(&rtlpci->pdev->dev, mapping)) {
		rtl_dbg(rtlpriv, COMP_SEND, DBG_TRACE,
			"DMA mapping error\n");
		return;
	}
	/* Clear all status	*/
	CLEAR_PCI_TX_DESC_CONTENT(pdesc, TX_CMDDESC_SIZE_RTL8192S);

	/* This bit indicate this packet is used for FW download. */
	if (tcb_desc->cmd_or_init == DESC_PACKET_TYPE_INIT) {
		/* For firmware download we only need to set LINIP */
		set_tx_desc_linip(pdesc, tcb_desc->last_inipkt);

		/* 92SE must set as 1 for firmware download HW DMA error */
		set_tx_desc_first_seg(pdesc, 1);
		set_tx_desc_last_seg(pdesc, 1);

		/* 92SE need not to set TX packet size when firmware download */
		set_tx_desc_pkt_size(pdesc, (u16)(skb->len));
		set_tx_desc_tx_buffer_size(pdesc, (u16)(skb->len));
		set_tx_desc_tx_buffer_address(pdesc, mapping);

		wmb();
		set_tx_desc_own(pdesc, 1);
	} else { /* H2C Command Desc format (Host TXCMD) */
		/* 92SE must set as 1 for firmware download HW DMA error */
		set_tx_desc_first_seg(pdesc, 1);
		set_tx_desc_last_seg(pdesc, 1);

		set_tx_desc_offset(pdesc, 0x20);

		/* Buffer size + command header */
		set_tx_desc_pkt_size(pdesc, (u16)(skb->len));
		/* Fixed queue of H2C command */
		set_tx_desc_queue_sel(pdesc, 0x13);

		le32p_replace_bits((__le32 *)skb->data, rtlhal->h2c_txcmd_seq,
				   GENMASK(30, 24));
		set_tx_desc_tx_buffer_size(pdesc, (u16)(skb->len));
		set_tx_desc_tx_buffer_address(pdesc, mapping);

		wmb();
		set_tx_desc_own(pdesc, 1);

	}
}

void rtl92se_set_desc(struct ieee80211_hw *hw, u8 *pdesc8, bool istx,
		      u8 desc_name, u8 *val)
{
	__le32 *pdesc = (__le32 *)pdesc8;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			wmb();
			set_tx_desc_own(pdesc, 1);
			break;
		case HW_DESC_TX_NEXTDESC_ADDR:
			set_tx_desc_next_desc_address(pdesc, *(u32 *)val);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR txdesc :%d not processed\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_RXOWN:
			wmb();
			set_rx_status_desc_own(pdesc, 1);
			break;
		case HW_DESC_RXBUFF_ADDR:
			set_rx_status__desc_buff_addr(pdesc, *(u32 *)val);
			break;
		case HW_DESC_RXPKT_LEN:
			set_rx_status_desc_pkt_len(pdesc, *(u32 *)val);
			break;
		case HW_DESC_RXERO:
			set_rx_status_desc_eor(pdesc, 1);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR rxdesc :%d not processed\n",
				  desc_name);
			break;
		}
	}
}

u64 rtl92se_get_desc(struct ieee80211_hw *hw,
		     u8 *desc8, bool istx, u8 desc_name)
{
	u32 ret = 0;
	__le32 *desc = (__le32 *)desc8;

	if (istx) {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = get_tx_desc_own(desc);
			break;
		case HW_DESC_TXBUFF_ADDR:
			ret = get_tx_desc_tx_buffer_address(desc);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR txdesc :%d not processed\n",
				  desc_name);
			break;
		}
	} else {
		switch (desc_name) {
		case HW_DESC_OWN:
			ret = get_rx_status_desc_own(desc);
			break;
		case HW_DESC_RXPKT_LEN:
			ret = get_rx_status_desc_pkt_len(desc);
			break;
		case HW_DESC_RXBUFF_ADDR:
			ret = get_rx_status_desc_buff_addr(desc);
			break;
		default:
			WARN_ONCE(true, "rtl8192se: ERR rxdesc :%d not processed\n",
				  desc_name);
			break;
		}
	}
	return ret;
}

void rtl92se_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	rtl_write_word(rtlpriv, TP_POLL, BIT(0) << (hw_queue));
}
