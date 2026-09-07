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
 *****************************************************************************/
#ifndef __REALTEK_USB92SU_SW_H__
#define __REALTEK_USB92SU_SW_H__

#include "../rtl8192s/fw_common.h"

struct rtl92su_priv {
	struct rt_firmware firmware;
	struct ieee80211_hw *hw;
	struct work_struct rate_work;
	struct sk_buff_head rate_queue;
	bool stopping;
	bool io_error;
};

static inline struct rtl92su_priv *rtl92su_priv(struct ieee80211_hw *hw)
{
	struct rt_firmware *firmware =
		(struct rt_firmware *)rtl_hal(rtl_priv(hw))->pfirmware;

	if (!firmware)
		return NULL;
	return container_of(firmware, struct rtl92su_priv, firmware);
}

#define EFUSE_MAX_SECTION	16

#define RT_TRACE	rtl_dbg

#endif
