/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.
 * Copyright(c) 2012  Joshua Roys <Joshua.Roys@gtri.gatech.edu>
 * Copyright(c) 2012-2017  Christian Lamparter <chunkeey@gmail.com>
 */

#ifndef __REALTEK_92SU_LED_H__
#define __REALTEK_92SU_LED_H__

void rtl92su_init_sw_leds(struct ieee80211_hw *hw);
void rtl92su_sw_led_on(struct ieee80211_hw *hw, enum rtl_led_pin ledpin);
void rtl92su_sw_led_off(struct ieee80211_hw *hw, enum rtl_led_pin ledpin);
void rtl92su_led_control(struct ieee80211_hw *hw, enum led_ctl_mode ledaction);
void rtl92su_deinit_sw_leds(struct ieee80211_hw *hw);

#endif
