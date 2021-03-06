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

#ifndef _CORE_H_
#define _CORE_H_

#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/uuid.h>
#include <linux/time.h>

#include "htt.h"
#include "htc.h"
#include "hw.h"
#include "targaddrs.h"
#include "wmi.h"
#include "../ath.h"
#include "../regd.h"
#include "../dfs_pattern_detector.h"
#include "spectral.h"

#define MS(_v, _f) (((_v) & _f##_MASK) >> _f##_LSB)
#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)
#define WO(_f)      ((_f##_OFFSET) >> 2)

#define ATH10K_SCAN_ID 0
#define WMI_READY_TIMEOUT (5 * HZ)
#define ATH10K_FLUSH_TIMEOUT_HZ (5*HZ)
#define ATH10K_NUM_CHANS 38

/* Antenna noise floor */
#define ATH10K_DEFAULT_NOISE_FLOOR -95

#define ATH10K_MAX_NUM_MGMT_PENDING 128

/* number of failed packets (20 packets with 16 sw reties each) */
#define DEFAULT_ATH10K_KICKOUT_THRESHOLD (20 * 16)

/*
 * Use insanely high numbers to make sure that the firmware implementation
 * won't start, we have the same functionality already in hostapd. Unit
 * is seconds.
 */
#define ATH10K_KEEPALIVE_MIN_IDLE 3747
#define ATH10K_KEEPALIVE_MAX_IDLE 3895
#define ATH10K_KEEPALIVE_MAX_UNRESPONSIVE 3900

struct ath10k;

struct ath10k_skb_cb {
	dma_addr_t paddr;
	u8 vdev_id;

	struct {
		u8 tid;
		bool is_offchan;
		struct ath10k_htt_txbuf *txbuf;
		u32 txbuf_paddr;
	} __packed htt;

	struct {
		bool dtim_zero;
		bool deliver_cab;
	} bcn;
} __packed;

static inline struct ath10k_skb_cb *ATH10K_SKB_CB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath10k_skb_cb) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct ath10k_skb_cb *)&IEEE80211_SKB_CB(skb)->driver_data;
}

static inline u32 host_interest_item_address(u32 item_offset)
{
	return QCA988X_HOST_INTEREST_ADDRESS + item_offset;
}

struct ath10k_bmi {
	bool done_sent;
};

#define ATH10K_MAX_MEM_REQS 16

struct ath10k_mem_chunk {
	void *vaddr;
	dma_addr_t paddr;
	u32 len;
	u32 req_id;
};

struct ath10k_wmi {
	enum ath10k_htc_ep_id eid;
	struct completion service_ready;
	struct completion unified_ready;
	wait_queue_head_t tx_credits_wq;
	struct wmi_cmd_map *cmd;
	struct wmi_vdev_param_map *vdev_param;
	struct wmi_pdev_param_map *pdev_param;

	u32 num_mem_chunks;
	struct ath10k_mem_chunk mem_chunks[ATH10K_MAX_MEM_REQS];
};

struct ath10k_peer_stat {
	u8 peer_macaddr[ETH_ALEN];
	u32 peer_rssi;
	u32 peer_tx_rate;
	u32 peer_rx_rate; /* 10x only */
};

struct ath10k_target_stats {
	/* PDEV stats */
	s32 ch_noise_floor;
	u32 tx_frame_count;
	u32 rx_frame_count;
	u32 rx_clear_count;
	u32 cycle_count;
	u32 phy_err_count;
	u32 chan_tx_power;
	u32 ack_rx_bad;
	u32 rts_bad;
	u32 rts_good;
	u32 fcs_bad;
	u32 no_beacons;
	u32 mib_int_count;

	/* PDEV TX stats */
	s32 comp_queued;
	s32 comp_delivered;
	s32 msdu_enqued;
	s32 mpdu_enqued;
	s32 wmm_drop;
	s32 local_enqued;
	s32 local_freed;
	s32 hw_queued;
	s32 hw_reaped;
	s32 underrun;
	s32 tx_abort;
	s32 mpdus_requed;
	u32 tx_ko;
	u32 data_rc;
	u32 self_triggers;
	u32 sw_retry_failure;
	u32 illgl_rate_phy_err;
	u32 pdev_cont_xretry;
	u32 pdev_tx_timeout;
	u32 pdev_resets;
	u32 phy_underrun;
	u32 txop_ovf;

	/* PDEV RX stats */
	s32 mid_ppdu_route_change;
	s32 status_rcvd;
	s32 r0_frags;
	s32 r1_frags;
	s32 r2_frags;
	s32 r3_frags;
	s32 htt_msdus;
	s32 htt_mpdus;
	s32 loc_msdus;
	s32 loc_mpdus;
	s32 oversize_amsdu;
	s32 phy_errs;
	s32 phy_err_drop;
	s32 mpdu_errs;

	/* VDEV STATS */

	/* PEER STATS */
	u8 peers;
	struct ath10k_peer_stat peer_stat[TARGET_NUM_PEERS];

	/* TODO: Beacon filter stats */

	/* Register and related dump, CT firmware only. */
	u32 mac_filter_addr_l32;
	u32 mac_filter_addr_u16;
	u32 dcu_slot_time;
	u32 phy_bb_mode_select;
	u32 pcu_bssid_l32;
	u32 pcu_bssid_u16;
	u32 pcu_bssid2_l32;
	u32 pcu_bssid2_u16;
	u32 pcu_sta_addr_l32;
	u32 pcu_sta_addr_u16;
	u32 mac_dma_cfg;
	u32 mac_dma_txcfg;
	u32 pcu_rxfilter;
	u32 phy_bb_gen_controls;
	u32 dma_imr;
	u32 dma_txrx_imr;
	u32 sw_powermode;
	u16 sw_chainmask_tx;
	u16 sw_chainmask_rx;
	u32 sw_opmode;
	u32 sw_rxfilter;
};

struct ath10k_dfs_stats {
	u32 phy_errors;
	u32 pulses_total;
	u32 pulses_detected;
	u32 pulses_discarded;
	u32 radar_detected;
};

#define ATH10K_MAX_NUM_PEER_IDS (1 << 11) /* htt rx_desc limit */

struct ath10k_peer {
	struct list_head list;
	int vdev_id;
	u8 addr[ETH_ALEN];
	DECLARE_BITMAP(peer_ids, ATH10K_MAX_NUM_PEER_IDS);
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
};

struct ath10k_sta {
	struct ath10k_vif *arvif;

	/* the following are protected by ar->data_lock */
	u32 changed; /* IEEE80211_RC_* */
	u32 bw;
	u32 nss;
	u32 smps;

	struct work_struct update_wk;
};

#define ATH10K_VDEV_SETUP_TIMEOUT_HZ (5*HZ)

struct ath10k_vif {
	struct list_head list;

	u32 vdev_id;
	enum wmi_vdev_type vdev_type;
	enum wmi_vdev_subtype vdev_subtype;
	u32 beacon_interval;
	u32 dtim_period;
	struct sk_buff *beacon;
	/* protected by data_lock */
	bool beacon_sent;

	struct ath10k *ar;
	struct ieee80211_vif *vif;

	bool is_started;
	bool is_up;
	bool spectral_enabled;
	u32 aid;
	u8 bssid[ETH_ALEN];

	struct work_struct wep_key_work;
	struct ieee80211_key_conf *wep_keys[WMI_MAX_KEY_INDEX + 1];
	u8 def_wep_key_idx;
	u8 def_wep_key_newidx;

	u16 tx_seq_no;

	union {
		struct {
			u32 uapsd;
		} sta;
		struct {
			/* 127 stations; wmi limit */
			u8 tim_bitmap[16];
			u8 tim_len;
			u32 ssid_len;
			u8 ssid[IEEE80211_MAX_SSID_LEN];
			bool hidden_ssid;
			/* P2P_IE with NoA attribute for P2P_GO case */
			u32 noa_len;
			u8 *noa_data;
		} ap;
	} u;

	u8 fixed_rate;
	u8 fixed_nss;
	u8 force_sgi;
	bool use_cts_prot;
	int num_legacy_stations;
};

struct ath10k_vif_iter {
	u32 vdev_id;
	struct ath10k_vif *arvif;
};

/* This will store at least the last 128 entries.  Each dbglog message
 * is a max of 7 32-bit integers in length, but the length can be less
 * than that as well.
 */
#define ATH10K_DBGLOG_DATA_LEN (128 * 7)
struct ath10k_dbglog_entry_storage {
	u32 head_idx; /* Where to write next chunk of data */
	u32 tail_idx; /* Index of first msg */
	__le32 data[ATH10K_DBGLOG_DATA_LEN];
};

/* Just enough info to decode firmware debug-log argument length */
#define DBGLOG_NUM_ARGS_OFFSET           26
#define DBGLOG_NUM_ARGS_MASK             0xFC000000 /* Bit 26-31 */
#define DBGLOG_NUM_ARGS_MAX              5 /* firmware tool chain limit */

/* estimated values, hopefully these are enough */
#define ATH10K_ROM_BSS_BUF_LEN 30000
#define ATH10K_RAM_BSS_BUF_LEN 10000

/* used for crash-dump storage, protected by data-lock */
struct ath10k_fw_crash_data {
	bool crashed_since_read;

	uuid_le uuid;
	struct timespec timestamp;
	__le32 registers[REG_DUMP_COUNT_QCA988X];
	__le32 stack_buf[ATH10K_FW_STACK_SIZE / sizeof(__le32)];
	__le32 exc_stack_buf[ATH10K_FW_STACK_SIZE / sizeof(__le32)];
	__le32 stack_addr;
	__le32 exc_stack_addr;
	__le32 rom_bss_buf[ATH10K_ROM_BSS_BUF_LEN / sizeof(__le32)];
	__le32 ram_bss_buf[ATH10K_RAM_BSS_BUF_LEN / sizeof(__le32)];
};

struct ath10k_debug {
	struct dentry *debugfs_phy;

	struct ath10k_target_stats target_stats;
	DECLARE_BITMAP(wmi_service_bitmap, WMI_SERVICE_MAX);

	struct completion event_stats_compl;

	unsigned long htt_stats_mask;
	struct delayed_work htt_stats_dwork;
	//struct delayed_work poll_ce_dwork;
	struct delayed_work nop_dwork;
	struct ath10k_dfs_stats dfs_stats;
	struct ath_dfs_pool_stats dfs_pool_stats;

	u32 fw_dbglog_mask;
	u32 nop_id;

	u8 htt_max_amsdu;
	u8 htt_max_ampdu;

	struct ath10k_dbglog_entry_storage dbglog_entry_data;

	struct ath10k_fw_crash_data *fw_crash_data;
};

enum ath10k_state {
	ATH10K_STATE_OFF = 0,
	ATH10K_STATE_ON,

	/* When doing firmware recovery the device is first powered down.
	 * mac80211 is supposed to call in to start() hook later on. It is
	 * however possible that driver unloading and firmware crash overlap.
	 * mac80211 can wait on conf_mutex in stop() while the device is
	 * stopped in ath10k_core_restart() work holding conf_mutex. The state
	 * RESTARTED means that the device is up and mac80211 has started hw
	 * reconfiguration. Once mac80211 is done with the reconfiguration we
	 * set the state to STATE_ON in restart_complete(). */
	ATH10K_STATE_RESTARTING,
	ATH10K_STATE_RESTARTED,

	/* The device has crashed while restarting hw. This state is like ON
	 * but commands are blocked in HTC and -ECOMM response is given. This
	 * prevents completion timeouts and makes the driver more responsive to
	 * userspace commands. This is also prevents recursive recovery. */
	ATH10K_STATE_WEDGED,

	/* factory tests */
	ATH10K_STATE_UTF,
};

enum ath10k_firmware_mode {
	/* the default mode, standard 802.11 functionality */
	ATH10K_FIRMWARE_MODE_NORMAL,

	/* factory tests etc */
	ATH10K_FIRMWARE_MODE_UTF,
};

enum ath10k_fw_features {
	/* wmi_mgmt_rx_hdr contains extra RSSI information */
	ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX = 0,

	/* firmware from 10X branch */
	ATH10K_FW_FEATURE_WMI_10X = 1,

	/* firmware support tx frame management over WMI, otherwise it's HTT */
	ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX = 2,

	/* Firmware does not support P2P */
	ATH10K_FW_FEATURE_NO_P2P = 3,

	/* Firmware 10.2 feature bit. The ATH10K_FW_FEATURE_WMI_10X feature bit
	 * is required to be set as well.
	 */
	ATH10K_FW_FEATURE_WMI_10_2 = 4,

	/* Firmware from Candela Technologies, enables more VIFs, etc */
	ATH10K_FW_FEATURE_WMI_10X_CT_OLD = 5,

	/* Firmware from Candela Technologies with rx-software-crypt.
	 * Required for multiple stations connected to same AP when using
	 * encryption (ie, commercial version of CT firmware) */
	ATH10K_FW_FEATURE_CT_RXSWCRYPT_OLD = 6,

	/* tx-status has the noack bits (CT firmware version 14 and higher ) */
	ATH10K_FW_FEATURE_HAS_TXSTATUS_NOACK = 30,
	ATH10K_FW_FEATURE_WMI_10X_CT         = 31,
	ATH10K_FW_FEATURE_CT_RXSWCRYPT       = 32,

	/* keep last */
	ATH10K_FW_FEATURE_COUNT,
};

enum ath10k_dev_flags {
	/* Indicates that ath10k device is during CAC phase of DFS */
	ATH10K_CAC_RUNNING,
	ATH10K_FLAG_CORE_REGISTERED,
};

enum ath10k_scan_state {
	ATH10K_SCAN_IDLE,
	ATH10K_SCAN_STARTING,
	ATH10K_SCAN_RUNNING,
	ATH10K_SCAN_ABORTING,
};

static inline const char *ath10k_scan_state_str(enum ath10k_scan_state state)
{
	switch (state) {
	case ATH10K_SCAN_IDLE:
		return "idle";
	case ATH10K_SCAN_STARTING:
		return "starting";
	case ATH10K_SCAN_RUNNING:
		return "running";
	case ATH10K_SCAN_ABORTING:
		return "aborting";
	}

	return "unknown";
}

struct ath10k {
	struct ath_common ath_common;
	struct ieee80211_hw *hw;
	struct device *dev;
	u8 mac_addr[ETH_ALEN];
	bool fw_powerup_failed; /* If true, might take reboot to recover. */
	u32 chip_id;
	u32 target_version;
	u8 fw_version_major;
	bool use_swcrypt; /* Firmware (and driver) supports rx-sw-crypt? */
	u32 fw_version_minor;
	u16 fw_version_release;
	u16 fw_version_build;
	u32 phy_capability;
	u32 hw_min_tx_power;
	u32 hw_max_tx_power;
	u32 ht_cap_info;
	u32 vht_cap_info;
	u32 num_rf_chains;
	u32 set_rate_type; /* override for set-rate behaviour */

	DECLARE_BITMAP(fw_features, ATH10K_FW_FEATURE_COUNT);

	struct targetdef *targetdef;
	struct hostdef *hostdef;

	bool p2p;
	bool forcing_ce_service_all;
	bool all_pkts_htt; /* target has no separate mgmt tx command? */
	u8 wmi_timeouts; /* counter */

	struct {
		const struct ath10k_hif_ops *ops;
	} hif;

	struct completion target_suspend;

	struct ath10k_bmi bmi;
	struct ath10k_wmi wmi;
	struct ath10k_htc htc;
	struct ath10k_htt htt;

	struct ieee80211_iface_limit ath10k_if_limits[3];
	struct ieee80211_iface_combination ath10k_if_comb[1];

	struct ath10k_hw_params {
		u32 id;
		const char *name;
		u32 patch_load_addr;

		/* This is true if given HW chip has a quirky Cycle Counter
                 * wraparound which resets to 0x7fffffff instead of 0. All
                 * other CC related counters (e.g. Rx Clear Count) are divided
                 * by 2 so they never wraparound themselves.
                 */
                bool has_shifted_cc_wraparound;

		struct ath10k_hw_params_fw {
			const char *dir;
			const char *fw;
			const char *otp;
			const char *board;
		} fw;
	} hw_params;

	/* These are written to only during first firmware load from user
	 * space so no need for any locking.
	 */
	struct {
		u32 ram_bss_addr;
		u32 ram_bss_len;
		u32 rom_bss_addr;
		u32 rom_bss_len;
	} fw;

	const struct firmware *board;
	const void *board_data;
	size_t board_len;

	const struct firmware *otp;
	const void *otp_data;
	size_t otp_len;

	const struct firmware *firmware;
	const void *firmware_data;
	size_t firmware_len;

	int fw_api;

	struct {
		struct completion started;
		struct completion completed;
		struct completion on_channel;
		struct delayed_work timeout;
		enum ath10k_scan_state state;
		bool is_roc;
		int vdev_id;
		int roc_freq;
	} scan;

	struct {
		struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
	} mac;

	/* should never be NULL; needed for regular htt rx */
	struct ieee80211_channel *rx_channel;

	/* valid during scan; needed for mgmt rx during scan */
	struct ieee80211_channel *scan_channel;

	/* current operating channel definition */
	struct cfg80211_chan_def chandef;

	unsigned long long free_vdev_map;
	bool monitor;
	int monitor_vdev_id;
	bool monitor_started;
	unsigned int filter_flags;
	unsigned long dev_flags;
	u32 dfs_block_radar_events;
	int install_key_rv; /* Store error code from key-install */

	/* protected by conf_mutex */
	bool radar_enabled;
	int num_started_vdevs;
	u32 sta_xretry_kickout_thresh;

	/* Protected by conf-mutex */
	u8 supp_tx_chainmask;
	u8 supp_rx_chainmask;
	u8 cfg_tx_chainmask;
	u8 cfg_rx_chainmask;

	bool fw_crashed_since_start;

	struct wmi_pdev_set_wmm_params_arg wmm_params;
	struct completion install_key_done;

	struct completion vdev_setup_done;

	struct workqueue_struct *workqueue;

	/* prevents concurrent FW reconfiguration */
	struct mutex conf_mutex;

	/* protects shared structure data */
	spinlock_t data_lock;

	struct list_head arvifs;
	struct list_head peers;
	wait_queue_head_t peer_mapping_wq;

	/* number of created peers; protected by data_lock */
	int num_peers;

	struct work_struct offchan_tx_work;
	struct sk_buff_head offchan_tx_queue;
	struct completion offchan_tx_completed;
	struct sk_buff *offchan_tx_skb;

	struct work_struct wmi_mgmt_tx_work;
	struct sk_buff_head wmi_mgmt_tx_queue;

	enum ath10k_state state;

	struct work_struct register_work;
	struct work_struct restart_work;

	/* cycle count is reported twice for each visited channel during scan.
	 * access protected by data_lock */
	u32 survey_last_rx_clear_count;
	u32 survey_last_cycle_count;
	struct survey_info survey[ATH10K_NUM_CHANS];

	struct dfs_pattern_detector *dfs_detector;

	u32 fw_crash_counter;
	u32 fw_warm_reset_counter;
	u32 fw_cold_reset_counter;

#ifdef CONFIG_ATH10K_DEBUGFS
	struct ath10k_debug debug;
#endif

	struct {
		/* relay(fs) channel for spectral scan */
		struct rchan *rfs_chan_spec_scan;

		/* spectral_mode and spec_config are protected by conf_mutex */
		enum ath10k_spectral_mode mode;
		struct ath10k_spec_scan config;
	} spectral;

	struct {
		/* protected by conf_mutex */
		const struct firmware *utf;
		DECLARE_BITMAP(orig_fw_features, ATH10K_FW_FEATURE_COUNT);

		/* protected by data_lock */
		bool utf_monitor;
	} testmode;

	/* Index 0 is for 5Ghz, index 1 is for 2.4Ghz, CT firmware only. */
	/* be sure to flush this to firmware after resets */
	/* Includes various other backdoor hacks as well. */
	struct {
		struct {
#define MIN_CCA_PWR_COUNT 3
			u16 minCcaPwrCT[MIN_CCA_PWR_COUNT]; /* 0 means don't-set */
			u8 noiseFloorThresh; /* 0 means don't-set */
			/* Have to set this to 2 before minCcaPwr settings will be active.
			 * Values:  0  don't-set, 1 disable, 2 enable
			 */
			u8 enable_minccapwr_thresh;
		} bands[2];
		u8 thresh62_ext;
		u8 tx_sta_bw_mask; /* 0:  all, 0x1: 20Mhz, 0x2 40Mhz, 0x4 80Mhz */
		bool allow_ibss_amsdu;
		bool rifs_enable_override;
		u16 max_txpower;
		u16 pdev_xretry_th; /* Max failed retries before wifi chip is reset, 10.1 firmware default is 0x40 */
		u32 wmi_wd_keepalive_ms; /* 0xFFFFFFFF means disable, otherwise, FW will assert after X ms of not receiving
					  * a NOP keepalive from the driver.  Suggested value is 0xFFFFFFFF, or 8000+.
					  * 0 means use whatever firmware defaults to (probably 8000).
					  * Units are actually 1/1024 of a second, but pretty close to ms, at least.
					  */
	} eeprom_overrides;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

struct ath10k *ath10k_core_create(size_t priv_size, struct device *dev,
				  const struct ath10k_hif_ops *hif_ops);
void ath10k_core_destroy(struct ath10k *ar);

int ath10k_core_start(struct ath10k *ar, enum ath10k_firmware_mode mode);
int ath10k_wait_for_suspend(struct ath10k *ar, u32 suspend_opt);
void ath10k_core_stop(struct ath10k *ar);
int ath10k_core_register(struct ath10k *ar, u32 chip_id);
void ath10k_core_unregister(struct ath10k *ar);
bool ath10k_can_send_fw_msg(struct ath10k *ar);
#endif /* _CORE_H_ */
