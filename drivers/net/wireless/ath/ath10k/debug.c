/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/version.h>
#include <linux/vermagic.h>
#include <linux/vmalloc.h>

#include "core.h"
#include "debug.h"
#include "hif.h"
#include "mac.h"

/* ms */
#define ATH10K_DEBUG_HTT_STATS_INTERVAL 1000

#define ATH10K_DEBUG_POLL_CE_INTERVAL 950 /* ms */
#define ATH10K_DEBUG_NOP_INTERVAL 2000 /* ms */

#define ATH10K_FW_CRASH_DUMP_VERSION 1

/**
 * enum ath10k_fw_crash_dump_type - types of data in the dump file
 * @ATH10K_FW_CRASH_DUMP_REGDUMP: Register crash dump in binary format
 * @ATH10K_FW_ERROR_DUMP_DBGLOG:  Recent firmware debug log entries
 * @ATH10K_FW_CRASH_DUMP_STACK:   Stack memory contents.
 * @ATH10K_FW_CRASH_DUMP_EXC_STACK:   Exception stack memory contents.
 * @ATH10K_FW_CRASH_DUMP_RAM_BSS:  BSS area for RAM code
 * @ATH10K_FW_CRASH_DUMP_ROM_BSS:  BSS area for ROM code
 */
enum ath10k_fw_crash_dump_type {
	ATH10K_FW_CRASH_DUMP_REGISTERS = 0,
	ATH10K_FW_CRASH_DUMP_DBGLOG = 1,
	ATH10K_FW_CRASH_DUMP_STACK = 2,
	ATH10K_FW_CRASH_DUMP_EXC_STACK = 3,
	ATH10K_FW_CRASH_DUMP_RAM_BSS = 4,
	ATH10K_FW_CRASH_DUMP_ROM_BSS = 5,

	ATH10K_FW_CRASH_DUMP_MAX,
};

struct ath10k_tlv_dump_data {
	/* see ath10k_fw_crash_dump_type above */
	__le32 type;

	/* in bytes */
	__le32 tlv_len;

	/* pad to 32-bit boundaries as needed */
	u8 tlv_data[];
} __packed;

struct ath10k_dump_file_data {
	/* dump file information */

	/* "ATH10K-FW-DUMP" */
	char df_magic[16];

	__le32 len;

	/* file dump version */
	__le32 version;

	/* some info we can get from ath10k struct that might help */

	u8 uuid[16];

	__le32 chip_id;

	/* 0 for now, in place for later hardware */
	__le32 bus_type;

	__le32 target_version;
	__le32 fw_version_major;
	__le32 fw_version_minor;
	__le32 fw_version_release;
	__le32 fw_version_build;
	__le32 phy_capability;
	__le32 hw_min_tx_power;
	__le32 hw_max_tx_power;
	__le32 ht_cap_info;
	__le32 vht_cap_info;
	__le32 num_rf_chains;

	/* firmware version string */
	char fw_ver[ETHTOOL_FWVERS_LEN];

	/* Kernel related information */

	/* time-of-day stamp */
	__le64 tv_sec;

	/* time-of-day stamp, nano-seconds */
	__le64 tv_nsec;

	/* LINUX_VERSION_CODE */
	__le32 kernel_ver_code;

	/* VERMAGIC_STRING */
	char kernel_ver[64];

	__le32 stack_addr;
	__le32 exc_stack_addr;
	__le32 rom_bss_addr;
	__le32 ram_bss_addr;

	/* room for growth w/out changing binary format */
	u8 unused[112];

	/* struct ath10k_tlv_dump_data + more */
	u8 data[0];
} __packed;

struct ath10k_dbglog_entry_storage_user {
	__le32 head_idx; /* Where to write next chunk of data */
	__le32 tail_idx; /* Index of first msg */
	__le32 data[ATH10K_DBGLOG_DATA_LEN];
} __packed;

int ath10k_info(struct ath10k *ar, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;
	int ret;

	va_start(args, fmt);
	vaf.va = &args;
	if (ath10k_debug_mask & ATH10K_DBG_INFO_AS_DBG)
		ret = dev_printk(KERN_DEBUG, ar->dev, "%pV", &vaf);
	else
		ret = dev_info(ar->dev, "%pV", &vaf);
	trace_ath10k_log_info(ar, &vaf);
	va_end(args);

	return ret;
}
EXPORT_SYMBOL(ath10k_info);

void ath10k_print_driver_info(struct ath10k *ar)
{
	ath10k_info(ar, "%s (0x%08x, 0x%08x) fw %s api %d htt %d.%d\n",
		    ar->hw_params.name,
		    ar->target_version,
		    ar->chip_id,
		    ar->hw->wiphy->fw_version,
		    ar->fw_api,
		    ar->htt.target_version_major,
		    ar->htt.target_version_minor);
	ath10k_info(ar, "debug %d debugfs %d tracing %d dfs %d testmode %d\n",
		    config_enabled(CONFIG_ATH10K_DEBUG),
		    config_enabled(CONFIG_ATH10K_DEBUGFS),
		    config_enabled(CONFIG_ATH10K_TRACING),
		    config_enabled(CONFIG_ATH10K_DFS_CERTIFIED),
		    config_enabled(CONFIG_NL80211_TESTMODE));
}
EXPORT_SYMBOL(ath10k_print_driver_info);

void ath10k_set_debug_mask(unsigned int v) {
	ath10k_debug_mask = v;
}
EXPORT_SYMBOL(ath10k_set_debug_mask);

int ath10k_err(struct ath10k *ar, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;
	int ret;

	va_start(args, fmt);
	vaf.va = &args;
	ret = dev_err(ar->dev, "%pV", &vaf);
	trace_ath10k_log_err(ar, &vaf);
	va_end(args);

	return ret;
}
EXPORT_SYMBOL(ath10k_err);

int ath10k_warn(struct ath10k *ar, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_warn(ar->dev, "state: %d %pV", ar->state, &vaf);
	trace_ath10k_log_warn(ar, &vaf);

	va_end(args);

	return 0;
}
EXPORT_SYMBOL(ath10k_warn);

#ifdef CONFIG_ATH10K_DEBUGFS

void ath10k_debug_read_service_map(struct ath10k *ar,
				   void *service_map,
				   size_t map_size)
{
	memcpy(ar->debug.wmi_service_bitmap, service_map, map_size);
}

static ssize_t ath10k_read_wmi_services(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char *buf;
	unsigned int len = 0, buf_len = 4096;
	const char *name;
	ssize_t ret_cnt;
	bool enabled;
	int i;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&ar->conf_mutex);

	if (len > buf_len)
		len = buf_len;

	for (i = 0; i < WMI_SERVICE_MAX; i++) {
		enabled = test_bit(i, ar->debug.wmi_service_bitmap);
		name = wmi_service_name(i);

		if (!name) {
			if (enabled)
				len += scnprintf(buf + len, buf_len - len,
						 "%-40s %s (bit %d)\n",
						 "unknown", "enabled", i);

			continue;
		}

		len += scnprintf(buf + len, buf_len - len,
				 "%-40s %s\n",
				 name, enabled ? "enabled" : "-");
	}

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	mutex_unlock(&ar->conf_mutex);

	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_wmi_services = {
	.read = ath10k_read_wmi_services,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath10k_debug_read_target_stats(struct ath10k *ar,
				    struct wmi_stats_event *ev)
{
	u8 *tmp = ev->data;
	struct ath10k_target_stats *stats;
	int num_pdev_stats, num_vdev_stats, num_peer_stats;
	struct wmi_pdev_stats_10x *ps;
	int i;

	spin_lock_bh(&ar->data_lock);

	stats = &ar->debug.target_stats;

	num_pdev_stats = __le32_to_cpu(ev->num_pdev_stats); /* 0 or 1 */
	num_vdev_stats = __le32_to_cpu(ev->num_vdev_stats); /* 0 or max vdevs */
	num_peer_stats = __le32_to_cpu(ev->num_peer_stats); /* 0 or max peers */

	if (__le32_to_cpu(ev->stats_id) == WMI_REQUEST_REGISTER_DUMP) {
		struct ath10k_reg_dump* regdump = (struct ath10k_reg_dump*)(tmp);
		for (i = 0; i < __le16_to_cpu(regdump->count); i++) {
			switch (__le16_to_cpu(regdump->regpair[i].reg_id)) {
			case REG_DUMP_NONE:
				break;
			case MAC_FILTER_ADDR_L32:
				stats->mac_filter_addr_l32 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case MAC_FILTER_ADDR_U16:
				stats->mac_filter_addr_u16 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case DCU_SLOT_TIME:
				stats->dcu_slot_time = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PHY_BB_MODE_SELECT:
				stats->phy_bb_mode_select = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_BSSID_L32:
				stats->pcu_bssid_l32 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_BSSID_U16:
				stats->pcu_bssid_u16 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_BSSID2_L32:
				stats->pcu_bssid_l32 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_BSSID2_U16:
				stats->pcu_bssid_u16 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_STA_ADDR_U16:
				stats->pcu_sta_addr_u16 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case MAC_DMA_CFG:
				stats->mac_dma_cfg = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case MAC_DMA_TXCFG:
				stats->mac_dma_txcfg = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_STA_ADDR_L32:
				stats->pcu_sta_addr_l32 = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PCU_RXFILTER:
				stats->pcu_rxfilter = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case PHY_BB_GEN_CONTROLS:
				stats->phy_bb_gen_controls = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case DMA_IMR:
				stats->dma_imr = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case DMA_TXRX_IMR:
				stats->dma_txrx_imr = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case SW_POWERMODE:
				stats->sw_powermode = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case SW_CHAINMASK:
				stats->sw_chainmask_tx = (__le32_to_cpu(regdump->regpair[i].reg_val) >> 16);
				stats->sw_chainmask_rx = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case SW_OPMODE:
				stats->sw_opmode = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			case SW_RXFILTER:
				stats->sw_rxfilter = __le32_to_cpu(regdump->regpair[i].reg_val);
				break;
			}/* switch */
		}
		goto done;
	}

	if (num_pdev_stats) {
		ps = (struct wmi_pdev_stats_10x *)tmp;

		stats->ch_noise_floor = __le32_to_cpu(ps->chan_nf);
		stats->tx_frame_count = __le32_to_cpu(ps->tx_frame_count);
		stats->rx_frame_count = __le32_to_cpu(ps->rx_frame_count);
		stats->rx_clear_count = __le32_to_cpu(ps->rx_clear_count);
		stats->cycle_count = __le32_to_cpu(ps->cycle_count);
		stats->phy_err_count = __le32_to_cpu(ps->phy_err_count);
		stats->chan_tx_power = __le32_to_cpu(ps->chan_tx_pwr);

		stats->comp_queued = __le32_to_cpu(ps->wal.tx.comp_queued);
		stats->comp_delivered =
			__le32_to_cpu(ps->wal.tx.comp_delivered);
		stats->msdu_enqued = __le32_to_cpu(ps->wal.tx.msdu_enqued);
		stats->mpdu_enqued = __le32_to_cpu(ps->wal.tx.mpdu_enqued);
		stats->wmm_drop = __le32_to_cpu(ps->wal.tx.wmm_drop);
		stats->local_enqued = __le32_to_cpu(ps->wal.tx.local_enqued);
		stats->local_freed = __le32_to_cpu(ps->wal.tx.local_freed);
		stats->hw_queued = __le32_to_cpu(ps->wal.tx.hw_queued);
		stats->hw_reaped = __le32_to_cpu(ps->wal.tx.hw_reaped);
		stats->underrun = __le32_to_cpu(ps->wal.tx.underrun);
		stats->tx_abort = __le32_to_cpu(ps->wal.tx.tx_abort);
		stats->mpdus_requed = __le32_to_cpu(ps->wal.tx.mpdus_requed);
		stats->tx_ko = __le32_to_cpu(ps->wal.tx.tx_ko);
		stats->data_rc = __le32_to_cpu(ps->wal.tx.data_rc);
		stats->self_triggers = __le32_to_cpu(ps->wal.tx.self_triggers);
		stats->sw_retry_failure =
			__le32_to_cpu(ps->wal.tx.sw_retry_failure);
		stats->illgl_rate_phy_err =
			__le32_to_cpu(ps->wal.tx.illgl_rate_phy_err);
		stats->pdev_cont_xretry =
			__le32_to_cpu(ps->wal.tx.pdev_cont_xretry);
		stats->pdev_tx_timeout =
			__le32_to_cpu(ps->wal.tx.pdev_tx_timeout);
		stats->pdev_resets = __le32_to_cpu(ps->wal.tx.pdev_resets);
		stats->phy_underrun = __le32_to_cpu(ps->wal.tx.phy_underrun);
		stats->txop_ovf = __le32_to_cpu(ps->wal.tx.txop_ovf);

		stats->mid_ppdu_route_change =
			__le32_to_cpu(ps->wal.rx.mid_ppdu_route_change);
		stats->status_rcvd = __le32_to_cpu(ps->wal.rx.status_rcvd);
		stats->r0_frags = __le32_to_cpu(ps->wal.rx.r0_frags);
		stats->r1_frags = __le32_to_cpu(ps->wal.rx.r1_frags);
		stats->r2_frags = __le32_to_cpu(ps->wal.rx.r2_frags);
		stats->r3_frags = __le32_to_cpu(ps->wal.rx.r3_frags);
		stats->htt_msdus = __le32_to_cpu(ps->wal.rx.htt_msdus);
		stats->htt_mpdus = __le32_to_cpu(ps->wal.rx.htt_mpdus);
		stats->loc_msdus = __le32_to_cpu(ps->wal.rx.loc_msdus);
		stats->loc_mpdus = __le32_to_cpu(ps->wal.rx.loc_mpdus);
		stats->oversize_amsdu =
			__le32_to_cpu(ps->wal.rx.oversize_amsdu);
		stats->phy_errs = __le32_to_cpu(ps->wal.rx.phy_errs);
		stats->phy_err_drop = __le32_to_cpu(ps->wal.rx.phy_err_drop);
		stats->mpdu_errs = __le32_to_cpu(ps->wal.rx.mpdu_errs);

		if (test_bit(ATH10K_FW_FEATURE_WMI_10X,
			     ar->fw_features)) {
			stats->ack_rx_bad = __le32_to_cpu(ps->ack_rx_bad);
			stats->rts_bad = __le32_to_cpu(ps->rts_bad);
			stats->rts_good = __le32_to_cpu(ps->rts_good);
			stats->fcs_bad = __le32_to_cpu(ps->fcs_bad);
			stats->no_beacons = __le32_to_cpu(ps->no_beacons);
			stats->mib_int_count = __le32_to_cpu(ps->mib_int_count);
			tmp += sizeof(struct wmi_pdev_stats_10x);
		} else {
			tmp += sizeof(struct wmi_pdev_stats_old);
		}
	}

	/* 0 or max vdevs */
	/* Currently firmware does not support VDEV stats */
	if (num_vdev_stats) {
		struct wmi_vdev_stats *vdev_stats;

		for (i = 0; i < num_vdev_stats; i++) {
			vdev_stats = (struct wmi_vdev_stats *)tmp;
			tmp += sizeof(struct wmi_vdev_stats);
		}
	}

	if (num_peer_stats) {
		struct wmi_peer_stats_10x *peer_stats;
		struct ath10k_peer_stat *s;

		stats->peers = num_peer_stats;

		for (i = 0; i < num_peer_stats; i++) {
			peer_stats = (struct wmi_peer_stats_10x *)tmp;
			s = &stats->peer_stat[i];

			memcpy(s->peer_macaddr, &peer_stats->peer_macaddr.addr,
			       ETH_ALEN);
			s->peer_rssi = __le32_to_cpu(peer_stats->peer_rssi);
			s->peer_tx_rate =
				__le32_to_cpu(peer_stats->peer_tx_rate);
			if (test_bit(ATH10K_FW_FEATURE_WMI_10X,
				     ar->fw_features)) {
				s->peer_rx_rate =
					__le32_to_cpu(peer_stats->peer_rx_rate);
				tmp += sizeof(struct wmi_peer_stats_10x);

			} else {
				tmp += sizeof(struct wmi_peer_stats_old);
			}
		}
	}

done:
	spin_unlock_bh(&ar->data_lock);
	complete(&ar->debug.event_stats_compl);
}

int ath10k_refresh_peer_stats_t(struct ath10k *ar, int type)
{
	int ret = ath10k_wmi_request_stats(ar, type);
	if (ret) {
		ath10k_warn(ar, "could not request stats (type %d ret %d)\n",
			    type, ret);
		return ret;
	}

	ret = wait_for_completion_timeout(&ar->debug.event_stats_compl, 1*HZ);
	if (ret <= 0)
		return ret;

	return 0;
}

int ath10k_refresh_peer_stats(struct ath10k *ar)
{
	return ath10k_refresh_peer_stats_t(ar, WMI_REQUEST_PEER_STAT);
}

int ath10k_refresh_target_regs(struct ath10k *ar)
{
	if (test_bit(ATH10K_FW_FEATURE_WMI_10X_CT,
		     ar->fw_features))
		return ath10k_refresh_peer_stats_t(ar, WMI_REQUEST_REGISTER_DUMP);
	return 0; /* fail silently if firmware does not support this option. */
}


static ssize_t ath10k_read_fw_regs(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	struct ath10k_target_stats *fw_regs;
	char *buf = NULL;
	unsigned int len = 0, buf_len = 8000;
	ssize_t ret_cnt = 0;
	int ret;

	fw_regs = &ar->debug.target_stats;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON)
		goto exit;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		goto exit;

	ret = ath10k_refresh_target_regs(ar);
	if (ret)
		goto exit;

	spin_lock_bh(&ar->data_lock);
	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k Target Register Dump");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "MAC-FILTER-ADDR-L32", fw_regs->mac_filter_addr_l32);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "MAC-FILTER-ADDR-U16", fw_regs->mac_filter_addr_u16);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "DCU-SLOT-TIME", fw_regs->dcu_slot_time);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PHY-MODE-SELECT", fw_regs->phy_bb_mode_select);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PHY-BB-GEN-CONTROLS", fw_regs->phy_bb_gen_controls);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "DMA-IMR", fw_regs->dma_imr);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "DMA-TXRX-IMR", fw_regs->dma_txrx_imr);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PCU-BSSID-L32", fw_regs->pcu_bssid_l32);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PCU-BSSID-U16", fw_regs->pcu_bssid_u16);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PCU-BSSID2-L32", fw_regs->pcu_bssid2_l32);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PCU-BSSID2-U16", fw_regs->pcu_bssid2_u16);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PCU-STA-ADDR-L32", fw_regs->pcu_sta_addr_l32);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "PCU-STA-ADDR-U16", fw_regs->pcu_sta_addr_u16);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "MAC-DMA-CFG", fw_regs->mac_dma_cfg);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "MAC-DMA-TXCFG", fw_regs->mac_dma_txcfg);

	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "SW-POWERMODE", fw_regs->sw_powermode);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "SW-CHAINMASK-TX", (u32)(fw_regs->sw_chainmask_tx));
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "SW-CHAINMASK-RX", (u32)(fw_regs->sw_chainmask_rx));
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "SW-OPMODE", fw_regs->sw_opmode);

	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "MAC-PCU-RXFILTER", fw_regs->pcu_rxfilter);
	len += scnprintf(buf + len, buf_len - len, "%30s 0x%08x\n",
			 "SW-RXFILTER", fw_regs->sw_rxfilter);

	spin_unlock_bh(&ar->data_lock);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

exit:
	mutex_unlock(&ar->conf_mutex);
	kfree(buf);
	return ret_cnt;
}


static ssize_t ath10k_read_fw_stats(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	struct ath10k_target_stats *fw_stats;
	char *buf = NULL;
	unsigned int len = 0, buf_len = 8000;
	ssize_t ret_cnt = 0;
	int i;
	int ret;

	fw_stats = &ar->debug.target_stats;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_ON)
		goto exit;

	buf = kzalloc(buf_len, GFP_KERNEL);
	if (!buf)
		goto exit;

	ret = ath10k_refresh_peer_stats(ar);
	if (ret)
		goto exit;

	spin_lock_bh(&ar->data_lock);
	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Channel noise floor", fw_stats->ch_noise_floor);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Channel TX power", fw_stats->chan_tx_power);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "TX frame count", fw_stats->tx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RX frame count", fw_stats->rx_frame_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RX clear count", fw_stats->rx_clear_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "Cycle count", fw_stats->cycle_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "PHY error count", fw_stats->phy_err_count);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RTS bad count", fw_stats->rts_bad);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "RTS good count", fw_stats->rts_good);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "FCS bad count", fw_stats->fcs_bad);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "No beacon count", fw_stats->no_beacons);
	len += scnprintf(buf + len, buf_len - len, "%30s %10u\n",
			 "MIB int count", fw_stats->mib_int_count);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV TX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies queued", fw_stats->comp_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HTT cookies disp.", fw_stats->comp_delivered);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDU queued", fw_stats->msdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU queued", fw_stats->mpdu_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs dropped", fw_stats->wmm_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local enqued", fw_stats->local_enqued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Local freed", fw_stats->local_freed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW queued", fw_stats->hw_queued);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs reaped", fw_stats->hw_reaped);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Num underruns", fw_stats->underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PPDUs cleaned", fw_stats->tx_abort);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs requed", fw_stats->mpdus_requed);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Excessive retries", fw_stats->tx_ko);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "HW rate", fw_stats->data_rc);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Sched self tiggers", fw_stats->self_triggers);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Dropped due to SW retries",
			 fw_stats->sw_retry_failure);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Illegal rate phy errors",
			 fw_stats->illgl_rate_phy_err);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Pdev continous xretry", fw_stats->pdev_cont_xretry);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "TX timeout", fw_stats->pdev_tx_timeout);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PDEV resets", fw_stats->pdev_resets);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY underrun", fw_stats->phy_underrun);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU is more than txop limit", fw_stats->txop_ovf);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s\n",
			 "ath10k PDEV RX stats");
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Mid PPDU route change",
			 fw_stats->mid_ppdu_route_change);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Tot. number of statuses", fw_stats->status_rcvd);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 0", fw_stats->r0_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 1", fw_stats->r1_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 2", fw_stats->r2_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Extra frags on rings 3", fw_stats->r3_frags);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to HTT", fw_stats->htt_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to HTT", fw_stats->htt_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MSDUs delivered to stack", fw_stats->loc_msdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDUs delivered to stack", fw_stats->loc_mpdus);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "Oversized AMSUs", fw_stats->oversize_amsdu);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors", fw_stats->phy_errs);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "PHY errors drops", fw_stats->phy_err_drop);
	len += scnprintf(buf + len, buf_len - len, "%30s %10d\n",
			 "MPDU errors (FCS, MIC, ENC)", fw_stats->mpdu_errs);

	len += scnprintf(buf + len, buf_len - len, "\n");
	len += scnprintf(buf + len, buf_len - len, "%30s (%d)\n",
			 "ath10k PEER stats", fw_stats->peers);
	len += scnprintf(buf + len, buf_len - len, "%30s\n\n",
				 "=================");

	for (i = 0; i < fw_stats->peers; i++) {
		len += scnprintf(buf + len, buf_len - len, "%30s %pM\n",
				 "Peer MAC address",
				 fw_stats->peer_stat[i].peer_macaddr);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer RSSI", fw_stats->peer_stat[i].peer_rssi);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer TX rate",
				 fw_stats->peer_stat[i].peer_tx_rate);
		len += scnprintf(buf + len, buf_len - len, "%30s %u\n",
				 "Peer RX rate",
				 fw_stats->peer_stat[i].peer_rx_rate);
		len += scnprintf(buf + len, buf_len - len, "\n");
	}
	spin_unlock_bh(&ar->data_lock);

	if (len > buf_len)
		len = buf_len;

	ret_cnt = simple_read_from_buffer(user_buf, count, ppos, buf, len);

exit:
	mutex_unlock(&ar->conf_mutex);
	kfree(buf);
	return ret_cnt;
}

static const struct file_operations fops_fw_stats = {
	.read = ath10k_read_fw_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/* This is a clean assert crash in firmware. */
static int ath10k_debug_fw_assert(struct ath10k *ar)
{
	struct wmi_vdev_install_key_cmd *cmd;
	struct sk_buff *skb;

	skb = ath10k_wmi_alloc_skb(ar, sizeof(*cmd) + 16);
	if (!skb)
		return -ENOMEM;

	cmd = (struct wmi_vdev_install_key_cmd *)skb->data;
	memset(cmd, 0, sizeof(*cmd));

	/* big enough number so that firmware asserts */
	cmd->vdev_id = __cpu_to_le32(0x7ffe);

	return ath10k_wmi_cmd_send(ar, skb,
				   ar->wmi.cmd->vdev_install_key_cmdid);
}

static const struct file_operations fops_fw_regs = {
	.read = ath10k_read_fw_regs,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_simulate_fw_crash(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	const char buf[] =
		"To simulate firmware crash write one of the keywords to this file:\n"
		"`soft` - this will send WMI_FORCE_FW_HANG_ASSERT to firmware if FW supports that command.\n"
		"`hard` - this will send to firmware command with illegal parameters causing firmware crash.\n"
		"`assert` - this will send special illegal parameter to firmware to cause assert failure and crash.\n";

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

/* Simulate firmware crash:
 * 'soft': Call wmi command causing firmware hang. This firmware hang is
 * recoverable by warm firmware reset.
 * 'hard': Force firmware crash by setting any vdev parameter for not allowed
 * vdev id. This is hard firmware crash because it is recoverable only by cold
 * firmware reset.
 */
static ssize_t ath10k_write_simulate_fw_crash(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	int ret;

	mutex_lock(&ar->conf_mutex);

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_RESTARTED) {
		ret = -ENETDOWN;
		goto exit;
	}

	/* drop the possible '\n' from the end */
	if (buf[count - 1] == '\n') {
		buf[count - 1] = 0;
		count--;
	}

	if (!strcmp(buf, "soft")) {
		ath10k_info(ar, "simulating soft firmware crash\n");
		ret = ath10k_wmi_force_fw_hang(ar, WMI_FORCE_FW_HANG_ASSERT, 0);
	} else if (!strcmp(buf, "hard")) {
		ath10k_info(ar, "simulating hard firmware crash\n");
		/* 0x7fff is vdev id, and it is always out of range for all
		 * firmware variants in order to force a firmware crash.
		 */
		ret = ath10k_wmi_vdev_set_param(ar, 0x7fff,
						ar->wmi.vdev_param->rts_threshold,
						0);
	} else if (!strcmp(buf, "assert")) {
		ath10k_info(ar, "simulating firmware assert crash\n");
		ret = ath10k_debug_fw_assert(ar);
	} else {
		ret = -EINVAL;
		goto exit;
	}

	if (ret) {
		ath10k_warn(ar, "failed to simulate firmware crash: %d\n", ret);
		goto exit;
	}

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_simulate_fw_crash = {
	.read = ath10k_read_simulate_fw_crash,
	.write = ath10k_write_simulate_fw_crash,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_debug_level(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	int sz;
	const char buf[] =
		"To change debug level, set value adding up desired flags:\n"
		"PCI:                0x1\n"
		"WMI:                0x2\n"
		"HTC:                0x4\n"
		"HTT:                0x8\n"
		"MAC:               0x10\n"
		"BOOT:              0x20\n"
		"PCI-DUMP:          0x40\n"
		"HTT-DUMP:          0x80\n"
		"MGMT:             0x100\n"
		"DATA:             0x200\n"
		"BMI:              0x400\n"
		"REGULATORY:       0x800\n"
		"TESTMODE:        0x1000\n"
		"INFO-AS-DBG: 0x40000000\n"
		"FW:          0x80000000\n"
		"ALL:         0xFFFFFFFF\n";
	char wbuf[sizeof(buf) + 60];
	sz = snprintf(wbuf, sizeof(wbuf), "Current debug level: 0x%x\n\n%s",
		      ath10k_debug_mask, buf);
	wbuf[sizeof(wbuf) - 1] = 0;

	return simple_read_from_buffer(user_buf, count, ppos, wbuf, sz);
}

/* Set logging level.
 */
static ssize_t ath10k_write_debug_level(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	int ret;
	unsigned long mask;

	ret = kstrtoul_from_user(user_buf, count, 0, &mask);
	if (ret)
		return ret;

	ath10k_warn(ar, "Setting debug-mask to: 0x%lx  old: 0x%x\n",
		    mask, ath10k_debug_mask);
	ath10k_debug_mask = mask;
	return count;
}

static const struct file_operations fops_debug_level = {
	.read = ath10k_read_debug_level,
	.write = ath10k_write_debug_level,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_set_rates(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	const char buf[] =
		"To set unicast, beacon/mgt, multicast, and broadcast,\n"
		"select a type below and then use 'iw' as normal to set\n"
		"the desired rate.\n"
		"beacon   # Beacons and management frames\n"
		"bcast    # Broadcast frames\n"
		"mcast    # Multicast frames\n"
		"ucast    # Unicast frames (normal traffic, default)\n";

	char tmpbuf[strlen(buf) + 80];
	char* str = "ucast";

	if (ar->set_rate_type == ar->wmi.vdev_param->mgmt_rate) {
		str = "beacon";
	}
	else if (ar->set_rate_type == ar->wmi.vdev_param->bcast_data_rate) {
		str = "bcast";
	}
	else if (ar->set_rate_type == ar->wmi.vdev_param->mcast_data_rate) {
		str = "mcast";
	}
	sprintf(tmpbuf, "%sCurrent: %s\n", buf, str);

	return simple_read_from_buffer(user_buf, count, ppos, tmpbuf, strlen(tmpbuf));
}

/* Set the rates for specific types of traffic.
 */
static ssize_t ath10k_write_set_rates(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	int ret;

	mutex_lock(&ar->conf_mutex);

	memset(buf, 0, sizeof(buf));

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

	/* drop the possible '\n' from the end */
	if (buf[count - 1] == '\n')
		buf[count - 1] = 0;

	/* Ignore empty lines, 'echo' appends them sometimes at least. */
	if (buf[0] == 0) {
		ret = count;
		goto exit;
	}

	if (ar->state != ATH10K_STATE_ON &&
	    ar->state != ATH10K_STATE_RESTARTED) {
		ret = -ENETDOWN;
		goto exit;
	}

	if (strncmp(buf, "beacon", strlen("beacon")) == 0) {
		ar->set_rate_type = ar->wmi.vdev_param->mgmt_rate;
	}
	else if (strncmp(buf, "bcast", strlen("bcast")) == 0) {
		ar->set_rate_type = ar->wmi.vdev_param->bcast_data_rate;
	}
	else if (strncmp(buf, "mcast", strlen("mcast")) == 0) {
		ar->set_rate_type = ar->wmi.vdev_param->mcast_data_rate;
	}
	else if (strncmp(buf, "ucast", strlen("ucast")) == 0) {
		ar->set_rate_type = 0;
	}
	else {
		ath10k_warn(ar, "set-rate, invalid rate type: %s  count: %d  %02hx:%02hx:%02hx:%02hx\n",
			    buf, (int)count, (int)(buf[0]), (int)(buf[1]), (int)(buf[2]), (int)(buf[3]));
		ret = -EINVAL;
		goto exit;
	}
	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static const struct file_operations fops_set_rates = {
	.read = ath10k_read_set_rates,
	.write = ath10k_write_set_rates,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_chip_id(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned int len;
	char buf[50];

	len = scnprintf(buf, sizeof(buf), "0x%08x\n", ar->chip_id);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations fops_chip_id = {
	.read = ath10k_read_chip_id,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

struct ath10k_fw_crash_data *
ath10k_debug_get_new_fw_crash_data(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->debug.fw_crash_data;

	lockdep_assert_held(&ar->data_lock);

	uuid_le_gen(&crash_data->uuid);
	getnstimeofday(&crash_data->timestamp);

	return crash_data;
}
EXPORT_SYMBOL(ath10k_debug_get_new_fw_crash_data);

static void ath10k_dbg_drop_dbg_buffer(struct ath10k *ar)
{
	/* Find next message boundary */
	u32 lg_hdr;
	int acnt;
	int tail_idx = ar->debug.dbglog_entry_data.tail_idx;
	int h_idx = (tail_idx + 1) % ATH10K_DBGLOG_DATA_LEN;

	lockdep_assert_held(&ar->data_lock);

	/* Log header is second 32-bit word */
	lg_hdr = le32_to_cpu(ar->debug.dbglog_entry_data.data[h_idx]);

	acnt = (lg_hdr & DBGLOG_NUM_ARGS_MASK) >> DBGLOG_NUM_ARGS_OFFSET;

	if (acnt > DBGLOG_NUM_ARGS_MAX) {
		/* Some sort of corruption it seems, recover as best we can. */
		ath10k_err(ar, "invalid dbglog arg-count: %i %i %i\n",
			   acnt, ar->debug.dbglog_entry_data.tail_idx,
			   ar->debug.dbglog_entry_data.head_idx);
		ar->debug.dbglog_entry_data.tail_idx =
			ar->debug.dbglog_entry_data.head_idx;
		return;
	}

	/* Move forward over the args and the two header fields */
	ar->debug.dbglog_entry_data.tail_idx =
		(tail_idx + acnt + 2) % ATH10K_DBGLOG_DATA_LEN;
}

void ath10k_dbg_save_fw_dbg_buffer(struct ath10k *ar, __le32 *buffer, int len)
{
	int i;
	int z;

	lockdep_assert_held(&ar->data_lock);

	z = ar->debug.dbglog_entry_data.head_idx;

	/* Don't save any new logs until user-space reads this. */
	if (ar->debug.fw_crash_data &&
	    ar->debug.fw_crash_data->crashed_since_read) {
		ath10k_warn(ar, "dropping dbg buffer due to crash since read\n");
		return;
	}

	for (i = 0; i < len; i++) {
		ar->debug.dbglog_entry_data.data[z] = buffer[i];
		z++;
		if (z >= ATH10K_DBGLOG_DATA_LEN)
			z = 0;

		/* If we are about to over-write an old message, move the
		 * tail_idx to the next message.  If idx's are same, we
		 * are empty.
		 */
		if (z == ar->debug.dbglog_entry_data.tail_idx)
			ath10k_dbg_drop_dbg_buffer(ar);

		ar->debug.dbglog_entry_data.head_idx = z;
	}
}
EXPORT_SYMBOL(ath10k_dbg_save_fw_dbg_buffer);

static struct ath10k_dump_file_data *ath10k_build_dump_file(struct ath10k *ar)
{
	struct ath10k_fw_crash_data *crash_data = ar->debug.fw_crash_data;
	struct ath10k_dump_file_data *dump_data;
	struct ath10k_tlv_dump_data *dump_tlv;
	struct ath10k_dbglog_entry_storage_user *dbglog_storage;
	int hdr_len = sizeof(*dump_data);
	unsigned int len, sofar = 0;
	unsigned char *buf;
	int tmp;

	BUILD_BUG_ON(sizeof(struct ath10k_dbglog_entry_storage) !=
		     sizeof(struct ath10k_dbglog_entry_storage_user));

	len = hdr_len;
	len += sizeof(*dump_tlv) + sizeof(crash_data->registers);
	len += sizeof(*dump_tlv) + sizeof(ar->debug.dbglog_entry_data);
	len += sizeof(*dump_tlv) + sizeof(crash_data->stack_buf);
	len += sizeof(*dump_tlv) + sizeof(crash_data->exc_stack_buf);

	if (ar->fw.ram_bss_addr && ar->fw.ram_bss_len)
		len += sizeof(*dump_tlv) + ar->fw.ram_bss_len;

	if (ar->fw.rom_bss_addr && ar->fw.rom_bss_len)
		len += sizeof(*dump_tlv) + ar->fw.rom_bss_len;

	sofar += hdr_len;

	/* This is going to get big when we start dumping FW RAM and such,
	 * so go ahead and use vmalloc.
	 */
	buf = vzalloc(len);
	if (!buf)
		return NULL;

	spin_lock_bh(&ar->data_lock);

	if (!crash_data->crashed_since_read) {
		spin_unlock_bh(&ar->data_lock);
		vfree(buf);
		return NULL;
	}

	dump_data = (struct ath10k_dump_file_data *)(buf);
	strlcpy(dump_data->df_magic, "ATH10K-FW-DUMP",
		sizeof(dump_data->df_magic));
	dump_data->len = cpu_to_le32(len);

	dump_data->version = cpu_to_le32(ATH10K_FW_CRASH_DUMP_VERSION);

	memcpy(dump_data->uuid, &crash_data->uuid, sizeof(dump_data->uuid));
	dump_data->chip_id = cpu_to_le32(ar->chip_id);
	dump_data->bus_type = cpu_to_le32(0);
	dump_data->target_version = cpu_to_le32(ar->target_version);
	dump_data->fw_version_major = cpu_to_le32(ar->fw_version_major);
	dump_data->fw_version_minor = cpu_to_le32(ar->fw_version_minor);
	dump_data->fw_version_release = cpu_to_le32(ar->fw_version_release);
	dump_data->fw_version_build = cpu_to_le32(ar->fw_version_build);
	dump_data->phy_capability = cpu_to_le32(ar->phy_capability);
	dump_data->hw_min_tx_power = cpu_to_le32(ar->hw_min_tx_power);
	dump_data->hw_max_tx_power = cpu_to_le32(ar->hw_max_tx_power);
	dump_data->ht_cap_info = cpu_to_le32(ar->ht_cap_info);
	dump_data->vht_cap_info = cpu_to_le32(ar->vht_cap_info);
	dump_data->num_rf_chains = cpu_to_le32(ar->num_rf_chains);
	dump_data->stack_addr = cpu_to_le32(crash_data->stack_addr);
	dump_data->exc_stack_addr = cpu_to_le32(crash_data->exc_stack_addr);
	dump_data->rom_bss_addr = cpu_to_le32(ar->fw.rom_bss_addr);
	dump_data->ram_bss_addr = cpu_to_le32(ar->fw.ram_bss_addr);

	strlcpy(dump_data->fw_ver, ar->hw->wiphy->fw_version,
		sizeof(dump_data->fw_ver));

	dump_data->kernel_ver_code = cpu_to_le32(LINUX_VERSION_CODE);
	strlcpy(dump_data->kernel_ver, VERMAGIC_STRING,
		sizeof(dump_data->kernel_ver));

	dump_data->tv_sec = cpu_to_le64(crash_data->timestamp.tv_sec);
	dump_data->tv_nsec = cpu_to_le64(crash_data->timestamp.tv_nsec);

	/* Gather crash-dump */
	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_REGISTERS);
	dump_tlv->tlv_len = cpu_to_le32(sizeof(crash_data->registers));
	memcpy(dump_tlv->tlv_data, &crash_data->registers,
	       sizeof(crash_data->registers));
	sofar += sizeof(*dump_tlv) + sizeof(crash_data->registers);

	/* Gather dbg-log */
	tmp = sizeof(ar->debug.dbglog_entry_data);
	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_DBGLOG);
	dump_tlv->tlv_len = cpu_to_le32(tmp);
	dbglog_storage =
		(struct ath10k_dbglog_entry_storage_user *)(dump_tlv->tlv_data);
	memcpy(dbglog_storage->data, ar->debug.dbglog_entry_data.data,
	       sizeof(dbglog_storage->data));
	dbglog_storage->head_idx =
		cpu_to_le32(ar->debug.dbglog_entry_data.head_idx);
	dbglog_storage->tail_idx =
		cpu_to_le32(ar->debug.dbglog_entry_data.tail_idx);
	sofar += sizeof(*dump_tlv) + tmp;

	/* Gather firmware stack dump */
	tmp = sizeof(crash_data->stack_buf);
	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_STACK);
	dump_tlv->tlv_len = cpu_to_le32(tmp);
	memcpy(dump_tlv->tlv_data, crash_data->stack_buf, tmp);
	sofar += sizeof(*dump_tlv) + tmp;

	/* Gather firmware exception stack dump */
	tmp = sizeof(crash_data->exc_stack_buf);
	dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
	dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_EXC_STACK);
	dump_tlv->tlv_len = cpu_to_le32(tmp);
	memcpy(dump_tlv->tlv_data, crash_data->exc_stack_buf, tmp);
	sofar += sizeof(*dump_tlv) + tmp;

	if (ar->fw.ram_bss_addr && ar->fw.ram_bss_len) {
		tmp = ar->fw.ram_bss_len;
		dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
		dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_RAM_BSS);
		dump_tlv->tlv_len = cpu_to_le32(tmp);
		memcpy(dump_tlv->tlv_data, crash_data->ram_bss_buf, tmp);
		sofar += sizeof(*dump_tlv) + tmp;
	}

	if (ar->fw.rom_bss_addr && ar->fw.rom_bss_len) {
		tmp = ar->fw.rom_bss_len;
		dump_tlv = (struct ath10k_tlv_dump_data *)(buf + sofar);
		dump_tlv->type = cpu_to_le32(ATH10K_FW_CRASH_DUMP_ROM_BSS);
		dump_tlv->tlv_len = cpu_to_le32(tmp);
		memcpy(dump_tlv->tlv_data, crash_data->rom_bss_buf, tmp);
		sofar += sizeof(*dump_tlv) + tmp;
	}

	ar->debug.fw_crash_data->crashed_since_read = false;

	WARN_ON(sofar != len);
	spin_unlock_bh(&ar->data_lock);

	return dump_data;
}

static int ath10k_fw_crash_dump_open(struct inode *inode, struct file *file)
{
	struct ath10k *ar = inode->i_private;
	struct ath10k_dump_file_data *dump;

	dump = ath10k_build_dump_file(ar);
	if (!dump)
		return -ENODATA;

	file->private_data = dump;

	return 0;
}

static ssize_t ath10k_fw_crash_dump_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ath10k_dump_file_data *dump_file = file->private_data;

	return simple_read_from_buffer(user_buf, count, ppos,
				       dump_file,
				       le32_to_cpu(dump_file->len));
}

static int ath10k_fw_crash_dump_release(struct inode *inode,
					struct file *file)
{
	vfree(file->private_data);

	return 0;
}

static const struct file_operations fops_fw_crash_dump = {
	.open = ath10k_fw_crash_dump_open,
	.read = ath10k_fw_crash_dump_read,
	.release = ath10k_fw_crash_dump_release,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static int ath10k_debug_htt_stats_req(struct ath10k *ar)
{
	u64 cookie;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	if (ar->debug.htt_stats_mask == 0)
		/* htt stats are disabled */
		return 0;

	if (ar->state != ATH10K_STATE_ON)
		return 0;

	cookie = get_jiffies_64();

	ret = ath10k_htt_h2t_stats_req(&ar->htt, ar->debug.htt_stats_mask,
				       cookie);
	if (ret) {
		ath10k_warn(ar, "failed to send htt stats request: %d\n", ret);
		return ret;
	}

	queue_delayed_work(ar->workqueue, &ar->debug.htt_stats_dwork,
			   msecs_to_jiffies(ATH10K_DEBUG_HTT_STATS_INTERVAL));

	return 0;
}

static void ath10k_debug_htt_stats_dwork(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k,
					 debug.htt_stats_dwork.work);

	mutex_lock(&ar->conf_mutex);

	ath10k_debug_htt_stats_req(ar);

	mutex_unlock(&ar->conf_mutex);
}

#if 0
static void ath10k_debug_poll_ce_dwork(struct work_struct *work)
{
	/* Seems we may miss IRQs locally as well when firmware gets
	 * stuck, so force a poll our own CE rings as well.
	 */
	struct ath10k *ar = container_of(work, struct ath10k,
					 debug.poll_ce_dwork.work);
	ath10k_hif_force_poll_ce(ar);
}
#endif

static void ath10k_debug_nop_dwork(struct work_struct *work)
{
	struct ath10k *ar = container_of(work, struct ath10k,
					 debug.nop_dwork.work);

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH10K_STATE_ON) {
		int ret = ath10k_wmi_request_nop(ar);
		if (ret) {
			ath10k_warn(ar, "failed to send wmi nop: %d\n", ret);
		}
	}

	/* Re-arm periodic work. */
	queue_delayed_work(ar->workqueue, &ar->debug.nop_dwork,
			   msecs_to_jiffies(ATH10K_DEBUG_NOP_INTERVAL));

	mutex_unlock(&ar->conf_mutex);
}

static ssize_t ath10k_read_htt_stats_mask(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[32];
	unsigned int len;

	len = scnprintf(buf, sizeof(buf), "%lu\n", ar->debug.htt_stats_mask);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_htt_stats_mask(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned long mask;
	int ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &mask);
	if (ret)
		return ret;

	/* max 8 bit masks (for now) */
	if (mask > 0xff)
		return -E2BIG;

	mutex_lock(&ar->conf_mutex);

	ar->debug.htt_stats_mask = mask;

	ret = ath10k_debug_htt_stats_req(ar);
	if (ret)
		goto out;

	ret = count;

out:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static const struct file_operations fops_htt_stats_mask = {
	.read = ath10k_read_htt_stats_mask,
	.write = ath10k_write_htt_stats_mask,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_htt_max_amsdu_ampdu(struct file *file,
					       char __user *user_buf,
					       size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	char buf[64];
	u8 amsdu = 3, ampdu = 64;
	unsigned int len;

	mutex_lock(&ar->conf_mutex);

	if (ar->debug.htt_max_amsdu)
		amsdu = ar->debug.htt_max_amsdu;

	if (ar->debug.htt_max_ampdu)
		ampdu = ar->debug.htt_max_ampdu;

	mutex_unlock(&ar->conf_mutex);

	len = scnprintf(buf, sizeof(buf), "%u %u\n", amsdu, ampdu);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_htt_max_amsdu_ampdu(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	int res;
	char buf[64];
	unsigned int amsdu, ampdu;

	simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, user_buf, count);

	/* make sure that buf is null terminated */
	buf[sizeof(buf) - 1] = 0;

	res = sscanf(buf, "%u %u", &amsdu, &ampdu);

	if (res != 2)
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);

	res = ath10k_htt_h2t_aggr_cfg_msg(&ar->htt, ampdu, amsdu);
	if (res)
		goto out;

	res = count;
	ar->debug.htt_max_amsdu = amsdu;
	ar->debug.htt_max_ampdu = ampdu;

out:
	mutex_unlock(&ar->conf_mutex);
	return res;
}

static const struct file_operations fops_htt_max_amsdu_ampdu = {
	.read = ath10k_read_htt_max_amsdu_ampdu,
	.write = ath10k_write_htt_max_amsdu_ampdu,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_read_fw_dbglog(struct file *file,
				     char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned int len;
	char buf[32];

	len = scnprintf(buf, sizeof(buf), "0x%08x\n",
			ar->debug.fw_dbglog_mask);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t ath10k_write_fw_dbglog(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	unsigned long mask;
	int ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &mask);
	if (ret)
		return ret;

	mutex_lock(&ar->conf_mutex);

	ar->debug.fw_dbglog_mask = mask;

	if (ar->state == ATH10K_STATE_ON) {
		ret = ath10k_wmi_dbglog_cfg(ar, ar->debug.fw_dbglog_mask);
		if (ret) {
			ath10k_warn(ar, "dbglog cfg failed from debugfs: %d\n",
				    ret);
			goto exit;
		}
	}

	ret = count;

exit:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

#ifdef CONFIG_ATH10K_DEBUGFS
/* TODO:  Would be nice to always support ethtool stats, would need to
 * move the stats storage out of ath10k_debug, or always have ath10k_debug
 * struct available..
 */

/* This generally cooresponds to the debugfs fw_stats file */
static const char ath10k_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_pkts_nic",
	"tx_bytes_nic",
	"rx_pkts_nic",
	"rx_bytes_nic",
	"d_noise_floor",
	"d_cycle_count", /* this is duty cycle counter, basically channel-time. 88MHz clock */
	"d_tx_cycle_count", /* tx cycle count */
	"d_rx_cycle_count", /* rx cycle count */
	"d_busy_count", /* Total channel busy time cycles (called 'clear' by firmware) */
	"d_flags", /* 0x1:  hw has shifted cycle-count wrap, see ath10k_hw_fill_survey_time */
	"d_phy_error",
	"d_rts_bad",
	"d_rts_good",
	"d_tx_power", /* in .5 dbM I think */
	"d_rx_crc_err", /* fcs_bad */
	"d_no_beacon",
	"d_tx_mpdus_queued",
	"d_tx_msdu_queued",
	"d_tx_msdu_dropped",
	"d_local_enqued",
	"d_local_freed",
	"d_tx_ppdu_hw_queued",
	"d_tx_ppdu_reaped",
	"d_tx_fifo_underrun",
	"d_tx_ppdu_abort",
	"d_tx_mpdu_requed",
	"d_tx_excessive_retries",
	"d_tx_hw_rate",
	"d_tx_dropped_sw_retries",
	"d_tx_illegal_rate",
	"d_tx_continuous_xretries",
	"d_tx_timeout",
	"d_tx_mpdu_txop_limit",
	"d_pdev_resets",
	"d_rx_mid_ppdu_route_change",
	"d_rx_status",
	"d_rx_extra_frags_ring0",
	"d_rx_extra_frags_ring1",
	"d_rx_extra_frags_ring2",
	"d_rx_extra_frags_ring3",
	"d_rx_msdu_htt",
	"d_rx_mpdu_htt",
	"d_rx_msdu_stack",
	"d_rx_mpdu_stack",
	"d_rx_phy_err",
	"d_rx_phy_err_drops",
	"d_rx_mpdu_errors", /* FCS, MIC, ENC */
	"d_fw_crash_count",
	"d_fw_warm_reset_count",
	"d_fw_cold_reset_count",
	"d_fw_powerup_failed", /* boolean */
};
#define ATH10K_SSTATS_LEN ARRAY_SIZE(ath10k_gstrings_stats)

void ath10k_get_et_strings(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, *ath10k_gstrings_stats,
		       sizeof(ath10k_gstrings_stats));
}

int ath10k_get_et_sset_count(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return ATH10K_SSTATS_LEN;
	return 0;
}

void ath10k_get_et_stats(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct ath10k *ar = hw->priv;
	int i = 0;
	struct ath10k_target_stats *fw_stats;
	u64 d_flags = 0;

	fw_stats = &ar->debug.target_stats;

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH10K_STATE_ON)
		ath10k_refresh_peer_stats(ar);

	if (ar->hw_params.has_shifted_cc_wraparound)
		d_flags |= 0x1;

	data[i++] = fw_stats->hw_reaped; /* ppdu reaped */
	data[i++] = 0; /* tx bytes */
	data[i++] = fw_stats->htt_mpdus;
	data[i++] = 0; /* rx bytes */
	data[i++] = fw_stats->ch_noise_floor;
	data[i++] = fw_stats->cycle_count;
	data[i++] = fw_stats->tx_frame_count;
	data[i++] = fw_stats->rx_frame_count;
	data[i++] = fw_stats->rx_clear_count; /* yes, this appears to actually be 'busy' count */
	data[i++] = d_flags; /* give user-space a chance to decode cycle counters */
	data[i++] = fw_stats->phy_err_count;
	data[i++] = fw_stats->rts_bad;
	data[i++] = fw_stats->rts_good;
	data[i++] = fw_stats->chan_tx_power;
	data[i++] = fw_stats->fcs_bad;
	data[i++] = fw_stats->no_beacons;
	data[i++] = fw_stats->mpdu_enqued;
	data[i++] = fw_stats->msdu_enqued;
	data[i++] = fw_stats->wmm_drop;
	data[i++] = fw_stats->local_enqued;
	data[i++] = fw_stats->local_freed;
	data[i++] = fw_stats->hw_queued;
	data[i++] = fw_stats->hw_reaped;
	data[i++] = fw_stats->underrun;
	data[i++] = fw_stats->tx_abort;
	data[i++] = fw_stats->mpdus_requed;
	data[i++] = fw_stats->tx_ko;
	data[i++] = fw_stats->data_rc;
	data[i++] = fw_stats->sw_retry_failure;
	data[i++] = fw_stats->illgl_rate_phy_err;
	data[i++] = fw_stats->pdev_cont_xretry;
	data[i++] = fw_stats->pdev_tx_timeout;
	data[i++] = fw_stats->txop_ovf;
	data[i++] = fw_stats->pdev_resets;
	data[i++] = fw_stats->mid_ppdu_route_change;
	data[i++] = fw_stats->status_rcvd;
	data[i++] = fw_stats->r0_frags;
	data[i++] = fw_stats->r1_frags;
	data[i++] = fw_stats->r2_frags;
	data[i++] = fw_stats->r3_frags;
	data[i++] = fw_stats->htt_msdus;
	data[i++] = fw_stats->htt_mpdus;
	data[i++] = fw_stats->loc_msdus;
	data[i++] = fw_stats->loc_mpdus;
	data[i++] = fw_stats->phy_errs;
	data[i++] = fw_stats->phy_err_drop;
	data[i++] = fw_stats->mpdu_errs;
	data[i++] = ar->fw_crash_counter;
	data[i++] = ar->fw_warm_reset_counter;
	data[i++] = ar->fw_cold_reset_counter;
	data[i++] = ar->fw_powerup_failed;

	mutex_unlock(&ar->conf_mutex);

	WARN_ON(i != ATH10K_SSTATS_LEN);
}

#endif

static const struct file_operations fops_fw_dbglog = {
	.read = ath10k_read_fw_dbglog,
	.write = ath10k_write_fw_dbglog,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ath10k_debug_start(struct ath10k *ar)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath10k_debug_htt_stats_req(ar);
	if (ret)
		/* continue normally anyway, this isn't serious */
		ath10k_warn(ar, "failed to start htt stats workqueue: %d\n",
			    ret);

	if (ar->debug.fw_dbglog_mask) {
		ret = ath10k_wmi_dbglog_cfg(ar, ar->debug.fw_dbglog_mask);
		if (ret)
			/* not serious */
			ath10k_warn(ar, "failed to enable dbglog during start: %d",
				    ret);
	}

	return 0;
}

void ath10k_debug_stop(struct ath10k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	/* Must not use _sync to avoid deadlock, we do that in
	 * ath10k_debug_destroy(). The check for htt_stats_mask is to avoid
	 * warning from del_timer(). */
	if (ar->debug.htt_stats_mask != 0)
		cancel_delayed_work(&ar->debug.htt_stats_dwork);

	ar->debug.htt_max_amsdu = 0;
	ar->debug.htt_max_ampdu = 0;
}

static ssize_t ath10k_write_simulate_radar(struct file *file,
					   const char __user *user_buf,
					   size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;

	ieee80211_radar_detected(ar->hw);

	return count;
}

static const struct file_operations fops_simulate_radar = {
	.write = ath10k_write_simulate_radar,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

#define ATH10K_DFS_STAT(s, p) (\
	len += scnprintf(buf + len, size - len, "%-28s : %10u\n", s, \
			 ar->debug.dfs_stats.p))

#define ATH10K_DFS_POOL_STAT(s, p) (\
	len += scnprintf(buf + len, size - len, "%-28s : %10u\n", s, \
			 ar->debug.dfs_pool_stats.p))

static ssize_t ath10k_read_dfs_stats(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	int retval = 0, len = 0;
	const int size = 8000;
	struct ath10k *ar = file->private_data;
	char *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!ar->dfs_detector) {
		len += scnprintf(buf + len, size - len, "DFS not enabled\n");
		goto exit;
	}

	ar->debug.dfs_pool_stats =
			ar->dfs_detector->get_stats(ar->dfs_detector);

	len += scnprintf(buf + len, size - len, "Pulse detector statistics:\n");

	ATH10K_DFS_STAT("reported phy errors", phy_errors);
	ATH10K_DFS_STAT("pulse events reported", pulses_total);
	ATH10K_DFS_STAT("DFS pulses detected", pulses_detected);
	ATH10K_DFS_STAT("DFS pulses discarded", pulses_discarded);
	ATH10K_DFS_STAT("Radars detected", radar_detected);

	len += scnprintf(buf + len, size - len, "Global Pool statistics:\n");
	ATH10K_DFS_POOL_STAT("Pool references", pool_reference);
	ATH10K_DFS_POOL_STAT("Pulses allocated", pulse_allocated);
	ATH10K_DFS_POOL_STAT("Pulses alloc error", pulse_alloc_error);
	ATH10K_DFS_POOL_STAT("Pulses in use", pulse_used);
	ATH10K_DFS_POOL_STAT("Seqs. allocated", pseq_allocated);
	ATH10K_DFS_POOL_STAT("Seqs. alloc error", pseq_alloc_error);
	ATH10K_DFS_POOL_STAT("Seqs. in use", pseq_used);

exit:
	if (len > size)
		len = size;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_dfs_stats = {
	.read = ath10k_read_dfs_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static ssize_t ath10k_write_thresh62_ext(struct file *file,
					 const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u8 val;
	int ret = 0;

	if (kstrtou8_from_user(ubuf, count, 0, &val))
		return -EINVAL;

	mutex_lock(&ar->conf_mutex);
	ar->eeprom_overrides.thresh62_ext = val;
	ret = ath10k_wmi_pdev_set_special(ar, SET_SPECIAL_ID_THRESH62_EXT, val);
	mutex_unlock(&ar->conf_mutex);

	return ret ?: count;
}

static ssize_t ath10k_read_thresh62_ext(struct file *file,
					char __user *ubuf,
					size_t count, loff_t *ppos)
{
	char buf[32];
	struct ath10k *ar = file->private_data;
	int len = 0;

	mutex_lock(&ar->conf_mutex);
	len = scnprintf(buf, sizeof(buf) - len, "%d\n",
			ar->eeprom_overrides.thresh62_ext);
	mutex_unlock(&ar->conf_mutex);

	return simple_read_from_buffer(ubuf, count, ppos, buf, len);
}

static const struct file_operations fops_thresh62_ext = {
	.read = ath10k_read_thresh62_ext,
	.write = ath10k_write_thresh62_ext,
	.open = simple_open
};


static ssize_t ath10k_write_ct_special(struct file *file,
				       const char __user *ubuf,
				       size_t count, loff_t *ppos)
{
	struct ath10k *ar = file->private_data;
	u64 tmp;
	u32 id;
	u32 val;
	int ret = 0;

	if (kstrtou64_from_user(ubuf, count, 0, &tmp))
		return -EINVAL;

	id = tmp >> 32;
	val = tmp & 0xFFFFFFFF;

	mutex_lock(&ar->conf_mutex);
	if (id == SET_SPECIAL_ID_THRESH62_EXT) {
		ar->eeprom_overrides.thresh62_ext = val;
	}
	else if (id == SET_SPECIAL_ID_NOISE_FLR_THRESH) {
		u8 band = val >> 24;
		u8 type = (val >> 16) & 0xFF;
		if ((band > 2) || (type > CT_CCA_TYPE_MAX)) {
			ret = -EINVAL;
			goto unlock;
		}
		if (type <= CT_CCA_TYPE_MIN2)
			ar->eeprom_overrides.bands[band].minCcaPwrCT[type] = val & 0xFFFF;
		else if (type == CT_CCA_TYPE_NOISE_FLOOR)
			ar->eeprom_overrides.bands[band].noiseFloorThresh = val & 0xFFFF;
		else if (type == CT_CCA_TYPE_EN_MINCCAPWR)
			ar->eeprom_overrides.bands[band].enable_minccapwr_thresh = val & 0xFFFF;
	}
	else if (id == SET_SPECIAL_ID_IBSS_AMSDU_OK) {
		ar->eeprom_overrides.allow_ibss_amsdu = !!val;
	}
	else if (id == SET_SPECIAL_ID_MAX_TXPOWER) {
		/* This can only be set once, and is designed to be
		 * a way to try to ensure that no other tools can
		 * accidently or otherwise set the power in the firmware
		 * higher.
		 */
		if (ar->eeprom_overrides.max_txpower == 0xFFFF) {
			ar->eeprom_overrides.max_txpower = val;
			ath10k_warn(ar, "Latching max-txpower to: %d (%d dBm)\n", val, val/2);
		}
		else {
			ath10k_err(ar, "Cannot re-set max-txpower, old: %d  new: %d (%d dBm)\n",
				   ar->eeprom_overrides.max_txpower, val, val/2);
			ret = -EPERM;
			goto unlock;
		}
	}
	else if (id == SET_SPECIAL_ID_STA_TXBW_MASK) {
		/* Specify Station tx bandwidth mask (20, 40, 80Mhz). */
		ar->eeprom_overrides.tx_sta_bw_mask = val;
		ath10k_warn(ar, "Setting sta-tx-bw-mask to 0x%x\n", val);
	}
	else if (id == SET_SPECIAL_ID_PDEV_XRETRY_TH) {
		/* Set the threshold for resetting phy due to failed retries, U16 */
		ar->eeprom_overrides.pdev_xretry_th = val;
		ath10k_warn(ar, "Setting pdev-xretry-th to 0x%x\n", val);
	}
	else if (id == SET_SPECIAL_ID_RIFS_ENABLE) {
		/* Enable(1)/disable(0) baseband RIFS. */
		ar->eeprom_overrides.rifs_enable_override = val;
		ath10k_warn(ar, "Setting RIFS enable override to 0x%x\n", val);
	}
	else if (id == SET_SPECIAL_ID_WMI_WD) {
		ar->eeprom_overrides.wmi_wd_keepalive_ms = val;
		ath10k_warn(ar, "Setting WMI WD to 0x%x\n", val);
		if (val == 0)
			goto unlock; /* 0 means don't set */

		if (val == 0xFFFFFFFF)
			val = 0; /* 0xFFFFFFFF means disable, FW uses 0 to mean disable */
	}
	/* Below here are local driver hacks, and not necessarily passed directly to firmware. */
	else if (id == 0x1001) {
		/* Set station failed-transmit kickout threshold. */
		ar->sta_xretry_kickout_thresh = val;

		ath10k_warn(ar, "Setting pdev sta-xretry-kickout-thresh to 0x%x\n",
			    val);

		ath10k_mac_set_pdev_kickout(ar);
		goto unlock;
	}
	/* else, pass it through to firmware...but will not be stored locally, so
	 * won't survive through firmware reboots, etc.
	 */

	/* Send it to the firmware. */
	ret = ath10k_wmi_pdev_set_special(ar, id, val);
unlock:
	mutex_unlock(&ar->conf_mutex);

	return ret ?: count;
}

static ssize_t ath10k_read_ct_special(struct file *file,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	const char buf[] =
		"BE WARNED:  You should understand the values before setting anything here.\n"
		"You could put your NIC out of spec or maybe even break the hardware if you\n"
		"put in bad values.\n\n"
		"Value is u64, encoded thus:\n"
		"id = t64 >> 32\n"
		"val = t64 & 0xFFFFFFFF\n"
		"id: 3 THRESH62_EXT (both bands use same value currently)\n"
		"  value = val & 0xFF;\n"
		"id: 4 CCA-Values, encoded as below:\n"
		"  band = val >> 24;  //(0 5Ghz, 1 2.4Ghz)\n"
		"  type = (val >> 16) & 0xFF; // 0-2 minCcaPwr[type], 3 noiseFloorThresh\n"
		"         4 enable_minccapwr_thresh\n"
		"  value = val & 0xFFFF;\n"
		"    Unless otherwise specified, 0 means don't set.\n"
		"    enable-minccapwr-thresh:  1 disabled, 2 enabled.\n"
		"id: 5 Allow-AMSDU-IBSS, 1 enabled, 0 disabled, global setting.\n"
		"id: 6 Max TX-Power, 0-65535:  Latch max-tx-power, in 0.5 dbM Units.\n"
		"id: 8 STA-TX-BW-MASK,  0:  all, 0x1: 20Mhz, 0x2 40Mhz, 0x4 80Mhz \n"
		"id: 9 pdev failed retry threshold, U16, 10.1 firmware default is 0x40\n"
		"id: 0xA Enable(1)/Disable(0) baseband RIFS.  Default is disabled.\n"
		"id: 0xB WMI WD Keepalive(ms): 0xFFFFFFFF disables, otherwise suggest 8000+.\n"
		"\nBelow here are not actually sent to firmware directly, but configure the driver.\n"
		"id: 0x1001 set sta-kickout threshold due to tx-failures (0 means disable.  Default is 20 * 16.)\n"
		"\n";

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static const struct file_operations fops_ct_special = {
	.read = ath10k_read_ct_special,
	.write = ath10k_write_ct_special,
	.open = simple_open
};


int ath10k_debug_create(struct ath10k *ar)
{
	ar->debug.fw_crash_data = vzalloc(sizeof(*ar->debug.fw_crash_data));
	if (!ar->debug.fw_crash_data)
		return -ENOMEM;

	return 0;
}

void ath10k_debug_destroy(struct ath10k *ar)
{
	vfree(ar->debug.fw_crash_data);
	ar->debug.fw_crash_data = NULL;
}

int ath10k_debug_register(struct ath10k *ar)
{
	ar->debug.debugfs_phy = debugfs_create_dir("ath10k",
						   ar->hw->wiphy->debugfsdir);
	if (IS_ERR_OR_NULL(ar->debug.debugfs_phy)) {
		if (IS_ERR(ar->debug.debugfs_phy))
			return PTR_ERR(ar->debug.debugfs_phy);

		return -ENOMEM;
	}

	//INIT_DELAYED_WORK(&ar->debug.poll_ce_dwork, ath10k_debug_poll_ce_dwork);
	INIT_DELAYED_WORK(&ar->debug.nop_dwork, ath10k_debug_nop_dwork);

	queue_delayed_work(ar->workqueue, &ar->debug.nop_dwork,
			   msecs_to_jiffies(ATH10K_DEBUG_NOP_INTERVAL));

	//queue_delayed_work(ar->workqueue, &ar->debug.poll_ce_dwork,
	//		   msecs_to_jiffies(ATH10K_DEBUG_POLL_CE_INTERVAL));

	INIT_DELAYED_WORK(&ar->debug.htt_stats_dwork,
			  ath10k_debug_htt_stats_dwork);

	init_completion(&ar->debug.event_stats_compl);

	debugfs_create_file("fw_stats", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_fw_stats);

	debugfs_create_file("fw_regs", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_fw_regs);

	debugfs_create_file("wmi_services", S_IRUSR, ar->debug.debugfs_phy, ar,
			    &fops_wmi_services);

	debugfs_create_file("set_rates", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_set_rates);

	debugfs_create_file("simulate_fw_crash", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_simulate_fw_crash);

	debugfs_create_file("fw_crash_dump", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_fw_crash_dump);

	debugfs_create_file("debug_level", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_debug_level);

	debugfs_create_file("chip_id", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_chip_id);

	debugfs_create_file("htt_stats_mask", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_htt_stats_mask);

	debugfs_create_file("htt_max_amsdu_ampdu", S_IRUSR | S_IWUSR,
			    ar->debug.debugfs_phy, ar,
			    &fops_htt_max_amsdu_ampdu);

	debugfs_create_file("fw_dbglog", S_IRUSR, ar->debug.debugfs_phy,
			    ar, &fops_fw_dbglog);

	if (config_enabled(CONFIG_ATH10K_DFS_CERTIFIED)) {
		debugfs_create_file("dfs_simulate_radar", S_IWUSR,
				    ar->debug.debugfs_phy, ar,
				    &fops_simulate_radar);

		debugfs_create_bool("dfs_block_radar_events", S_IWUSR,
				    ar->debug.debugfs_phy,
				    &ar->dfs_block_radar_events);

		debugfs_create_file("dfs_stats", S_IRUSR,
				    ar->debug.debugfs_phy, ar,
				    &fops_dfs_stats);
	}

	debugfs_create_file("thresh62_ext", S_IRUGO | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_thresh62_ext);

	debugfs_create_file("ct_special", S_IRUGO | S_IWUSR,
			    ar->debug.debugfs_phy, ar, &fops_ct_special);

	return 0;
}

void ath10k_debug_unregister(struct ath10k *ar)
{
	//cancel_delayed_work_sync(&ar->debug.poll_ce_dwork);
	cancel_delayed_work_sync(&ar->debug.nop_dwork);
	cancel_delayed_work_sync(&ar->debug.htt_stats_dwork);
}

#endif /* CONFIG_ATH10K_DEBUGFS */

#ifdef CONFIG_ATH10K_DEBUG
void ath10k_dbg(struct ath10k *ar, enum ath10k_debug_mask mask,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (ath10k_debug_mask & mask)
		dev_printk(KERN_DEBUG, ar->dev, "%pV", &vaf);

	trace_ath10k_log_dbg(ar, mask, &vaf);

	va_end(args);
}
EXPORT_SYMBOL(ath10k_dbg);

void ath10k_dbg_dump(struct ath10k *ar,
		     enum ath10k_debug_mask mask,
		     const char *msg, const char *prefix,
		     const void *buf, size_t len)
{
	if (ath10k_debug_mask & mask) {
		if (msg)
			ath10k_dbg(ar, mask, "%s\n", msg);

		print_hex_dump_bytes(prefix, DUMP_PREFIX_OFFSET, buf, len);
	}

	/* tracing code doesn't like null strings :/ */
	trace_ath10k_log_dbg_dump(ar, msg ? msg : "", prefix ? prefix : "",
				  buf, len);
}
EXPORT_SYMBOL(ath10k_dbg_dump);

#endif /* CONFIG_ATH10K_DEBUG */

void ath10k_dbg_print_fw_dbg_buffer(struct ath10k *ar, __le32 *ibuf, int len,
				    const char* lvl)
{
	/* Print out raw hex, external tools can decode if
	 * they care.
	 * TODO:  Add ar identifier to messages.
	 */
	int q = 0;

	dev_printk(lvl, ar->dev, "ath10k_pci ATH10K_DBG_BUFFER:\n");
	while (q < len) {
		if (q + 8 <= len) {
			printk("%sath10k: [%04d]: %08X %08X %08X %08X %08X %08X %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1], ibuf[q+2], ibuf[q+3],
			       ibuf[q+4], ibuf[q+5], ibuf[q+6], ibuf[q+7]);
			q += 8;
		}
		else if (q + 7 <= len) {
			printk("%sath10k: [%04d]: %08X %08X %08X %08X %08X %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1], ibuf[q+2], ibuf[q+3],
			       ibuf[q+4], ibuf[q+5], ibuf[q+6]);
			q += 7;
		}
		else if (q + 6 <= len) {
			printk("%sath10k: [%04d]: %08X %08X %08X %08X %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1], ibuf[q+2], ibuf[q+3],
			       ibuf[q+4], ibuf[q+5]);
			q += 6;
		}
		else if (q + 5 <= len) {
			printk("%sath10k: [%04d]: %08X %08X %08X %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1], ibuf[q+2], ibuf[q+3],
			       ibuf[q+4]);
			q += 5;
		}
		else if (q + 4 <= len) {
			printk("%sath10k: [%04d]: %08X %08X %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1], ibuf[q+2], ibuf[q+3]);
			q += 4;
		}
		else if (q + 3 <= len) {
			printk("%sath10k: [%04d]: %08X %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1], ibuf[q+2]);
			q += 3;
		}
		else if (q + 2 <= len) {
			printk("%sath10k: [%04d]: %08X %08X\n",
			       lvl, q,
			       ibuf[q], ibuf[q+1]);
			q += 2;
		}
		else if (q + 1 <= len) {
			printk("%sath10k: [%04d]: %08X\n",
			       lvl, q,
			       ibuf[q]);
			q += 1;
		}
		else {
			break;
		}
	}/* while */

	dev_printk(lvl, ar->dev, "ATH10K_END\n");
}
EXPORT_SYMBOL(ath10k_dbg_print_fw_dbg_buffer);
