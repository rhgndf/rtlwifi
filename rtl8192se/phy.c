// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../pci.h"
#include "../ps.h"
#include "../core.h"
#include "../rtl8192s/reg.h"
#include "../rtl8192s/def.h"
#include "../rtl8192s/phy_common.h"
#include "phy.h"
#include "hw.h"

static u32 _rtl92se_phy_rf_serial_read(struct ieee80211_hw *hw,
				      enum radio_path rfpath, u32 offset)
{

	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 newoffset;
	u32 tmplong, tmplong2;
	u8 rfpi_enable = 0;
	u32 retvalue = 0;

	offset &= 0x3f;
	newoffset = offset;

	tmplong = rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD);

	if (rfpath == RF90_PATH_A)
		tmplong2 = tmplong;
	else
		tmplong2 = rtl_get_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD);

	tmplong2 = (tmplong2 & (~BLSSI_READADDRESS)) | (newoffset << 23) |
			BLSSI_READEDGE;

	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD,
		      tmplong & (~BLSSI_READEDGE));

	mdelay(1);

	rtl_set_bbreg(hw, pphyreg->rfhssi_para2, MASKDWORD, tmplong2);
	mdelay(1);

	rtl_set_bbreg(hw, RFPGA0_XA_HSSIPARAMETER2, MASKDWORD, tmplong |
		      BLSSI_READEDGE);
	mdelay(1);

	if (rfpath == RF90_PATH_A)
		rfpi_enable = (u8)rtl_get_bbreg(hw, RFPGA0_XA_HSSIPARAMETER1,
						BIT(8));
	else if (rfpath == RF90_PATH_B)
		rfpi_enable = (u8)rtl_get_bbreg(hw, RFPGA0_XB_HSSIPARAMETER1,
						BIT(8));

	if (rfpi_enable)
		retvalue = rtl_get_bbreg(hw, pphyreg->rf_rbpi,
					 BLSSI_READBACK_DATA);
	else
		retvalue = rtl_get_bbreg(hw, pphyreg->rf_rb,
					 BLSSI_READBACK_DATA);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "RFR-%d Addr[0x%x]=0x%x\n",
		rfpath, pphyreg->rf_rb, retvalue);

	return retvalue;

}

static void _rtl92se_phy_rf_serial_write(struct ieee80211_hw *hw,
					enum radio_path rfpath, u32 offset,
					u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct bb_reg_def *pphyreg = &rtlphy->phyreg_def[rfpath];
	u32 data_and_addr = 0;
	u32 newoffset;

	offset &= 0x3f;
	newoffset = offset;

	data_and_addr = ((newoffset << 20) | (data & 0x000fffff)) & 0x0fffffff;
	rtl_set_bbreg(hw, pphyreg->rf3wire_offset, MASKDWORD, data_and_addr);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE, "RFW-%d Addr[0x%x]=0x%x\n",
		rfpath, pphyreg->rf3wire_offset, data_and_addr);
}


u32 rtl92se_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 original_value, readback_value, bitshift;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), rfpath(%#x), bitmask(%#x)\n",
		 regaddr, rfpath, bitmask);

	spin_lock(&rtlpriv->locks.rf_lock);

	original_value = _rtl92se_phy_rf_serial_read(hw, rfpath, regaddr);

	bitshift = calculate_bit_shift(bitmask);
	readback_value = (original_value & bitmask) >> bitshift;

	spin_unlock(&rtlpriv->locks.rf_lock);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), rfpath(%#x), bitmask(%#x), original_value(%#x)\n",
		regaddr, rfpath, bitmask, original_value);

	return readback_value;
}

void rtl92se_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			   u32 regaddr, u32 bitmask, u32 data)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u32 original_value, bitshift;

	if (!((rtlphy->rf_pathmap >> rfpath) & 0x1))
		return;

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		regaddr, bitmask, data, rfpath);

	spin_lock(&rtlpriv->locks.rf_lock);

	if (bitmask != RFREG_OFFSET_MASK) {
		original_value = _rtl92se_phy_rf_serial_read(hw, rfpath,
							    regaddr);
		bitshift = calculate_bit_shift(bitmask);
		data = ((original_value & (~bitmask)) | (data << bitshift));
	}

	_rtl92se_phy_rf_serial_write(hw, rfpath, regaddr, data);

	spin_unlock(&rtlpriv->locks.rf_lock);

	rtl_dbg(rtlpriv, COMP_RF, DBG_TRACE,
		"regaddr(%#x), bitmask(%#x), data(%#x), rfpath(%#x)\n",
		regaddr, bitmask, data, rfpath);

}

static void _rtl92se_phy_set_rf_sleep(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 u1btmp;

	u1btmp = rtl_read_byte(rtlpriv, LDOV12D_CTRL);
	u1btmp |= BIT(0);

	rtl_write_byte(rtlpriv, LDOV12D_CTRL, u1btmp);
	rtl_write_byte(rtlpriv, SPS1_CTRL, 0x0);
	rtl_write_byte(rtlpriv, TXPAUSE, 0xFF);
	rtl_write_word(rtlpriv, CMDR, 0x57FC);
	udelay(100);

	rtl_write_word(rtlpriv, CMDR, 0x77FC);
	rtl_write_byte(rtlpriv, PHY_CCA, 0x0);
	udelay(10);

	rtl_write_word(rtlpriv, CMDR, 0x37FC);
	udelay(10);

	rtl_write_word(rtlpriv, CMDR, 0x77FC);
	udelay(10);

	rtl_write_word(rtlpriv, CMDR, 0x57FC);

	/* we should chnge GPIO to input mode
	 * this will drop away current about 25mA*/
	rtl8192se_gpiobit3_cfg_inputmode(hw);
}

bool rtl92se_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	bool bresult = true;
	u8 i, queue_id;
	struct rtl8192_tx_ring *ring = NULL;

	if (rfpwr_state == ppsc->rfpwr_state)
		return false;

	switch (rfpwr_state) {
	case ERFON:{
			if ((ppsc->rfpwr_state == ERFOFF) &&
			    RT_IN_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC)) {

				bool rtstatus;
				u32 initializecount = 0;
				do {
					initializecount++;
					rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
						"IPS Set eRf nic enable\n");
					rtstatus = rtl_ps_enable_nic(hw);
				} while (!rtstatus && (initializecount < 10));

				RT_CLEAR_PS_LEVEL(ppsc,
						  RT_RF_OFF_LEVL_HALT_NIC);
			} else {
				rtl_dbg(rtlpriv, COMP_POWER, DBG_DMESG,
					"awake, slept:%d ms state_inap:%x\n",
					jiffies_to_msecs(jiffies -
					ppsc->last_sleep_jiffies),
					rtlpriv->psc.state_inap);
				ppsc->last_awake_jiffies = jiffies;
				rtl_write_word(rtlpriv, CMDR, 0x37FC);
				rtl_write_byte(rtlpriv, TXPAUSE, 0x00);
				rtl_write_byte(rtlpriv, PHY_CCA, 0x3);
			}

			if (mac->link_state == MAC80211_LINKED)
				rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_LINK);
			else
				rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_NO_LINK);
			break;
		}
	case ERFOFF:{
			if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_HALT_NIC) {
				rtl_dbg(rtlpriv, COMP_RF, DBG_DMESG,
					"IPS Set eRf nic disable\n");
				rtl_ps_disable_nic(hw);
				RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);
			} else {
				if (ppsc->rfoff_reason == RF_CHANGE_BY_IPS)
					rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_NO_LINK);
				else
					rtlpriv->cfg->ops->led_control(hw,
							 LED_CTL_POWER_OFF);
			}
			break;
		}
	case ERFSLEEP:
			if (ppsc->rfpwr_state == ERFOFF)
				return false;

			for (queue_id = 0, i = 0;
			     queue_id < RTL_PCI_MAX_TX_QUEUE_COUNT;) {
				ring = &pcipriv->dev.tx_ring[queue_id];
				if (skb_queue_len(&ring->queue) == 0 ||
					queue_id == BEACON_QUEUE) {
					queue_id++;
					continue;
				} else {
					rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
						"eRf Off/Sleep: %d times TcbBusyQueue[%d] = %d before doze!\n",
						i + 1, queue_id,
						skb_queue_len(&ring->queue));

					udelay(10);
					i++;
				}

				if (i >= MAX_DOZE_WAITING_TIMES_9x) {
					rtl_dbg(rtlpriv, COMP_ERR, DBG_WARNING,
						"ERFOFF: %d times TcbBusyQueue[%d] = %d !\n",
						MAX_DOZE_WAITING_TIMES_9x,
						queue_id,
						skb_queue_len(&ring->queue));
					break;
				}
			}

			rtl_dbg(rtlpriv, COMP_POWER, DBG_DMESG,
				"Set ERFSLEEP awaked:%d ms\n",
				jiffies_to_msecs(jiffies -
						 ppsc->last_awake_jiffies));

			rtl_dbg(rtlpriv, COMP_POWER, DBG_DMESG,
				"sleep awaked:%d ms state_inap:%x\n",
				jiffies_to_msecs(jiffies -
						 ppsc->last_awake_jiffies),
				 rtlpriv->psc.state_inap);
			ppsc->last_sleep_jiffies = jiffies;
			_rtl92se_phy_set_rf_sleep(hw);
			break;
	default:
		pr_err("switch case %#x not processed\n",
		       rfpwr_state);
		bresult = false;
		break;
	}

	if (bresult)
		ppsc->rfpwr_state = rfpwr_state;

	return bresult;
}

static	void _rtl92se_phy_check_ephy_switchready(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32	delay = 100;
	u8	regu1;

	regu1 = rtl_read_byte(rtlpriv, 0x554);
	while ((regu1 & BIT(5)) && (delay > 0)) {
		regu1 = rtl_read_byte(rtlpriv, 0x554);
		delay--;
		/* We delay only 50us to prevent
		 * being scheduled out. */
		udelay(50);
	}
}

void rtl92se_phy_switch_ephy_parameter(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	/* The way to be capable to switch clock request
	 * when the PG setting does not support clock request.
	 * This is the backdoor solution to switch clock
	 * request before ASPM or D3. */
	rtl_write_dword(rtlpriv, 0x540, 0x73c11);
	rtl_write_dword(rtlpriv, 0x548, 0x2407c);

	/* Switch EPHY parameter!!!! */
	rtl_write_word(rtlpriv, 0x550, 0x1000);
	rtl_write_byte(rtlpriv, 0x554, 0x20);
	_rtl92se_phy_check_ephy_switchready(hw);

	rtl_write_word(rtlpriv, 0x550, 0xa0eb);
	rtl_write_byte(rtlpriv, 0x554, 0x3e);
	_rtl92se_phy_check_ephy_switchready(hw);

	rtl_write_word(rtlpriv, 0x550, 0xff80);
	rtl_write_byte(rtlpriv, 0x554, 0x39);
	_rtl92se_phy_check_ephy_switchready(hw);

	/* Delay L1 enter time */
	if (ppsc->support_aspm && !ppsc->support_backdoor)
		rtl_write_byte(rtlpriv, 0x560, 0x40);
	else
		rtl_write_byte(rtlpriv, 0x560, 0x00);

}
