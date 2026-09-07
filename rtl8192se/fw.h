/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __REALTEK_PCI92SE_FIRMWARE_H__
#define __REALTEK_PCI92SE_FIRMWARE_H__

#include "../rtl8192s/fw_common.h"

bool rtl92se_cmd_send_packet(struct ieee80211_hw *hw, struct sk_buff *skb);
void rtl92se_fw_set_rqpn(struct ieee80211_hw *hw);
void rtl92se_fw_download_poll(struct ieee80211_hw *hw);

#endif
