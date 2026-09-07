/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_PCI92SE_PHY_H__
#define __REALTEK_PCI92SE_PHY_H__

u32 rtl92se_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			     u32 regaddr, u32 bitmask);
void rtl92se_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask, u32 data);
bool rtl92se_phy_set_rf_power_state(struct ieee80211_hw *hw,
				    enum rf_pwrstate rfpower_state);
void rtl92se_phy_switch_ephy_parameter(struct ieee80211_hw *hw);

#endif
