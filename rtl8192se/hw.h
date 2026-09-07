/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_PCI92SE_HW_H__
#define __REALTEK_PCI92SE_HW_H__

void rtl92se_get_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92se_set_hw_reg(struct ieee80211_hw *hw, u8 variable, u8 *val);
void rtl92se_read_eeprom_info(struct ieee80211_hw *hw);
void rtl92se_interrupt_recognized(struct ieee80211_hw *hw,
				  struct rtl_int *int_vec);
int rtl92se_hw_init(struct ieee80211_hw *hw);
void rtl92se_card_disable(struct ieee80211_hw *hw);
void rtl92se_enable_interrupt(struct ieee80211_hw *hw);
void rtl92se_disable_interrupt(struct ieee80211_hw *hw);
void rtl92se_set_mac_addr(struct rtl_io *io, const u8 *addr);
void rtl92se_update_interrupt_mask(struct ieee80211_hw *hw,
				   u32 add_msr, u32 rm_msr);
bool rtl92se_gpio_radio_on_off_checking(struct ieee80211_hw *hw, u8 *valid);
void rtl8192se_gpiobit3_cfg_inputmode(struct ieee80211_hw *hw);
void rtl92se_suspend(struct ieee80211_hw *hw);
void rtl92se_resume(struct ieee80211_hw *hw);

#endif
