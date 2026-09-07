/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../usb.h"
#include "../base.h"
#include "../stats.h"
#include "../efuse.h"
#include "../rtl8192s/reg.h"
#include "../rtl8192s/def.h"
#include "../rtl8192s/phy_common.h"
#include "../rtl8192s/trx_common.h"
#include "../rtl8192s/fw_common.h"
#include "../rtl8192s/hw_common.h"
#include "reg.h"
#include "hw.h"
#include "sw.h"
#include "trx.h"
#include "led.h"

/* endpoint mapping */
static bool rtl92su_has_ep(const u8 *eps, u8 count, u8 ep)
{
	u8 i;

	for (i = 0; i < count; i++)
		if (eps[i] == ep)
			return true;
	return false;
}

int rtl92su_endpoint_layout(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	const u8 *map = rtlpriv->efuse.efuse_map[EFUSE_INIT_MAP];
	u8 eeprom_layout;
	int layout;
	static const u8 out4[] = { 4, 6, 13 };
	static const u8 out6[] = { 4, 5, 6, 7, 13 };
	static const u8 out11[] = { 4, 5, 6, 7, 10, 11, 12, 13 };
	const u8 *required;
	u8 required_count;
	u8 i;

	switch (rtlusb->out_ep_nums) {
	case ARRAY_SIZE(out4):
		layout = 4;
		required = out4;
		required_count = ARRAY_SIZE(out4);
		break;
	case ARRAY_SIZE(out6):
		layout = 6;
		required = out6;
		required_count = ARRAY_SIZE(out6);
		break;
	case ARRAY_SIZE(out11):
		layout = 11;
		required = out11;
		required_count = ARRAY_SIZE(out11);
		break;
	default:
		return -EINVAL;
	}

	for (i = 0; i < required_count; i++)
		if (!rtl92su_has_ep(rtlusb->out_eps, rtlusb->out_ep_nums,
				    required[i]))
			return -EINVAL;

	if (layout == 11) {
		if (rtlusb->in_ep_nums != 2 ||
		    !rtl92su_has_ep(rtlusb->in_eps, rtlusb->in_ep_nums, 3) ||
		    !rtl92su_has_ep(rtlusb->in_eps, rtlusb->in_ep_nums, 9))
			return -EINVAL;
	} else if (rtlusb->in_ep_nums != 1 ||
		   !rtl92su_has_ep(rtlusb->in_eps, rtlusb->in_ep_nums, 3)) {
		return -EINVAL;
	}

	switch ((map[offsetof(struct r92su_eeprom, usb_optional)] >> 3) & 3) {
	case 0:
		eeprom_layout = 6;
		break;
	case 1:
		eeprom_layout = 11;
		break;
	case 2:
		eeprom_layout = 4;
		break;
	default:
		return -EINVAL;
	}
	return layout == eeprom_layout ? layout : -EINVAL;
}

int rtl92su_endpoint_mapping(struct ieee80211_hw *hw)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl_ep_map *ep_map = &rtlusb->ep_map;
	int layout = rtl92su_endpoint_layout(hw);

	if (layout < 0)
		return layout;

	ep_map->ep_mapping[RTL_TXQ_BE] = 6;
	ep_map->ep_mapping[RTL_TXQ_VO] = 4;
	switch (layout) {
	case 4:
		ep_map->ep_mapping[RTL_TXQ_BK] = 6;
		ep_map->ep_mapping[RTL_TXQ_VI] = 4;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 13;
		ep_map->ep_mapping[RTL_TXQ_HI] = 13;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 13;
		break;
	case 6:
		ep_map->ep_mapping[RTL_TXQ_BK] = 7;
		ep_map->ep_mapping[RTL_TXQ_VI] = 5;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 13;
		ep_map->ep_mapping[RTL_TXQ_HI] = 13;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 13;
		break;
	case 11:
		ep_map->ep_mapping[RTL_TXQ_BK] = 7;
		ep_map->ep_mapping[RTL_TXQ_VI] = 5;
		ep_map->ep_mapping[RTL_TXQ_BCN] = 10;
		ep_map->ep_mapping[RTL_TXQ_HI] = 11;
		ep_map->ep_mapping[RTL_TXQ_MGT] = 12;
		break;
	}
	return 0;
}

u16 rtl92su_mq_to_hwq(__le16 fc, u16 mac80211_queue_index)
{
	u16 hw_queue_index;

	if (unlikely(ieee80211_is_beacon(fc))) {
		hw_queue_index = RTL_TXQ_BCN;
		goto out;
	}
	if (ieee80211_is_mgmt(fc)) {
		hw_queue_index = RTL_TXQ_MGT;
		goto out;
	}
	switch (mac80211_queue_index) {
	case 0:
		hw_queue_index = RTL_TXQ_VO;
		break;
	case 1:
		hw_queue_index = RTL_TXQ_VI;
		break;
	case 2:
		hw_queue_index = RTL_TXQ_BE;
		break;
	case 3:
		hw_queue_index = RTL_TXQ_BK;
		break;
	default:
		hw_queue_index = RTL_TXQ_BE;
		WARN_ONCE(true, "rtl8192su: QSLT_BE queue, skb_queue:%d\n",
			  mac80211_queue_index);
		break;
	}
out:
	return hw_queue_index;
}

#define RTL92SU_C2H_HBCN		0x15

struct rtl92su_c2h {
	__le16 len;
	u8 event;
	u8 cmd_seq;
	u8 agg_num;
	u8 unknown;
	__le16 agg_total_len;
} __packed;

static int rtl92su_rx_record_len(const u8 *data, size_t remaining,
				  size_t *len, size_t *stride)
{
	const __le32 *pdesc = (const __le32 *)data;
	size_t header_len;
	size_t pkt_len;

	if (remaining < RTL_RX_DESC_SIZE)
		return -EINVAL;
	pkt_len = get_rx_status_desc_pkt_len((__le32 *)pdesc);
	if (!pkt_len)
		return -EINVAL;
	header_len = RTL_RX_DESC_SIZE +
		     get_rx_status_desc_drvinfo_size((__le32 *)pdesc) * 8 +
		     get_rx_status_desc_shift((__le32 *)pdesc);
	if (header_len > remaining || pkt_len > remaining - header_len)
		return -EINVAL;
	*len = header_len + pkt_len;
	if (*len > SIZE_MAX - 127)
		return -EINVAL;
	*stride = ALIGN(*len, 128);
	return 0;
}

static void rtl92su_send_buffered_bc(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_tcb_desc tcb_desc;
	struct sk_buff *skb;

	if (!mac->vif || !mac->beacon_enabled ||
	    (mac->vif->type != NL80211_IFTYPE_AP &&
	     mac->vif->type != NL80211_IFTYPE_ADHOC))
		return;
	while ((skb = ieee80211_get_buffered_bc(hw, mac->vif))) {
		memset(&tcb_desc, 0, sizeof(tcb_desc));
		rtlpriv->intf_ops->adapter_tx(hw, NULL, skb, &tcb_desc);
	}
}

static void rtl92su_handle_c2h(struct ieee80211_hw *hw, const u8 *data,
			       size_t record_len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	const __le32 *pdesc = (const __le32 *)data;
	size_t offset = RTL_RX_DESC_SIZE +
			get_rx_status_desc_drvinfo_size((__le32 *)pdesc) * 8 +
			get_rx_status_desc_shift((__le32 *)pdesc);
	size_t pkt_len = get_rx_status_desc_pkt_len((__le32 *)pdesc);
	const struct rtl92su_c2h *c2h;
	u16 payload_len;
	u16 total_len;

	if (offset > record_len || pkt_len > record_len - offset ||
	    pkt_len < sizeof(*c2h))
		return;
	c2h = (const struct rtl92su_c2h *)(data + offset);
	payload_len = le16_to_cpu(c2h->len);
	total_len = le16_to_cpu(c2h->agg_total_len);
	if (payload_len > pkt_len - sizeof(*c2h) ||
	    (total_len && (total_len < sizeof(*c2h) + payload_len ||
			   total_len > pkt_len)) ||
	    (c2h->agg_num && !total_len))
		return;

	switch (c2h->event) {
	case RTL92SU_C2H_HBCN:
		rtl92su_send_buffered_bc(hw);
		break;
	default:
		RT_TRACE(rtlpriv, COMP_RECV, DBG_DMESG,
			 "Unknown event 0x%x\n", c2h->event);
		break;
	}
}

static void rtl92su_rx_data(struct ieee80211_hw *hw, struct sk_buff *skb,
			    const u8 *pdesc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct rtl_stats stats = {};
	struct ieee80211_hdr *hdr;
	unsigned int hdrlen;
	unsigned int offset;
	__le16 fc;
	bool unicast = false;
	u16 pkt_len = get_rx_status_desc_pkt_len((__le32 *)pdesc);
	if (get_rx_status_desc_crc32((__le32 *)pdesc) ||
	    get_rx_status_desc_icv((__le32 *)pdesc))
		goto drop;

	offset = get_rx_status_desc_drvinfo_size((__le32 *)pdesc) * 8 +
		 get_rx_status_desc_shift((__le32 *)pdesc);
	if (skb->len < RTL_RX_DESC_SIZE + offset + pkt_len ||
	    pkt_len < sizeof(fc) + FCS_LEN)
		goto drop;
	memcpy(&fc, skb->data + RTL_RX_DESC_SIZE + offset, sizeof(fc));
	hdrlen = ieee80211_hdrlen(fc);
	if (!hdrlen || pkt_len < hdrlen + FCS_LEN)
		goto drop;

	skb_pull(skb, RTL_RX_DESC_SIZE);
	memset(rx_status, 0, sizeof(*rx_status));
	if (!rtl92s_rx_query_desc(hw, &stats, rx_status, (u8 *)pdesc, skb))
		goto drop;
	skb_pull(skb, offset);
	skb_trim(skb, pkt_len);
	hdr = (struct ieee80211_hdr *)skb->data;

	if (!stats.crc) {
		if (is_broadcast_ether_addr(hdr->addr1)) {
			/* TODO */
		} else if (is_multicast_ether_addr(hdr->addr1)) {
			/* TODO */
		} else {
			unicast = true;
			rtlpriv->stats.rxbytesunicast += skb->len;
		}

		if (ieee80211_is_data(fc)) {
			rtlpriv->cfg->ops->led_control(hw, LED_CTL_RX);
			if (unicast)
				rtlpriv->link_info.num_rx_inperiod++;
		}
		rtl_beacon_statistic(hw, skb);
		if (likely(rtl_action_proc(hw, skb, false))) {
			ieee80211_rx(hw, skb);
			return;
		}
	}
drop:
	dev_kfree_skb_any(skb);
}

void rtl92su_rx_hdl(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	const __le32 *first;
	const u8 *data;
	size_t remaining;
	unsigned int count;
	unsigned int i;

	if (skb->len < RTL_RX_DESC_SIZE)
		goto free_parent;
	first = (const __le32 *)skb->data;
	count = le32_get_bits(first[2], GENMASK(23, 16));
	if (!count)
		count = 1;
	if (count > skb->len / RTL_RX_DESC_SIZE)
		goto free_parent;

	data = skb->data;
	remaining = skb->len;
	for (i = 0; i < count; i++) {
		struct sk_buff *record;
		size_t record_len;
		size_t stride;

		if (rtl92su_rx_record_len(data, remaining, &record_len,
					   &stride))
			goto free_parent;
		if (i + 1 < count && stride > remaining)
			goto free_parent;

		if (rtl92s_rx_is_cmd((const __le32 *)data)) {
			rtl92su_handle_c2h(hw, data, record_len);
			if (count == 1)
				goto free_parent;
		} else if (count == 1) {
			rtl92su_rx_data(hw, skb, data);
			return;
		} else {
			record = dev_alloc_skb(record_len);
			if (record) {
				skb_put_data(record, data, record_len);
				rtl92su_rx_data(hw, record, record->data);
			}
		}

		if (i + 1 == count)
			break;
		data += stride;
		remaining -= stride;
	}

free_parent:
	dev_kfree_skb_any(skb);
}

void rtl92su_tx_fill_desc(struct ieee80211_hw *hw,
		struct ieee80211_hdr *hdr, u8 *pdesc_tx, u8 *pbd_desc_tx,
		struct ieee80211_tx_info *info,
		struct ieee80211_sta *sta,
		struct sk_buff *skb,
		u8 hw_queue, struct rtl_tcb_desc *ptcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	__le32 *pdesc;
	u16 seq_number;
	__le16 fc = hdr->frame_control;
	u8 reserved_macid = 0;
	u8 fw_qsel = rtl92s_map_hwqueue_to_fwqueue(skb, hw_queue);
	bool firstseg = (!(hdr->seq_ctrl & cpu_to_le16(IEEE80211_SCTL_FRAG)));
	bool lastseg = (!(hdr->frame_control &
			cpu_to_le16(IEEE80211_FCTL_MOREFRAGS)));
	u8 bw_40 = 0;

	if (mac->opmode == NL80211_IFTYPE_STATION) {
		bw_40 = mac->bw_40;
	} else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (sta)
			bw_40 = sta->deflink.bandwidth >= IEEE80211_STA_RX_BW_40;
	}

	seq_number = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;

	rtl_get_tcb_desc(hw, info, sta, skb, ptcb_desc);
	pdesc = (__le32 *)skb_push(skb, RTL_TX_HEADER_SIZE);
	memset(pdesc, 0, RTL_TX_HEADER_SIZE);

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
			set_tx_desc_tx_short(pdesc, 1);

		/* Aggregation related */
		if (info->flags & IEEE80211_TX_CTL_AMPDU)
			set_tx_desc_agg_enable(pdesc, 1);

		/* For AMPDU, we must insert SSN into TX_DESC */
		set_tx_desc_seq(pdesc, seq_number);

		/* Protection mode related */
		/* For 92S, if RTS/CTS are set, HW will execute RTS. */
		/* We choose only one protection mode to execute */
		set_tx_desc_rts_enable(pdesc, ((ptcb_desc->rts_enable &&
				!ptcb_desc->cts_enable) ? 1 : 0));
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
		set_tx_desc_pkt_size(pdesc,
				    (u16)skb->len - RTL_TX_HEADER_SIZE);

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

		/* Always enable all rate fallback range */
		set_tx_desc_data_rate_fb_limit(pdesc, 0x1F);

		/* Fix: I don't know why hw use 6.5M to tx when set it */
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
	set_tx_desc_own(pdesc, 1);

	/* DWORD 7 */
	set_tx_desc_tx_buffer_size(pdesc, (u16)skb->len);

	RT_TRACE(rtlpriv, COMP_SEND, DBG_TRACE, "\n");
}

void rtl92su_tx_fill_cmddesc(struct ieee80211_hw *hw, u8 *pdesc8,
	struct sk_buff *skb)
{
	struct rtl_tcb_desc *tcb_desc = (struct rtl_tcb_desc *)(skb->cb);
	__le32 *pdesc = (__le32 *)pdesc8;

	/* Clear all status	*/
	memset((void *)pdesc, 0, RTL_TX_HEADER_SIZE);

	/* This bit indicate this packet is used for FW download. */
	if (tcb_desc->cmd_or_init == DESC_PACKET_TYPE_INIT) {
		/* For firmware downlaod we only need to set LINIP */
		set_tx_desc_linip(pdesc, tcb_desc->last_inipkt);

		/* 92SU need not to set TX packet size when firmware download */
		set_tx_desc_pkt_size(pdesc,
				     (u16)(skb->len - RTL_TX_HEADER_SIZE));
	} else { /* H2C Command Desc format (Host TXCMD) */
		/* 92SE must set as 1 for firmware download HW DMA error */
		set_tx_desc_first_seg(pdesc, 1);
		set_tx_desc_last_seg(pdesc, 1);

		set_tx_desc_offset(pdesc, 0x20);

		/* Buffer size + command header */
		set_tx_desc_pkt_size(pdesc,
				     (u16)(skb->len - RTL_TX_HEADER_SIZE));
		/* Fixed queue of H2C command */
		set_tx_desc_queue_sel(pdesc, 0x13);
		set_tx_desc_own(pdesc, 1);

		set_tx_desc_tx_buffer_size(pdesc, (u16)(skb->len));
	}
}

struct rtl92su_init_urb {
	struct completion done;
	int status;
	unsigned int actual_length;
};

static void rtl92su_init_complete(struct urb *urb)
{
	struct rtl92su_init_urb *ctx = urb->context;

	ctx->status = urb->status;
	ctx->actual_length = urb->actual_length;
	complete(&ctx->done);
}

static bool rtl92su_send_init(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct rtl92su_init_urb ctx;
	struct urb *urb;
	long timeout;
	int err;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return false;

	init_completion(&ctx.done);
	ctx.status = -EINPROGRESS;
	ctx.actual_length = 0;
	usb_fill_bulk_urb(urb, rtlusb->udev,
			  usb_sndbulkpipe(rtlusb->udev, 4),
			  skb->data, skb->len, rtl92su_init_complete, &ctx);
	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &rtlusb->tx_submitted);
	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err) {
		usb_unanchor_urb(urb);
		usb_free_urb(urb);
		return false;
	}

	timeout = wait_for_completion_timeout(&ctx.done,
					      msecs_to_jiffies(1000));
	if (!timeout)
		usb_kill_urb(urb);
	err = !timeout ? -ETIMEDOUT : ctx.status;
	if (!err && ctx.actual_length != skb->len)
		err = -EIO;
	usb_free_urb(urb);
	if (err)
		return false;
	dev_kfree_skb_any(skb);
	return true;
}

static void rtl92su_h2c_complete(struct urb *urb)
{
	dev_kfree_skb_any(urb->context);
}

static bool rtl92su_send_h2c(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct rtl_usb *rtlusb = rtl_usbdev(rtl_usbpriv(hw));
	struct urb *urb;
	int err;

	if (IS_USB_STOP(rtlusb))
		return false;
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return false;
	usb_fill_bulk_urb(urb, rtlusb->udev,
			  usb_sndbulkpipe(rtlusb->udev, 13),
			  skb->data, skb->len, rtl92su_h2c_complete, skb);
	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &rtlusb->tx_submitted);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		usb_unanchor_urb(urb);
		usb_free_urb(urb);
		return false;
	}
	usb_free_urb(urb);
	return true;
}

bool rtl92su_cmd_send_packet(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct rtl_tcb_desc *tcb_desc = (struct rtl_tcb_desc *)skb->cb;
	bool init = tcb_desc->cmd_or_init == DESC_PACKET_TYPE_INIT;
	u8 *pdesc;

	if (skb_cow_head(skb, RTL_TX_HEADER_SIZE))
		return false;
	pdesc = skb_push(skb, RTL_TX_HEADER_SIZE);
	rtl92su_tx_fill_cmddesc(hw, pdesc, skb);
	return init ? rtl92su_send_init(hw, skb) :
		      rtl92su_send_h2c(hw, skb);
}

void rtl92su_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			  u32 cmd_len, u8 *cmd_buffer)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl92su_priv *su = rtl92su_priv(hw);
	struct sk_buff *skb;
	unsigned long flags;

	if (!su || element_id != H2C_RA_MASK ||
	    cmd_len != sizeof(struct rtl92s_rate_mask_h2c) || !cmd_buffer)
		return;
	skb = alloc_skb(cmd_len, GFP_ATOMIC);
	if (!skb)
		return;
	skb_put_data(skb, cmd_buffer, cmd_len);

	spin_lock_irqsave(&su->rate_queue.lock, flags);
	if (su->stopping || skb_queue_len(&su->rate_queue) >= 32) {
		spin_unlock_irqrestore(&su->rate_queue.lock, flags);
		dev_kfree_skb_any(skb);
		return;
	}
	__skb_queue_tail(&su->rate_queue, skb);
	queue_work(rtlpriv->works.rtl_wq, &su->rate_work);
	spin_unlock_irqrestore(&su->rate_queue.lock, flags);
}

void rtl92su_rate_work(struct work_struct *work)
{
	struct rtl92su_priv *su =
		container_of(work, struct rtl92su_priv, rate_work);
	struct ieee80211_hw *hw = su->hw;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl92s_rate_mask_h2c rate;
	struct sk_buff *skb;
	unsigned long flags;
	u32 bitmap;
	u16 mask;

	for (;;) {
		spin_lock_irqsave(&su->rate_queue.lock, flags);
		skb = su->stopping ? NULL : __skb_dequeue(&su->rate_queue);
		spin_unlock_irqrestore(&su->rate_queue.lock, flags);
		if (!skb)
			break;

		memcpy(&rate, skb->data, sizeof(rate));
		bitmap = le32_to_cpu(rate.ratr_bitmap);
		mask = le16_to_cpu(rate.mask);
		if (bitmap & BIT(28))
			rtl_write_byte(rtlpriv, SG_RATE, rate.shortgi_rate);
		if (!rtl92s_phy_send_fw_cmd(hw,
					   FW_RA_UPDATE_MASK | ((u32)mask << 8),
					   &bitmap, false))
			su->io_error = true;
		dev_kfree_skb_any(skb);
	}
}

void rtl92su_stop_rate_work(struct ieee80211_hw *hw)
{
	struct rtl92su_priv *su = rtl92su_priv(hw);
	unsigned long flags;

	if (!su)
		return;
	spin_lock_irqsave(&su->rate_queue.lock, flags);
	su->stopping = true;
	spin_unlock_irqrestore(&su->rate_queue.lock, flags);
	cancel_work_sync(&su->rate_work);
	skb_queue_purge(&su->rate_queue);
}

int rtl92su_update_beacon(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct sk_buff *skb;
	struct ieee80211_mutable_offsets offs;
	int err = -ENOMEM;
	u32 extra = 0;
	if (!mac->vif)
		return -ENODEV;

	skb = ieee80211_beacon_get_template(hw, mac->vif, &offs, 0);
	if (skb) {
		struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct rtl_tcb_desc tcb_desc;
		memset(&tcb_desc, 0, sizeof(tcb_desc));

		rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, NULL, NULL,
						info, NULL, skb,
						QSLT_CMD, &tcb_desc);

		extra = (u32)offs.tim_offset << 16;
		err = rtl92s_firmware_set_h2c_cmd(hw, H2C_UPDATE_BCN_CMD,
						  extra, skb->data, skb->len);
		dev_kfree_skb_any(skb);
	}
	return err;
}


void rtl92su_tx_cleanup(struct ieee80211_hw *hw, struct sk_buff *skb)
{
}

int rtl92su_tx_post_hdl(struct ieee80211_hw *hw, struct urb *urb,
			struct sk_buff *skb)
{
	struct ieee80211_tx_info *info;
	bool no_ack;

	skb_pull(skb, RTL_TX_HEADER_SIZE);
	info = IEEE80211_SKB_CB(skb);
	no_ack = info->flags & IEEE80211_TX_CTL_NO_ACK;
	ieee80211_tx_info_clear_status(info);
	if (!urb->status && no_ack)
		info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
	ieee80211_tx_status_irqsafe(hw, skb);
	return 1;
}

struct sk_buff *rtl92su_tx_aggregate_hdl(struct ieee80211_hw *hw,
					 struct sk_buff_head *list)
{
	return skb_dequeue(list);
}

void rtl92su_tx_polling(struct ieee80211_hw *hw, u8 hw_queue)
{
}
