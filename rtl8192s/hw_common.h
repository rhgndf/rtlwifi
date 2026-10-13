/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_92S_HW_COMMON_H__
#define __REALTEK_92S_HW_COMMON_H__

#define MSR_LINK_MANAGED   2
#define MSR_LINK_NONE      0
#define MSR_LINK_SHIFT     0
#define MSR_LINK_ADHOC     1
#define MSR_LINK_MASTER    3

enum WIRELESS_NETWORK_TYPE {
	WIRELESS_11B = 1,
	WIRELESS_11G = 2,
	WIRELESS_11A = 4,
	WIRELESS_11N = 8
};

void rtl92s_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92s_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92s_set_acm_ctrl(struct ieee80211_hw *hw, u8 variable, u8 *val,
			 enum acm_method acm_method);
int rtl92s_set_media_status(struct ieee80211_hw *hw,
			    enum nl80211_iftype type);
int rtl92s_set_network_type(struct ieee80211_hw *hw,
			    enum nl80211_iftype type);
void rtl92s_set_check_bssid(struct ieee80211_hw *hw, bool check_bssid);
void rtl92s_set_qos(struct ieee80211_hw *hw, int aci);
void rtl92s_set_beacon_related_registers(struct ieee80211_hw *hw);
void rtl92s_set_beacon_interval(struct ieee80211_hw *hw);
void rtl92s_update_hal_rate_tbl(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 rssi_level, bool update_bw);
void rtl92s_update_channel_access_setting(struct ieee80211_hw *hw);
void rtl92s_enable_hw_security_config(struct ieee80211_hw *hw);
void rtl92s_set_key(struct ieee80211_hw *hw, u32 key_index, u8 *macaddr,
		    bool is_group, u8 enc_algo, bool is_wepkey,
		    bool clear_all);

#endif
