/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.
 * Copyright(c) 2012  Joshua Roys <Joshua.Roys@gtri.gatech.edu>
 * Copyright(c) 2012-2017  Christian Lamparter <chunkeey@gmail.com>
 */

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

#endif
