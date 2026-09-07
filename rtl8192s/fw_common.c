// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#include "../wifi.h"
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "fw_common.h"

int rtl92s_validate_fw(const u8 *data, size_t size)
{
	const struct fw_hdr *header;
	size_t remaining;
	u32 length;

	if (!data || size < sizeof(*header) ||
	    size > RTL8190_MAX_RAW_FIRMWARE_CODE_SIZE)
		return -EINVAL;

	header = (const struct fw_hdr *)data;
	if (le16_to_cpu(header->signature) != 0x8192 ||
	    le32_to_cpu(header->fw_priv_size) != sizeof(header->fwpriv))
		return -EINVAL;

	remaining = size - sizeof(*header);
	length = le32_to_cpu(header->img_imem_size);
	if (!length || length > RTL8190_MAX_FIRMWARE_CODE_SIZE ||
	    length > remaining)
		return -EINVAL;
	remaining -= length;

	length = le32_to_cpu(header->img_sram_size);
	if (length > RTL8190_MAX_FIRMWARE_CODE_SIZE || length > remaining)
		return -EINVAL;
	remaining -= length;

	if (le32_to_cpu(header->dmem_size) > remaining)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(rtl92s_validate_fw);

static void rtl92s_fw_set_rqpn(struct ieee80211_hw *hw)
{
	struct rt_firmware *firmware =
		(struct rt_firmware *)rtl_priv(hw)->rtlhal.pfirmware;

	if (firmware->fw_set_rqpn)
		firmware->fw_set_rqpn(hw);
}

static bool rtl92s_cmd_send_packet(struct ieee80211_hw *hw,
				   struct sk_buff *skb)
{
	struct rt_firmware *firmware =
		(struct rt_firmware *)rtl_priv(hw)->rtlhal.pfirmware;

	return firmware->cmd_send_packet && firmware->cmd_send_packet(hw, skb);
}

static void rtl92s_fw_download_poll(struct ieee80211_hw *hw)
{
	struct rt_firmware *firmware =
		(struct rt_firmware *)rtl_priv(hw)->rtlhal.pfirmware;

	if (firmware->fw_download_poll)
		firmware->fw_download_poll(hw);
}

static bool _rtl92s_firmware_enable_cpu(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 ichecktime = 200;
	u16 tmpu2b;
	u8 tmpu1b, cpustatus = 0;

	rtl92s_fw_set_rqpn(hw);

	/* Enable CPU. */
	tmpu1b = rtl_read_byte(rtlpriv, SYS_CLKR);
	/* AFE source */
	rtl_write_byte(rtlpriv, SYS_CLKR, (tmpu1b | SYS_CPU_CLKSEL));

	tmpu2b = rtl_read_word(rtlpriv, REG_SYS_FUNC_EN);
	rtl_write_word(rtlpriv, REG_SYS_FUNC_EN, (tmpu2b | FEN_CPUEN));

	/* Polling IMEM Ready after CPU has refilled. */
	do {
		cpustatus = rtl_read_byte(rtlpriv, TCR);
		if (cpustatus & IMEM_RDY) {
			rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
				"IMEM Ready after CPU has refilled\n");
			break;
		}

		udelay(100);
	} while (ichecktime--);

	if (!(cpustatus & IMEM_RDY))
		return false;

	return true;
}

static enum fw_status _rtl92s_firmware_get_nextstatus(
		enum fw_status fw_currentstatus)
{
	enum fw_status	next_fwstatus = 0;

	switch (fw_currentstatus) {
	case FW_STATUS_INIT:
		next_fwstatus = FW_STATUS_LOAD_IMEM;
		break;
	case FW_STATUS_LOAD_IMEM:
		next_fwstatus = FW_STATUS_LOAD_EMEM;
		break;
	case FW_STATUS_LOAD_EMEM:
		next_fwstatus = FW_STATUS_LOAD_DMEM;
		break;
	case FW_STATUS_LOAD_DMEM:
		next_fwstatus = FW_STATUS_READY;
		break;
	default:
		break;
	}

	return next_fwstatus;
}

static u8 _rtl92s_firmware_header_map_rftype(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	switch (rtlphy->rf_type) {
	case RF_1T1R:
		return 0x11;
	case RF_1T2R:
		return 0x12;
	case RF_2T2R:
		return 0x22;
	case RF_2T2R_GREEN:
		return 0x92;
	default:
		pr_err("Unknown RF type(%x)\n", rtlphy->rf_type);
		break;
	}
	return 0x22;
}

static void _rtl92s_firmwareheader_priveupdate(struct ieee80211_hw *hw,
		struct fw_priv *pfw_priv)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));

	/* Update RF types for RATR settings. */
	pfw_priv->rf_config = _rtl92s_firmware_header_map_rftype(hw);
	pfw_priv->hci_sel = rtlhal->interface == INTF_USB ? 2 : 1;
}



static bool _rtl92s_firmware_downloadcode(struct ieee80211_hw *hw,
		u8 *code_virtual_address, u32 buffer_len)
{
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct sk_buff *skb;
	struct rtl_tcb_desc *tcb_desc;
	u16 frag_threshold = MAX_FIRMWARE_CODE_SIZE;
	u16 frag_length, frag_offset = 0;
	u16 extra_descoffset = 0;
	u8 last_inipkt = 0;

	rtl92s_fw_set_rqpn(hw);

	if (buffer_len >= MAX_FIRMWARE_CODE_SIZE) {
		pr_err("Size over FIRMWARE_CODE_SIZE!\n");
		return false;
	}

	extra_descoffset = rtlhal->interface == INTF_USB ?
				RTL_TX_HEADER_SIZE : 0;

	do {
		if ((buffer_len - frag_offset) > frag_threshold) {
			frag_length = frag_threshold + extra_descoffset;
		} else {
			frag_length = (u16)(buffer_len - frag_offset +
					    extra_descoffset);
			last_inipkt = 1;
		}

		/* Allocate skb buffer to contain firmware */
		/* info and tx descriptor info. */
		skb = dev_alloc_skb(frag_length);
		if (!skb)
			return false;
		skb_reserve(skb, extra_descoffset);
		skb_put_data(skb, code_virtual_address + frag_offset,
			     (u32)(frag_length - extra_descoffset));

		tcb_desc = (struct rtl_tcb_desc *)(skb->cb);
		tcb_desc->cmd_or_init = DESC_PACKET_TYPE_INIT;
		tcb_desc->last_inipkt = last_inipkt;

		if (!rtl92s_cmd_send_packet(hw, skb)) {
			kfree_skb(skb);
			return false;
		}

		frag_offset += (frag_length - extra_descoffset);

	} while (frag_offset < buffer_len);

	rtl92s_fw_download_poll(hw);

	return true ;
}

static bool _rtl92s_firmware_checkready(struct ieee80211_hw *hw,
		u8 loadfw_status)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rt_firmware *firmware = (struct rt_firmware *)rtlhal->pfirmware;
	u32 tmpu4b;
	u8 cpustatus = 0;
	short pollingcnt = 1000;
	bool rtstatus = true;

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"LoadStaus(%d)\n", loadfw_status);

	firmware->fwstatus = (enum fw_status)loadfw_status;

	switch (loadfw_status) {
	case FW_STATUS_LOAD_IMEM:
		/* Polling IMEM code done. */
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & IMEM_CODE_DONE)
				break;
			udelay(5);
		} while (pollingcnt--);

		if (!(cpustatus & IMEM_CHK_RPT) || (pollingcnt <= 0)) {
			pr_err("FW_STATUS_LOAD_IMEM FAIL CPU, Status=%x\n",
			       cpustatus);
			rtstatus = false;
			goto status_check_fail;
		}
		break;

	case FW_STATUS_LOAD_EMEM:
		/* Check Put Code OK and Turn On CPU */
		/* Polling EMEM code done. */
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & EMEM_CODE_DONE)
				break;
			udelay(5);
		} while (pollingcnt--);

		if (!(cpustatus & EMEM_CHK_RPT) || (pollingcnt <= 0)) {
			pr_err("FW_STATUS_LOAD_EMEM FAIL CPU, Status=%x\n",
			       cpustatus);
			rtstatus = false;
			goto status_check_fail;
		}

		/* Turn On CPU */
		rtstatus = _rtl92s_firmware_enable_cpu(hw);
		if (!rtstatus) {
			pr_err("Enable CPU fail!\n");
			goto status_check_fail;
		}
		break;

	case FW_STATUS_LOAD_DMEM:
		/* Polling DMEM code done */
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & DMEM_CODE_DONE)
				break;
			udelay(5);
		} while (pollingcnt--);

		if (!(cpustatus & DMEM_CODE_DONE) || (pollingcnt <= 0)) {
			pr_err("Polling DMEM code done fail ! cpustatus(%#x)\n",
			       cpustatus);
			rtstatus = false;
			goto status_check_fail;
		}

		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"DMEM code download success, cpustatus(%#x)\n",
			cpustatus);

		/* Prevent Delay too much and being scheduled out */
		/* Polling Load Firmware ready */
		pollingcnt = 2000;
		do {
			cpustatus = rtl_read_byte(rtlpriv, TCR);
			if (cpustatus & FWRDY)
				break;
			udelay(40);
		} while (pollingcnt--);

		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"Polling Load Firmware ready, cpustatus(%x)\n",
			cpustatus);

		if (((cpustatus & LOAD_FW_READY) != LOAD_FW_READY) ||
		    (pollingcnt <= 0)) {
			pr_err("Polling Load Firmware ready fail ! cpustatus(%x)\n",
			       cpustatus);
			rtstatus = false;
			goto status_check_fail;
		}

		/* If right here, we can set TCR/RCR to desired value  */
		/* and config MAC lookback mode to normal mode */
		tmpu4b = rtl_read_dword(rtlpriv, TCR);
		rtl_write_dword(rtlpriv, TCR, (tmpu4b & (~TCR_ICV)));

		tmpu4b = rtl_read_dword(rtlpriv, RCR);
		rtl_write_dword(rtlpriv, RCR, (tmpu4b | RCR_APPFCS |
				RCR_APP_ICV | RCR_APP_MIC));

		rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
			"Current RCR settings(%#x)\n", tmpu4b);

		/* Set to normal mode. */
		rtl_write_byte(rtlpriv, LBKMD_SEL, LBK_NORMAL);
		break;

	default:
		pr_err("Unknown status check!\n");
		rtstatus = false;
		break;
	}

status_check_fail:
	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"loadfw_status(%d), rtstatus(%x)\n",
		loadfw_status, rtstatus);
	return rtstatus;
}

int rtl92s_download_fw(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rt_firmware *firmware = NULL;
	struct fw_hdr *pfwheader;
	struct fw_priv *pfw_priv = NULL;
	u8 *puc_mappedfile = NULL;
	u32 ul_filelength = 0;
	u8 fwhdr_size = RT_8192S_FIRMWARE_HDR_SIZE;
	u8 fwstatus = FW_STATUS_INIT;
	bool rtstatus = true;

	if (rtlpriv->max_fw_size == 0 || !rtlhal->pfirmware)
		return 0;

	firmware = (struct rt_firmware *)rtlhal->pfirmware;
	if (rtl92s_validate_fw(firmware->sz_fw_tmpbuffer,
			       firmware->sz_fw_tmpbufferlen))
		return 0;
	firmware->fwstatus = FW_STATUS_INIT;

	puc_mappedfile = firmware->sz_fw_tmpbuffer;

	/* 1. Retrieve FW header. */
	firmware->pfwheader = (struct fw_hdr *) puc_mappedfile;
	pfwheader = firmware->pfwheader;
	firmware->firmwareversion = byte(le16_to_cpu(pfwheader->version), 0);

	rtl_dbg(rtlpriv, COMP_INIT, DBG_LOUD,
		"signature:%x, version:%x, size:%x, imemsize:%x, sram size:%x\n",
		le16_to_cpu(pfwheader->signature),
		le16_to_cpu(pfwheader->version), le32_to_cpu(pfwheader->dmem_size),
		le32_to_cpu(pfwheader->img_imem_size),
		le32_to_cpu(pfwheader->img_sram_size));

	/* 2. Retrieve IMEM image. */
	firmware->fw_imem_len = le32_to_cpu(pfwheader->img_imem_size);
	if ((firmware->fw_imem_len == 0) || (firmware->fw_imem_len >
	    sizeof(firmware->fw_imem))) {
		pr_err("memory for data image is less than IMEM required\n");
		goto fail;
	} else {
		puc_mappedfile += fwhdr_size;

		memcpy(firmware->fw_imem, puc_mappedfile,
		       firmware->fw_imem_len);
	}

	/* 3. Retriecve EMEM image. */
	firmware->fw_emem_len = le32_to_cpu(pfwheader->img_sram_size);
	if (firmware->fw_emem_len > sizeof(firmware->fw_emem)) {
		pr_err("memory for data image is less than EMEM required\n");
		goto fail;
	} else {
		puc_mappedfile += firmware->fw_imem_len;

		memcpy(firmware->fw_emem, puc_mappedfile,
		       firmware->fw_emem_len);
	}

	/* 4. download fw now */
	fwstatus = _rtl92s_firmware_get_nextstatus(firmware->fwstatus);
	while (fwstatus != FW_STATUS_READY) {
		/* Image buffer redirection. */
		switch (fwstatus) {
		case FW_STATUS_LOAD_IMEM:
			puc_mappedfile = firmware->fw_imem;
			ul_filelength = firmware->fw_imem_len;
			break;
		case FW_STATUS_LOAD_EMEM:
			puc_mappedfile = firmware->fw_emem;
			ul_filelength = firmware->fw_emem_len;
			break;
		case FW_STATUS_LOAD_DMEM:
			/* Partial update the content of header private. */
			pfwheader = firmware->pfwheader;
			pfw_priv = &pfwheader->fwpriv;
			_rtl92s_firmwareheader_priveupdate(hw, pfw_priv);
			puc_mappedfile = (u8 *)(firmware->pfwheader) +
					RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE;
			ul_filelength = fwhdr_size -
					RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE;
			break;
		default:
			pr_err("Unexpected Download step!!\n");
			goto fail;
		}

		/* <2> Download image file */
		rtstatus = _rtl92s_firmware_downloadcode(hw, puc_mappedfile,
				ul_filelength);

		if (!rtstatus) {
			pr_err("fail!\n");
			goto fail;
		}

		/* <3> Check whether load FW process is ready */
		rtstatus = _rtl92s_firmware_checkready(hw, fwstatus);
		if (!rtstatus) {
			pr_err("rtl8192se: firmware fail!\n");
			goto fail;
		}

		fwstatus = _rtl92s_firmware_get_nextstatus(firmware->fwstatus);
	}

	return rtstatus;
fail:
	return 0;
}
EXPORT_SYMBOL_GPL(rtl92s_download_fw);

int rtl92s_firmware_set_h2c_cmd(struct ieee80211_hw *hw, u32 element_id,
				u32 rsvd, u8 *pcmd_buffer, u32 cmd_len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_tcb_desc *cb_desc;
	struct sk_buff *skb;
	unsigned long flags;
	u32 payload_len;
	u32 desc_len;
	__le32 *header;
	int ret = 0;

	if (element_id > U8_MAX || cmd_len > U16_MAX ||
	    (cmd_len && !pcmd_buffer))
		return -EINVAL;
	if (cmd_len > MAX_TRANSMIT_BUFFER_SIZE - H2C_TX_CMD_HDR_LEN)
		return -EMSGSIZE;

	payload_len = ALIGN(cmd_len, 8);
	desc_len = rtlhal->interface == INTF_USB ? RTL_TX_HEADER_SIZE : 0;
	if (payload_len > MAX_TRANSMIT_BUFFER_SIZE - H2C_TX_CMD_HDR_LEN)
		return -EMSGSIZE;

	spin_lock_irqsave(&rtlpriv->locks.h2c_lock, flags);
	skb = alloc_skb(desc_len + H2C_TX_CMD_HDR_LEN + payload_len,
			GFP_ATOMIC);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}
	skb_reserve(skb, desc_len);
	header = (__le32 *)skb_put(skb, H2C_TX_CMD_HDR_LEN + payload_len);
	memset(header, 0, H2C_TX_CMD_HDR_LEN + payload_len);
	le32p_replace_bits(&header[0], cmd_len, GENMASK(15, 0));
	le32p_replace_bits(&header[0], element_id, GENMASK(23, 16));
	rtlhal->h2c_txcmd_seq %= 0x80;
	le32p_replace_bits(&header[0], rtlhal->h2c_txcmd_seq,
			   GENMASK(30, 24));
	rtlhal->h2c_txcmd_seq++;
	header[1] = cpu_to_le32(rsvd);
	if (cmd_len)
		memcpy(header + 2, pcmd_buffer, cmd_len);

	cb_desc = (struct rtl_tcb_desc *)skb->cb;
	cb_desc->cmd_or_init = DESC_PACKET_TYPE_NORMAL;
	cb_desc->last_inipkt = false;
	if (!rtl92s_cmd_send_packet(hw, skb)) {
		kfree_skb(skb);
		ret = -EIO;
		goto out;
	}
	if (rtlhal->interface != INTF_USB)
		rtlpriv->cfg->ops->tx_polling(hw, RTL92S_TXCMD_QUEUE);
out:
	spin_unlock_irqrestore(&rtlpriv->locks.h2c_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(rtl92s_firmware_set_h2c_cmd);

void rtl92s_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct h2c_set_pwrmode_parm	pwrmode = {0};
	u16 max_wakeup_period = 0;

	pwrmode.mode = mode;
	pwrmode.flag_low_traffic_en = 0;
	pwrmode.flag_lpnav_en = 0;
	pwrmode.flag_rf_low_snr_en = 0;
	pwrmode.flag_dps_en = 0;
	pwrmode.bcn_rx_en = 0;
	pwrmode.bcn_to = 0;
	le16p_replace_bits((__le16 *)(((u8 *)(&pwrmode) + 8)),
			   mac->vif->bss_conf.beacon_int, GENMASK(15, 0));
	pwrmode.app_itv = 0;
	pwrmode.awake_bcn_itvl = ppsc->reg_max_lps_awakeintvl;
	pwrmode.smart_ps = 1;
	pwrmode.bcn_pass_period = 10;

	/* Set beacon pass count */
	if (pwrmode.mode == FW_PS_MIN_MODE)
		max_wakeup_period = mac->vif->bss_conf.beacon_int;
	else if (pwrmode.mode == FW_PS_MAX_MODE)
		max_wakeup_period = mac->vif->bss_conf.beacon_int *
			mac->vif->bss_conf.dtim_period;

	if (max_wakeup_period >= 500)
		pwrmode.bcn_pass_cnt = 1;
	else if ((max_wakeup_period >= 300) && (max_wakeup_period < 500))
		pwrmode.bcn_pass_cnt = 2;
	else if ((max_wakeup_period >= 200) && (max_wakeup_period < 300))
		pwrmode.bcn_pass_cnt = 3;
	else if ((max_wakeup_period >= 20) && (max_wakeup_period < 200))
		pwrmode.bcn_pass_cnt = 5;
	else
		pwrmode.bcn_pass_cnt = 1;

	rtl92s_firmware_set_h2c_cmd(hw, H2C_SETPWRMODE_CMD, 0,
				      (u8 *)&pwrmode, sizeof(pwrmode));

}
EXPORT_SYMBOL_GPL(rtl92s_set_fw_pwrmode_cmd);

void rtl92s_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw,
		u8 mstatus, u8 ps_qosinfo)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct h2c_joinbss_rpt_parm joinbss_rpt = {0};

	joinbss_rpt.opmode = mstatus;
	joinbss_rpt.ps_qos_info = ps_qosinfo;
	joinbss_rpt.bssid[0] = mac->bssid[0];
	joinbss_rpt.bssid[1] = mac->bssid[1];
	joinbss_rpt.bssid[2] = mac->bssid[2];
	joinbss_rpt.bssid[3] = mac->bssid[3];
	joinbss_rpt.bssid[4] = mac->bssid[4];
	joinbss_rpt.bssid[5] = mac->bssid[5];
	le16p_replace_bits((__le16 *)(((u8 *)(&joinbss_rpt) + 8)),
			   mac->vif->bss_conf.beacon_int, GENMASK(15, 0));
	le16p_replace_bits((__le16 *)(((u8 *)(&joinbss_rpt) + 10)),
			   mac->assoc_id, GENMASK(15, 0));

	rtl92s_firmware_set_h2c_cmd(hw, H2C_JOINBSSRPT_CMD, 0,
				      (u8 *)&joinbss_rpt,
				      sizeof(joinbss_rpt));
}
EXPORT_SYMBOL_GPL(rtl92s_set_fw_joinbss_report_cmd);

