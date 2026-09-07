// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "../stats.h"
#include "reg.h"
#include "def.h"
#include "phy_common.h"
#include "trx_common.h"

u8 rtl92s_map_hwqueue_to_fwqueue(struct sk_buff *skb, u8 skb_queue)
{
	__le16 fc = rtl_get_fc(skb);

	if (unlikely(ieee80211_is_beacon(fc)))
		return QSLT_BEACON;
	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc))
		return QSLT_MGNT;
	if (ieee80211_is_nullfunc(fc))
		return QSLT_HIGH;

	/* Kernel commit 1bf4bbb4024dcdab changed EAPOL packets to use
	 * queue V0 at priority 7; however, the RTL8192SE appears to have
	 * that queue at priority 6
	 */
	if (skb->priority == 7)
		return QSLT_VO;
	return skb->priority;
}
EXPORT_SYMBOL_GPL(rtl92s_map_hwqueue_to_fwqueue);

static void _rtl92s_query_rxphystatus(struct ieee80211_hw *hw,
				       struct rtl_stats *pstats, __le32 *pdesc,
				       struct rx_fwinfo *p_drvinfo,
				       bool packet_match_bssid,
				       bool packet_toself,
				       bool packet_beacon)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct phy_sts_cck_8192s_t *cck_buf;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	s8 rx_pwr_all = 0, rx_pwr[4];
	u8 rf_rx_num = 0, evm, pwdb_all;
	u8 i, max_spatial_stream;
	u32 rssi, total_rssi = 0;
	bool is_cck = pstats->is_cck;

	pstats->packet_matchbssid = packet_match_bssid;
	pstats->packet_toself = packet_toself;
	pstats->packet_beacon = packet_beacon;
	pstats->rx_mimo_sig_qual[0] = -1;
	pstats->rx_mimo_sig_qual[1] = -1;

	if (is_cck) {
		u8 report, cck_highpwr;
		cck_buf = (struct phy_sts_cck_8192s_t *)p_drvinfo;

		if (ppsc->rfpwr_state == ERFON) {
			if (rtlhal->interface == INTF_USB)
				cck_highpwr = rtlphy->cck_high_power;
			else
				cck_highpwr = (u8)rtl_get_bbreg(hw,
						RFPGA0_XA_HSSIPARAMETER2,
						0x200);
		} else {
			cck_highpwr = false;
		}

		if (!cck_highpwr) {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = cck_buf->cck_agc_rpt & 0xc0;
			report = report >> 6;
			switch (report) {
			case 0x3:
				rx_pwr_all = -40 - (cck_agc_rpt & 0x3e);
				break;
			case 0x2:
				rx_pwr_all = -20 - (cck_agc_rpt & 0x3e);
				break;
			case 0x1:
				rx_pwr_all = -2 - (cck_agc_rpt & 0x3e);
				break;
			case 0x0:
				rx_pwr_all = 14 - (cck_agc_rpt & 0x3e);
				break;
			}
		} else {
			u8 cck_agc_rpt = cck_buf->cck_agc_rpt;
			report = p_drvinfo->cfosho[0] & 0x60;
			report = report >> 5;
			switch (report) {
			case 0x3:
				rx_pwr_all = -40 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x2:
				rx_pwr_all = -20 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x1:
				rx_pwr_all = -2 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			case 0x0:
				rx_pwr_all = 14 - ((cck_agc_rpt & 0x1f) << 1);
				break;
			}
		}

		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);

		/* CCK gain is smaller than OFDM/MCS gain,  */
		/* so we add gain diff by experiences, the val is 6 */
		pwdb_all += 6;
		if (pwdb_all > 100)
			pwdb_all = 100;
		/* modify the offset to make the same gain index with OFDM. */
		if (pwdb_all > 34 && pwdb_all <= 42)
			pwdb_all -= 2;
		else if (pwdb_all > 26 && pwdb_all <= 34)
			pwdb_all -= 6;
		else if (pwdb_all > 14 && pwdb_all <= 26)
			pwdb_all -= 8;
		else if (pwdb_all > 4 && pwdb_all <= 14)
			pwdb_all -= 4;

		pstats->rx_pwdb_all = pwdb_all;
		pstats->recvsignalpower = rx_pwr_all;

		if (packet_match_bssid) {
			u8 sq;
			if (pstats->rx_pwdb_all > 40) {
				sq = 100;
			} else {
				sq = cck_buf->sq_rpt;
				if (sq > 64)
					sq = 0;
				else if (sq < 20)
					sq = 100;
				else
					sq = ((64 - sq) * 100) / 44;
			}

			pstats->signalquality = sq;
			pstats->rx_mimo_sig_qual[0] = sq;
			pstats->rx_mimo_sig_qual[1] = -1;
		}
	} else {
		rtlpriv->dm.rfpath_rxenable[0] =
		    rtlpriv->dm.rfpath_rxenable[1] = true;
		for (i = RF90_PATH_A; i < RF6052_MAX_PATH; i++) {
			if (rtlpriv->dm.rfpath_rxenable[i])
				rf_rx_num++;

			rx_pwr[i] = ((p_drvinfo->gain_trsw[i] &
				    0x3f) * 2) - 110;
			rssi = rtl_query_rxpwrpercentage(rx_pwr[i]);
			total_rssi += rssi;
			rtlpriv->stats.rx_snr_db[i] =
					 (long)(p_drvinfo->rxsnr[i] / 2);

			if (packet_match_bssid)
				pstats->rx_mimo_signalstrength[i] = (u8) rssi;
		}

		rx_pwr_all = ((p_drvinfo->pwdb_all >> 1) & 0x7f) - 110;
		pwdb_all = rtl_query_rxpwrpercentage(rx_pwr_all);
		pstats->rx_pwdb_all = pwdb_all;
		pstats->rxpower = rx_pwr_all;
		pstats->recvsignalpower = rx_pwr_all;

		if (pstats->is_ht && pstats->rate >= DESC_RATEMCS8 &&
		    pstats->rate <= DESC_RATEMCS15)
			max_spatial_stream = 2;
		else
			max_spatial_stream = 1;

		for (i = 0; i < max_spatial_stream; i++) {
			evm = rtl_evm_db_to_percentage(p_drvinfo->rxevm[i]);

			if (packet_match_bssid) {
				if (i == 0)
					pstats->signalquality = (u8)(evm &
								 0xff);
				pstats->rx_mimo_sig_qual[i] = (u8) (evm & 0xff);
			}
		}
	}

	if (is_cck)
		pstats->signalstrength = (u8)(rtl_signal_scale_mapping(hw,
					 pwdb_all));
	else if (rf_rx_num != 0)
		pstats->signalstrength = (u8) (rtl_signal_scale_mapping(hw,
				total_rssi /= rf_rx_num));
}

static void _rtl92s_translate_rx_signal_stuff(struct ieee80211_hw *hw,
		struct sk_buff *skb, struct rtl_stats *pstats,
		__le32 *pdesc, struct rx_fwinfo *p_drvinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_hdr *hdr;
	u8 *tmp_buf;
	u8 *praddr;
	__le16 fc;
	u16 type, cfc;
	bool packet_matchbssid, packet_toself, packet_beacon = false;

	tmp_buf = skb->data + pstats->rx_drvinfo_size + pstats->rx_bufshift;

	hdr = (struct ieee80211_hdr *)tmp_buf;
	fc = hdr->frame_control;
	cfc = le16_to_cpu(fc);
	type = WLAN_FC_GET_TYPE(fc);
	praddr = hdr->addr1;

	packet_matchbssid = ((IEEE80211_FTYPE_CTL != type) &&
	     ether_addr_equal(mac->bssid,
			      (cfc & IEEE80211_FCTL_TODS) ? hdr->addr1 :
			      (cfc & IEEE80211_FCTL_FROMDS) ? hdr->addr2 :
			      hdr->addr3) &&
	     (!pstats->hwerror) && (!pstats->crc) && (!pstats->icv));

	packet_toself = packet_matchbssid &&
	    ether_addr_equal(praddr, rtlefuse->dev_addr);

	if (ieee80211_is_beacon(fc))
		packet_beacon = true;

	_rtl92s_query_rxphystatus(hw, pstats, pdesc, p_drvinfo,
			packet_matchbssid, packet_toself, packet_beacon);
	rtl_process_phyinfo(hw, tmp_buf, pstats);
}

bool rtl92s_rx_query_desc(struct ieee80211_hw *hw, struct rtl_stats *stats,
			   struct ieee80211_rx_status *rx_status, u8 *pdesc8,
			   struct sk_buff *skb)
{
	struct rx_fwinfo *p_drvinfo;
	__le32 *pdesc = (__le32 *)pdesc8;
	u32 phystatus = (u32)get_rx_status_desc_phy_status(pdesc);
	struct ieee80211_hdr *hdr;

	stats->length = (u16)get_rx_status_desc_pkt_len(pdesc);
	stats->rx_drvinfo_size = (u8)get_rx_status_desc_drvinfo_size(pdesc) * 8;
	stats->rx_bufshift = (u8)(get_rx_status_desc_shift(pdesc) & 0x03);
	stats->icv = (u16)get_rx_status_desc_icv(pdesc);
	stats->crc = (u16)get_rx_status_desc_crc32(pdesc);
	stats->hwerror = (u16)(stats->crc | stats->icv);
	stats->decrypted = !get_rx_status_desc_swdec(pdesc);

	stats->rate = (u8)get_rx_status_desc_rx_mcs(pdesc);
	stats->shortpreamble = (u16)get_rx_status_desc_splcp(pdesc);
	stats->isampdu = (bool)(get_rx_status_desc_paggr(pdesc) == 1);
	stats->isfirst_ampdu = (bool)((get_rx_status_desc_paggr(pdesc) == 1) &&
				      (get_rx_status_desc_faggr(pdesc) == 1));
	stats->timestamp_low = get_rx_status_desc_tsfl(pdesc);
	stats->rx_is40mhzpacket = (bool)get_rx_status_desc_bw(pdesc);
	stats->is_ht = (bool)get_rx_status_desc_rx_ht(pdesc);
	stats->is_cck = SE_RX_HAL_IS_CCK_RATE(pdesc);

	if (stats->hwerror)
		return false;

	rx_status->freq = hw->conf.chandef.chan->center_freq;
	rx_status->band = hw->conf.chandef.chan->band;

	if (stats->crc)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (stats->rx_is40mhzpacket)
		rx_status->bw = RATE_INFO_BW_40;

	if (stats->is_ht)
		rx_status->encoding = RX_ENC_HT;

	rx_status->flag |= RX_FLAG_MACTIME_START;

	/* hw will set stats->decrypted true, if it finds the
	 * frame is open data frame or mgmt frame,
	 * hw will not decrypt robust managment frame
	 * for IEEE80211w but still set stats->decrypted
	 * true, so here we should set it back to undecrypted
	 * for IEEE80211w frame, and mac80211 sw will help
	 * to decrypt it */
	if (stats->decrypted) {
		hdr = (struct ieee80211_hdr *)(skb->data +
		       stats->rx_drvinfo_size + stats->rx_bufshift);

		if ((_ieee80211_is_robust_mgmt_frame(hdr)) &&
			(ieee80211_has_protected(hdr->frame_control)))
			rx_status->flag &= ~RX_FLAG_DECRYPTED;
		else
			rx_status->flag |= RX_FLAG_DECRYPTED;
	}

	rx_status->rate_idx = rtlwifi_rate_mapping(hw, stats->is_ht,
						   false, stats->rate);

	rx_status->mactime = stats->timestamp_low;
	if (phystatus) {
		p_drvinfo = (struct rx_fwinfo *)(skb->data +
						 stats->rx_bufshift);
		_rtl92s_translate_rx_signal_stuff(hw, skb, stats, pdesc,
						   p_drvinfo);
	}

	/*rx_status->qual = stats->signal; */
	rx_status->signal = stats->recvsignalpower + 10;

	return true;
}
EXPORT_SYMBOL_GPL(rtl92s_rx_query_desc);
