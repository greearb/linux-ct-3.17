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

#ifndef _MAC_H_
#define _MAC_H_

#include <net/mac80211.h>
#include "core.h"

extern int ath10k_modparam_nohwcrypt;
extern int ath10k_modparam_target_num_vdevs_ct;
extern int ath10k_modparam_target_num_peers_ct;
extern int ath10k_modparam_target_num_msdu_desc_ct;
extern int ath10k_modparam_target_num_rate_ctrl_objs_ct;

struct ath10k_generic_iter {
	struct ath10k *ar;
	int ret;
};

struct ath10k *ath10k_mac_create(size_t priv_size);
void ath10k_mac_destroy(struct ath10k *ar);
int ath10k_mac_register(struct ath10k *ar);
void ath10k_mac_unregister(struct ath10k *ar);
struct ath10k_vif *ath10k_get_arvif(struct ath10k *ar, u32 vdev_id);
void __ath10k_scan_finish(struct ath10k *ar);
void ath10k_scan_finish(struct ath10k *ar);
void ath10k_scan_timeout_work(struct work_struct *work);
void ath10k_offchan_tx_purge(struct ath10k *ar);
void ath10k_offchan_tx_work(struct work_struct *work);
void ath10k_mgmt_over_wmi_tx_purge(struct ath10k *ar);
void ath10k_mgmt_over_wmi_tx_work(struct work_struct *work);
void ath10k_halt(struct ath10k *ar);

static inline struct ath10k_vif *ath10k_vif_to_arvif(struct ieee80211_vif *vif)
{
	return (struct ath10k_vif *)vif->drv_priv;
}

static inline void ath10k_tx_h_seq_no(struct ieee80211_vif *vif,
				      struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath10k_vif *arvif = ath10k_vif_to_arvif(vif);

	if (info->flags  & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		if (arvif->tx_seq_no == 0)
			arvif->tx_seq_no = 0x1000;

		if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
			arvif->tx_seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(arvif->tx_seq_no);
	}
}

int ath10k_mac_set_pdev_kickout(struct ath10k *ar);

#endif /* _MAC_H_ */
