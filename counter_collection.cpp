#include "counter_collection.h"
#include <dirent.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>

// ── Full list of all possible counters (immutable) ──────────────────────────
const std::vector<std::string> all_counter_names = {
    "atu_ats_prs_odp_latency_3",
    "atu_ats_prs_odp_latency_1",
    "atu_ats_prs_odp_latency_2",
    "atu_ats_prs_odp_latency_0",
    "atu_ats_trans_latency_1",
    "atu_ats_trans_latency_2",
    "atu_ats_trans_latency_0",
    "atu_ats_trans_latency_3",
    "atu_cache_hit_base_page_size_3",
    "atu_cache_hit_base_page_size_1",
    "atu_cache_hit_base_page_size_2",
    "atu_cache_hit_base_page_size_0",
    "atu_cache_hit_derivative1_page_size_3",
    "atu_cache_hit_derivative1_page_size_1",
    "atu_cache_hit_derivative1_page_size_2",
    "atu_cache_hit_derivative1_page_size_0",
    "atu_cache_hit_derivative2_page_size_2",
    "atu_cache_hit_derivative2_page_size_0",
    "atu_cache_hit_derivative2_page_size_3",
    "atu_cache_hit_derivative2_page_size_1",
    "atu_cache_miss_2",
    "atu_cache_miss_0",
    "atu_cache_miss_ee",
    "atu_cache_miss_ixe",
    "atu_cache_miss_3",
    "atu_cache_miss_1",
    "atu_cache_miss_oxe",
    "atu_cache_miss_ee",
    "atu_cache_miss_ixe",
    "atu_cache_miss_oxe",
    "atu_cache_evictions",
    "atu_client_req_ee",
    "atu_client_req_ixe",
    "atu_client_req_oxe",
    "atu_client_rsp_not_ok_2",
    "atu_client_rsp_not_ok_0",
    "atu_client_rsp_not_ok_3",
    "atu_client_rsp_not_ok_1",
    "atu_nic_pri_odp_latency_2",
    "atu_nic_pri_odp_latency_0",
    "atu_nic_pri_odp_latency_3",
    "atu_nic_pri_odp_latency_1",
    "atu_nta_trans_latency_3",
    "atu_nta_trans_latency_1",
    "atu_nta_trans_latency_2",
    "atu_nta_trans_latency_0",
    "atu_odp_requests_1",
    "atu_odp_requests_2",
    "atu_odp_requests_0",
    "atu_odp_requests_3",
    "cq_cq_cmd_counts_5",
    "cq_cq_cmd_counts_3",
    "cq_cq_cmd_counts_14",
    "cq_cq_cmd_counts_1",
    "cq_cq_cmd_counts_12",
    "cq_cq_cmd_counts_8",
    "cq_cq_cmd_counts_10",
    "cq_cq_cmd_counts_6",
    "cq_cq_cmd_counts_4",
    "cq_cq_cmd_counts_15",
    "cq_cq_cmd_counts_2",
    "cq_cq_cmd_counts_13",
    "cq_cq_cmd_counts_0",
    "cq_cq_cmd_counts_9",
    "cq_cq_cmd_counts_11",
    "cq_cq_cmd_counts_7",
    "cq_dma_cmd_counts_6",
    "cq_dma_cmd_counts_4",
    "cq_dma_cmd_counts_2",
    "cq_dma_cmd_counts_0",
    "cq_dma_cmd_counts_14",
    "cq_dma_cmd_counts_9",
    "cq_dma_cmd_counts_12",
    "cq_dma_cmd_counts_7",
    "cq_dma_cmd_counts_10",
    "cq_dma_cmd_counts_5",
    "cq_dma_cmd_counts_3",
    "cq_dma_cmd_counts_1",
    "cq_dma_cmd_counts_15",
    "cq_dma_cmd_counts_13",
    "cq_dma_cmd_counts_8",
    "cq_dma_cmd_counts_11",
    "cq_cycles_blocked_1",
    "cq_cycles_blocked_2",
    "cq_cycles_blocked_0",
    "cq_cycles_blocked_3",
    "cq_num_cq_cmds_3",
    "cq_num_cq_cmds_1",
    "cq_num_cq_cmds_2",
    "cq_num_cq_cmds_0",
    "cq_num_dma_cmds_3",
    "cq_num_dma_cmds_1",
    "cq_num_dma_cmds_2",
    "cq_num_dma_cmds_0",
    "cq_num_idc_cmds_3",
    "cq_num_idc_cmds_1",
    "cq_num_idc_cmds_2",
    "cq_num_idc_cmds_0",
    "cq_num_ll_cmds_0",
    "cq_num_ll_cmds_3",
    "cq_num_ll_cmds_1",
    "cq_num_ll_cmds_2",
    "cq_num_ll_ops_received_3",
    "cq_num_ll_ops_received_1",
    "cq_num_ll_ops_received_2",
    "cq_num_ll_ops_received_0",
    "cq_num_ll_ops_rejected_3",
    "cq_num_ll_ops_rejected_1",
    "cq_num_ll_ops_rejected_2",
    "cq_num_ll_ops_rejected_0",
    "cq_num_ll_ops_split_2",
    "cq_num_ll_ops_split_0",
    "cq_num_ll_ops_split_3",
    "cq_num_ll_ops_split_1",
    "cq_num_ll_ops_successful_3",
    "cq_num_ll_ops_successful_1",
    "cq_num_ll_ops_successful_2",
    "cq_num_ll_ops_successful_0",
    "cq_num_tgq_cmd_reads_2",
    "cq_num_tgq_cmd_reads_0",
    "cq_num_tgq_cmd_reads_3",
    "cq_num_tgq_cmd_reads_1",
    "cq_num_tgt_cmds_2",
    "cq_num_tgt_cmds_0",
    "cq_num_tgt_cmds_3",
    "cq_num_tgt_cmds_1",
    "cq_num_tou_cmd_reads_3",
    "cq_num_tou_cmd_reads_1",
    "cq_num_tou_cmd_reads_2",
    "cq_num_tou_cmd_reads_0",
    "cq_num_txq_cmd_reads_2",
    "cq_num_txq_cmd_reads_0",
    "cq_num_txq_cmd_reads_3",
    "cq_num_txq_cmd_reads_1",
    "cq_tgt_waiting_on_read_2",
    "cq_tgt_waiting_on_read_0",
    "cq_tgt_waiting_on_read_3",
    "cq_tgt_waiting_on_read_1",
    "cq_tx_waiting_on_read_2",
    "cq_tx_waiting_on_read_0",
    "cq_tx_waiting_on_read_3",
    "cq_tx_waiting_on_read_1",
    "ee_addr_trans_prefetch_cntr_3",
    "ee_addr_trans_prefetch_cntr_1",
    "ee_addr_trans_prefetch_cntr_2",
    "ee_addr_trans_prefetch_cntr_0",
    "ee_cbs_written_cntr_1",
    "ee_cbs_written_cntr_2",
    "ee_cbs_written_cntr_0",
    "ee_cbs_written_cntr_3",
    "ee_deferred_eq_switch_cntr_3",
    "ee_deferred_eq_switch_cntr_1",
    "ee_deferred_eq_switch_cntr_2",
    "ee_deferred_eq_switch_cntr_0",
    "ee_eq_buffer_switch_cntr_2",
    "ee_eq_buffer_switch_cntr_0",
    "ee_eq_buffer_switch_cntr_3",
    "ee_eq_buffer_switch_cntr_1",
    "ee_eq_status_update_cntr_0",
    "ee_eq_status_update_cntr_3",
    "ee_eq_status_update_cntr_1",
    "ee_eq_status_update_cntr_2",
    "ee_eq_sw_state_wr_cntr_2",
    "ee_eq_sw_state_wr_cntr_0",
    "ee_eq_sw_state_wr_cntr_3",
    "ee_eq_sw_state_wr_cntr_1",
    "ee_events_dropped_fc_sc_cntr_3",
    "ee_events_dropped_fc_sc_cntr_1",
    "ee_events_dropped_fc_sc_cntr_2",
    "ee_events_dropped_fc_sc_cntr_0",
    "ee_events_dropped_ordinary_cntr_2",
    "ee_events_dropped_ordinary_cntr_0",
    "ee_events_dropped_ordinary_cntr_3",
    "ee_events_dropped_ordinary_cntr_1",
    "ee_events_dropped_rsrvn_cntr_1",
    "ee_events_dropped_rsrvn_cntr_2",
    "ee_events_dropped_rsrvn_cntr_0",
    "ee_events_dropped_rsrvn_cntr_3",
    "ee_events_enqueued_cntr_3",
    "ee_events_enqueued_cntr_1",
    "ee_events_enqueued_cntr_2",
    "ee_events_enqueued_cntr_0",
    "ee_expired_cbs_written_cntr_3",
    "ee_expired_cbs_written_cntr_1",
    "ee_expired_cbs_written_cntr_2",
    "ee_expired_cbs_written_cntr_0",
    "ee_partial_cbs_written_cntr_3",
    "ee_partial_cbs_written_cntr_1",
    "ee_partial_cbs_written_cntr_2",
    "ee_partial_cbs_written_cntr_0",
    "hni_discard_cntr_0",
    "hni_discard_cntr_7",
    "hni_discard_cntr_5",
    "hni_discard_cntr_3",
    "hni_discard_cntr_1",
    "hni_discard_cntr_6",
    "hni_discard_cntr_4",
    "hni_discard_cntr_2",
    "hni_llr_tx_replay_event",
    "hni_llr_rx_replay_event",
    "hni_multicast_pkts_recv_by_tc_4",
    "hni_multicast_pkts_recv_by_tc_2",
    "hni_multicast_pkts_recv_by_tc_0",
    "hni_multicast_pkts_recv_by_tc_7",
    "hni_multicast_pkts_recv_by_tc_5",
    "hni_multicast_pkts_recv_by_tc_3",
    "hni_multicast_pkts_recv_by_tc_1",
    "hni_multicast_pkts_recv_by_tc_6",
    "hni_multicast_pkts_sent_by_tc_7",
    "hni_multicast_pkts_sent_by_tc_5",
    "hni_multicast_pkts_sent_by_tc_3",
    "hni_multicast_pkts_sent_by_tc_1",
    "hni_multicast_pkts_sent_by_tc_6",
    "hni_multicast_pkts_sent_by_tc_4",
    "hni_multicast_pkts_sent_by_tc_2",
    "hni_multicast_pkts_sent_by_tc_0",
    "hni_pause_recv_0",
    "hni_pause_recv_7",
    "hni_pause_recv_5",
    "hni_pause_recv_3",
    "hni_pause_recv_1",
    "hni_pause_recv_6",
    "hni_pause_recv_4",
    "hni_pause_recv_2",
    "hni_pause_xoff_sent_5",
    "hni_pause_xoff_sent_3",
    "hni_pause_xoff_sent_1",
    "hni_pause_xoff_sent_6",
    "hni_pause_xoff_sent_4",
    "hni_pause_xoff_sent_2",
    "hni_pause_xoff_sent_0",
    "hni_pause_xoff_sent_7",
    "hni_pcs_corrected_cw",
    "hni_pcs_fecl_errors_5",
    "hni_pcs_fecl_errors_3",
    "hni_pcs_fecl_errors_1",
    "hni_pcs_fecl_errors_6",
    "hni_pcs_fecl_errors_4",
    "hni_pcs_fecl_errors_2",
    "hni_pcs_fecl_errors_0",
    "hni_pcs_fecl_errors_7",
    "hni_pcs_good_cw",
    "hni_pcs_uncorrected_cw",
    "hni_pfc_fifo_oflw_cntr_6",
    "hni_pfc_fifo_oflw_cntr_4",
    "hni_pfc_fifo_oflw_cntr_2",
    "hni_pfc_fifo_oflw_cntr_0",
    "hni_pfc_fifo_oflw_cntr_7",
    "hni_pfc_fifo_oflw_cntr_5",
    "hni_pfc_fifo_oflw_cntr_3",
    "hni_pfc_fifo_oflw_cntr_1",
    "hni_pkts_recv_by_tc_1",
    "hni_pkts_recv_by_tc_6",
    "hni_pkts_recv_by_tc_4",
    "hni_pkts_recv_by_tc_2",
    "hni_pkts_recv_by_tc_0",
    "hni_pkts_recv_by_tc_7",
    "hni_pkts_recv_by_tc_5",
    "hni_pkts_recv_by_tc_3",
    "hni_pkts_sent_by_tc_4",
    "hni_pkts_sent_by_tc_2",
    "hni_pkts_sent_by_tc_0",
    "hni_pkts_sent_by_tc_7",
    "hni_pkts_sent_by_tc_5",
    "hni_pkts_sent_by_tc_3",
    "hni_pkts_sent_by_tc_1",
    "hni_pkts_sent_by_tc_6",
    "hni_rx_ok_27",
    "hni_rx_ok_35",
    "hni_rx_ok_64",
    "hni_rx_ok_36_to_63",
    "hni_rx_ok_65_to_127",
    "hni_rx_ok_128_to_255",
    "hni_rx_ok_256_to_511",
    "hni_rx_ok_512_to_1023",
    "hni_rx_ok_1024_to_2047",
    "hni_rx_ok_2048_to_4095",
    "hni_rx_ok_4096_to_8191",
    "hni_rx_ok_8192_to_max",
    "hni_rx_paused_7",
    "hni_rx_paused_5",
    "hni_rx_paused_3",
    "hni_rx_paused_std",
    "hni_rx_paused_1",
    "hni_rx_paused_6",
    "hni_rx_paused_4",
    "hni_rx_paused_2",
    "hni_rx_paused_0",
    "hni_rx_paused_std",
    "hni_rx_stall_ixe_pktbuf_6",
    "hni_rx_stall_ixe_pktbuf_4",
    "hni_rx_stall_ixe_pktbuf_2",
    "hni_rx_stall_ixe_pktbuf_0",
    "hni_rx_stall_ixe_pktbuf_7",
    "hni_rx_stall_ixe_pktbuf_5",
    "hni_rx_stall_ixe_pktbuf_3",
    "hni_rx_stall_ixe_pktbuf_1",
    "hni_tx_ok_27",
    "hni_tx_ok_35",
    "hni_tx_ok_64",
    "hni_tx_ok_36_to_63",
    "hni_tx_ok_65_to_127",
    "hni_tx_ok_128_to_255",
    "hni_tx_ok_256_to_511",
    "hni_tx_ok_512_to_1023",
    "hni_tx_ok_1024_to_2047",
    "hni_tx_ok_2048_to_4095",
    "hni_tx_ok_4096_to_8191",
    "hni_tx_ok_8192_to_max",
    "hni_tx_paused_3",
    "hni_tx_paused_1",
    "hni_tx_paused_6",
    "hni_tx_paused_4",
    "hni_tx_paused_2",
    "hni_tx_paused_0",
    "hni_tx_paused_7",
    "hni_tx_paused_5",
    "ixe_disp_dmawr_reqs",
    "ixe_dmawr_stall_np_cdt",
    "ixe_dmawr_stall_p_cdt",
    "ixe_pool_ecn_pkts_2",
    "ixe_pool_ecn_pkts_0",
    "ixe_pool_ecn_pkts_3",
    "ixe_pool_ecn_pkts_1",
    "ixe_pool_no_ecn_pkts_2",
    "ixe_pool_no_ecn_pkts_0",
    "ixe_pool_no_ecn_pkts_3",
    "ixe_pool_no_ecn_pkts_1",
    "pi_pti_tarb_mrd_pkts",
    "pi_pti_tarb_mwr_pkts",
    "ixe_tc_req_ecn_pkts_2",
    "ixe_tc_req_ecn_pkts_0",
    "ixe_tc_req_ecn_pkts_7",
    "ixe_tc_req_ecn_pkts_5",
    "ixe_tc_req_ecn_pkts_3",
    "ixe_tc_req_ecn_pkts_1",
    "ixe_tc_req_ecn_pkts_6",
    "ixe_tc_req_ecn_pkts_4",
    "ixe_tc_req_no_ecn_pkts_6",
    "ixe_tc_req_no_ecn_pkts_4",
    "ixe_tc_req_no_ecn_pkts_2",
    "ixe_tc_req_no_ecn_pkts_0",
    "ixe_tc_req_no_ecn_pkts_7",
    "ixe_tc_req_no_ecn_pkts_5",
    "ixe_tc_req_no_ecn_pkts_3",
    "ixe_tc_req_no_ecn_pkts_1",
    "ixe_tc_rsp_ecn_pkts_6",
    "ixe_tc_rsp_ecn_pkts_4",
    "ixe_tc_rsp_ecn_pkts_2",
    "ixe_tc_rsp_ecn_pkts_0",
    "ixe_tc_rsp_ecn_pkts_7",
    "ixe_tc_rsp_ecn_pkts_5",
    "ixe_tc_rsp_ecn_pkts_3",
    "ixe_tc_rsp_ecn_pkts_1",
    "ixe_tc_rsp_no_ecn_pkts_5",
    "ixe_tc_rsp_no_ecn_pkts_3",
    "ixe_tc_rsp_no_ecn_pkts_1",
    "ixe_tc_rsp_no_ecn_pkts_6",
    "ixe_tc_rsp_no_ecn_pkts_4",
    "ixe_tc_rsp_no_ecn_pkts_2",
    "ixe_tc_rsp_no_ecn_pkts_0",
    "ixe_tc_rsp_no_ecn_pkts_7",
    "lpe_append_cmds_0",
    "lpe_append_cmds_3",
    "lpe_append_cmds_1",
    "lpe_append_cmds_2",
    "lpe_append_success_2",
    "lpe_append_success_0",
    "lpe_append_success_3",
    "lpe_append_success_1",
    "lpe_cyc_rrq_blocked_2",
    "lpe_cyc_rrq_blocked_0",
    "lpe_cyc_rrq_blocked_3",
    "lpe_cyc_rrq_blocked_1",
    "lpe_net_match_local_3",
    "lpe_net_match_local_1",
    "lpe_net_match_local_2",
    "lpe_net_match_local_0",
    "lpe_net_match_overflow_2",
    "lpe_net_match_overflow_0",
    "lpe_net_match_overflow_3",
    "lpe_net_match_overflow_1",
    "lpe_net_match_priority_2",
    "lpe_net_match_priority_0",
    "lpe_net_match_priority_3",
    "lpe_net_match_priority_1",
    "lpe_net_match_requests_3",
    "lpe_net_match_requests_1",
    "lpe_net_match_request_2",
    "lpe_net_match_request_0",
    "lpe_net_match_requests_2",
    "lpe_net_match_request_3",
    "lpe_net_match_requests_0",
    "lpe_net_match_request_1",
    "lpe_net_match_requests_3",
    "lpe_net_match_requests_1",
    "lpe_net_match_requests_2",
    "lpe_net_match_requests_0",
    "lpe_net_match_success_2",
    "lpe_net_match_success_0",
    "lpe_net_match_success_3",
    "lpe_net_match_success_1",
    "lpe_net_match_useonce_1",
    "lpe_net_match_useonce_2",
    "lpe_net_match_useonce_0",
    "lpe_net_match_useonce_3",
    "lpe_num_truncated_3",
    "lpe_num_truncated_1",
    "lpe_num_truncated_2",
    "lpe_num_truncated_0",
    "lpe_rndzv_puts_2",
    "lpe_rndzv_puts_offloaded_3",
    "lpe_rndzv_puts_0",
    "lpe_rndzv_puts_offloaded_1",
    "lpe_rndzv_puts_3",
    "lpe_rndzv_puts_1",
    "lpe_rndzv_puts_offloaded_2",
    "lpe_rndzv_puts_offloaded_0",
    "lpe_rndzv_puts_offloaded_3",
    "lpe_rndzv_puts_offloaded_1",
    "lpe_rndzv_puts_offloaded_2",
    "lpe_rndzv_puts_offloaded_0",
    "lpe_search_nid_any_2",
    "lpe_search_nid_any_0",
    "lpe_search_nid_any_3",
    "lpe_search_nid_any_1",
    "lpe_search_pid_any_3",
    "lpe_search_pid_any_1",
    "lpe_search_pid_any_2",
    "lpe_search_pid_any_0",
    "lpe_search_rank_any_3",
    "lpe_search_rank_any_1",
    "lpe_search_rank_any_2",
    "lpe_search_rank_any_0",
    "lpe_setstate_cmds_0",
    "lpe_setstate_cmds_3",
    "lpe_setstate_cmds_1",
    "lpe_setstate_cmds_2",
    "lpe_setstate_success_3",
    "lpe_setstate_success_1",
    "lpe_setstate_success_2",
    "lpe_setstate_success_0",
    "lpe_unexpected_get_amo_2",
    "lpe_unexpected_get_amo_0",
    "lpe_unexpected_get_amo_3",
    "lpe_unexpected_get_amo_1",
    "lpe_sts_net_match_attempts_2",
    "lpe_sts_net_match_attempts_0",
    "lpe_sts_net_match_attempts_3",
    "lpe_sts_net_match_attempts_1",
    "lpe_sts_app_match_attempts_2",
    "lpe_sts_app_match_attempts_0",
    "lpe_sts_app_match_attempts_3",
    "lpe_sts_app_match_attempts_1",
    "lpe_sts_net_max_attempts_max_1",
    "lpe_sts_net_max_attempts_num_pbuf_entries_2",
    "lpe_sts_net_max_attempts_max_2",
    "lpe_sts_net_max_attempts_num_pbuf_entries_0",
    "lpe_sts_net_max_attempts_max_0",
    "lpe_sts_net_max_attempts_num_pbuf_entries_3",
    "lpe_sts_net_max_attempts_max_3",
    "lpe_sts_net_max_attempts_num_pbuf_entries_1",
    "lpe_sts_app_max_attempts_max_2",
    "lpe_sts_app_max_attempts_max_0",
    "lpe_sts_app_max_attempts_num_pbuf_entries_2",
    "lpe_sts_app_max_attempts_num_pbuf_entries_0",
    "lpe_sts_app_max_attempts_max_3",
    "lpe_sts_app_max_attempts_max_1",
    "lpe_sts_app_max_attempts_num_pbuf_entries_3",
    "lpe_sts_app_max_attempts_num_pbuf_entries_1",
    "lpe_amo_cmds",
    "lpe_famo_cmds",
    "mb_cmc_axi_rd_requests_3",
    "mb_cmc_axi_rd_requests_1",
    "mb_cmc_axi_rd_requests_2",
    "mb_cmc_axi_rd_requests_0",
    "mb_cmc_axi_wr_requests_1",
    "mb_cmc_axi_wr_requests_2",
    "mb_cmc_axi_wr_requests_0",
    "mb_cmc_axi_wr_requests_3",
    "mb_crmc_axi_rd_requests_1",
    "mb_crmc_axi_rd_requests_2",
    "mb_crmc_axi_rd_requests_0",
    "mb_crmc_axi_wr_requests_2",
    "mb_crmc_axi_wr_requests_0",
    "mb_crmc_axi_wr_requests_1",
    "mb_crmc_rd_error_2",
    "mb_crmc_rd_error_0",
    "mb_crmc_rd_error_1",
    "mb_crmc_ring_mbe_1",
    "mb_crmc_ring_mbe_2",
    "mb_crmc_ring_mbe_0",
    "mb_crmc_ring_rd_requests_1",
    "mb_crmc_ring_rd_requests_2",
    "mb_crmc_ring_rd_requests_0",
    "mb_crmc_ring_sbe_2",
    "mb_crmc_ring_sbe_0",
    "mb_crmc_ring_sbe_1",
    "mb_crmc_ring_wr_requests_2",
    "mb_crmc_ring_wr_requests_0",
    "mb_crmc_ring_wr_requests_1",
    "mb_crmc_wr_error_1",
    "mb_crmc_wr_error_2",
    "mb_crmc_wr_error_0",
    "oxe_channel_idle",
    "oxe_mcu_meas_42",
    "oxe_mcu_meas_70",
    "oxe_mcu_meas_32",
    "oxe_mcu_meas_60",
    "oxe_mcu_meas_89",
    "oxe_mcu_meas_22",
    "oxe_mcu_meas_50",
    "oxe_mcu_meas_79",
    "oxe_mcu_meas_12",
    "oxe_mcu_meas_40",
    "oxe_mcu_meas_8",
    "oxe_mcu_meas_69",
    "oxe_mcu_meas_30",
    "oxe_mcu_meas_59",
    "oxe_mcu_meas_87",
    "oxe_mcu_meas_20",
    "oxe_mcu_meas_49",
    "oxe_mcu_meas_77",
    "oxe_mcu_meas_10",
    "oxe_mcu_meas_39",
    "oxe_mcu_meas_6",
    "oxe_mcu_meas_67",
    "oxe_mcu_meas_95",
    "oxe_mcu_meas_29",
    "oxe_mcu_meas_57",
    "oxe_mcu_meas_85",
    "oxe_mcu_meas_19",
    "oxe_mcu_meas_47",
    "oxe_mcu_meas_75",
    "oxe_mcu_meas_37",
    "oxe_mcu_meas_4",
    "oxe_mcu_meas_65",
    "oxe_mcu_meas_93",
    "oxe_mcu_meas_27",
    "oxe_mcu_meas_55",
    "oxe_mcu_meas_83",
    "oxe_mcu_meas_17",
    "oxe_mcu_meas_45",
    "oxe_mcu_meas_73",
    "oxe_mcu_meas_35",
    "oxe_mcu_meas_2",
    "oxe_mcu_meas_63",
    "oxe_mcu_meas_91",
    "oxe_mcu_meas_25",
    "oxe_mcu_meas_53",
    "oxe_mcu_meas_81",
    "oxe_mcu_meas_15",
    "oxe_mcu_meas_43",
    "oxe_mcu_meas_71",
    "oxe_mcu_meas_33",
    "oxe_mcu_meas_0",
    "oxe_mcu_meas_61",
    "oxe_mcu_meas_23",
    "oxe_mcu_meas_51",
    "oxe_mcu_meas_13",
    "oxe_mcu_meas_41",
    "oxe_mcu_meas_9",
    "oxe_mcu_meas_31",
    "oxe_mcu_meas_88",
    "oxe_mcu_meas_21",
    "oxe_mcu_meas_78",
    "oxe_mcu_meas_11",
    "oxe_mcu_meas_7",
    "oxe_mcu_meas_68",
    "oxe_mcu_meas_58",
    "oxe_mcu_meas_86",
    "oxe_mcu_meas_48",
    "oxe_mcu_meas_76",
    "oxe_mcu_meas_38",
    "oxe_mcu_meas_5",
    "oxe_mcu_meas_66",
    "oxe_mcu_meas_94",
    "oxe_mcu_meas_28",
    "oxe_mcu_meas_56",
    "oxe_mcu_meas_84",
    "oxe_mcu_meas_18",
    "oxe_mcu_meas_46",
    "oxe_mcu_meas_74",
    "oxe_mcu_meas_36",
    "oxe_mcu_meas_3",
    "oxe_mcu_meas_64",
    "oxe_mcu_meas_92",
    "oxe_mcu_meas_26",
    "oxe_mcu_meas_54",
    "oxe_mcu_meas_82",
    "oxe_mcu_meas_16",
    "oxe_mcu_meas_44",
    "oxe_mcu_meas_72",
    "oxe_mcu_meas_34",
    "oxe_mcu_meas_1",
    "oxe_mcu_meas_62",
    "oxe_mcu_meas_90",
    "oxe_mcu_meas_24",
    "oxe_mcu_meas_52",
    "oxe_mcu_meas_80",
    "oxe_mcu_meas_14",
    "oxe_ptl_tx_get_msgs_tsc_2",
    "oxe_ptl_tx_get_msgs_tsc_0",
    "oxe_ptl_tx_get_msgs_tsc_9",
    "oxe_ptl_tx_get_msgs_tsc_7",
    "oxe_ptl_tx_get_msgs_tsc_5",
    "oxe_ptl_tx_get_msgs_tsc_3",
    "oxe_ptl_tx_get_msgs_tsc_1",
    "oxe_ptl_tx_get_msgs_tsc_8",
    "oxe_ptl_tx_get_msgs_tsc_6",
    "oxe_ptl_tx_get_msgs_tsc_4",
    "oxe_ptl_tx_put_msgs_tsc_0",
    "oxe_ptl_tx_put_msgs_tsc_9",
    "oxe_ptl_tx_put_msgs_tsc_7",
    "oxe_ptl_tx_put_msgs_tsc_5",
    "oxe_ptl_tx_put_msgs_tsc_3",
    "oxe_ptl_tx_put_msgs_tsc_1",
    "oxe_ptl_tx_put_msgs_tsc_8",
    "oxe_ptl_tx_put_msgs_tsc_6",
    "oxe_ptl_tx_put_msgs_tsc_4",
    "oxe_ptl_tx_put_msgs_tsc_2",
    "oxe_ptl_tx_put_pkts_tsc_3",
    "oxe_ptl_tx_put_pkts_tsc_1",
    "oxe_ptl_tx_put_pkts_tsc_8",
    "oxe_ptl_tx_put_pkts_tsc_6",
    "oxe_ptl_tx_put_pkts_tsc_4",
    "oxe_ptl_tx_put_pkts_tsc_2",
    "oxe_ptl_tx_put_pkts_tsc_0",
    "oxe_ptl_tx_put_pkts_tsc_9",
    "oxe_ptl_tx_put_pkts_tsc_7",
    "oxe_ptl_tx_put_pkts_tsc_5",
    "oxe_stall_fgfc_blk_2",
    "oxe_stall_fgfc_blk_0",
    "oxe_stall_fgfc_blk_3",
    "oxe_stall_fgfc_blk_1",
    "oxe_stall_fgfc_cntrl_1",
    "oxe_stall_fgfc_cntrl_2",
    "oxe_stall_fgfc_cntrl_0",
    "oxe_stall_fgfc_cntrl_3",
    "oxe_stall_fgfc_end_2",
    "oxe_stall_fgfc_end_0",
    "oxe_stall_fgfc_end_3",
    "oxe_stall_fgfc_end_1",
    "oxe_stall_fgfc_start_3",
    "oxe_stall_fgfc_start_1",
    "oxe_stall_fgfc_start_2",
    "oxe_stall_fgfc_start_0",
    "oxe_stall_idc_no_buff_bc_1",
    "oxe_stall_idc_no_buff_bc_8",
    "oxe_stall_idc_no_buff_bc_6",
    "oxe_stall_idc_no_buff_bc_4",
    "oxe_stall_idc_no_buff_bc_2",
    "oxe_stall_idc_no_buff_bc_0",
    "oxe_stall_idc_no_buff_bc_9",
    "oxe_stall_idc_no_buff_bc_7",
    "oxe_stall_idc_no_buff_bc_5",
    "oxe_stall_idc_no_buff_bc_3",
    "oxe_stall_pbuf_bc_5",
    "oxe_stall_pbuf_bc_3",
    "oxe_stall_pbuf_bc_1",
    "oxe_stall_pbuf_bc_8",
    "oxe_stall_pbuf_bc_6",
    "oxe_stall_pbuf_bc_4",
    "oxe_stall_pbuf_bc_2",
    "oxe_stall_pbuf_bc_0",
    "oxe_stall_pbuf_bc_9",
    "oxe_stall_pbuf_bc_7",
    "oxe_stall_pct_bc_2",
    "oxe_stall_pct_bc_0",
    "oxe_stall_pct_bc_9",
    "oxe_stall_pct_bc_7",
    "oxe_stall_pct_bc_5",
    "oxe_stall_pct_bc_3",
    "oxe_stall_pct_bc_1",
    "oxe_stall_pct_bc_8",
    "oxe_stall_pct_bc_6",
    "oxe_stall_pct_bc_4",
    "oxe_stall_ts_no_in_crd_tsc_4",
    "oxe_stall_ts_no_in_crd_tsc_2",
    "oxe_stall_ts_no_in_crd_tsc_0",
    "oxe_stall_ts_no_in_crd_tsc_9",
    "oxe_stall_ts_no_in_crd_tsc_7",
    "oxe_stall_ts_no_in_crd_tsc_5",
    "oxe_stall_ts_no_in_crd_tsc_3",
    "oxe_stall_ts_no_in_crd_tsc_1",
    "oxe_stall_ts_no_in_crd_tsc_8",
    "oxe_stall_ts_no_in_crd_tsc_6",
    "oxe_stall_ts_no_out_crd_tsc_9",
    "oxe_stall_ts_no_out_crd_tsc_7",
    "oxe_stall_ts_no_out_crd_tsc_5",
    "oxe_stall_ts_no_out_crd_tsc_3",
    "oxe_stall_ts_no_out_crd_tsc_1",
    "oxe_stall_ts_no_out_crd_tsc_8",
    "oxe_stall_ts_no_out_crd_tsc_6",
    "oxe_stall_ts_no_out_crd_tsc_4",
    "oxe_stall_ts_no_out_crd_tsc_2",
    "oxe_stall_ts_no_out_crd_tsc_0",
    "oxe_stall_wr_conflict_pkt_buff_bnk_2",
    "oxe_stall_wr_conflict_pkt_buff_bnk_0",
    "oxe_stall_wr_conflict_pkt_buff_bnk_3",
    "oxe_stall_wr_conflict_pkt_buff_bnk_1",
    "parbs_tarb_pi_posted_pkts",
    "parbs_tarb_pi_posted_blocked_cnt",
    "parbs_tarb_pi_non_posted_pkts",
    "parbs_tarb_pi_non_posted_blocked_cnt",
    "pct_conn_sct_open",
    "pct_host_access_latency_4",
    "pct_host_access_latency_14",
    "pct_host_access_latency_2",
    "pct_host_access_latency_12",
    "pct_host_access_latency_0",
    "pct_host_access_latency_9",
    "pct_host_access_latency_10",
    "pct_host_access_latency_7",
    "pct_host_access_latency_5",
    "pct_host_access_latency_15",
    "pct_host_access_latency_3",
    "pct_host_access_latency_13",
    "pct_host_access_latency_1",
    "pct_host_access_latency_11",
    "pct_host_access_latency_8",
    "pct_host_access_latency_6",
    "pct_mst_hit_on_som",
    "pct_no_tct_nacks",
    "pct_no_trs_nacks",
    "pct_no_mst_nacks",
    "pct_req_ordered",
    "pct_req_unordered",
    "pct_req_rsp_latency_22",
    "pct_req_rsp_latency_3",
    "pct_req_rsp_latency_12",
    "pct_req_rsp_latency_30",
    "pct_req_rsp_latency_20",
    "pct_req_rsp_latency_1",
    "pct_req_rsp_latency_10",
    "pct_req_rsp_latency_29",
    "pct_req_rsp_latency_19",
    "pct_req_rsp_latency_27",
    "pct_req_rsp_latency_8",
    "pct_req_rsp_latency_17",
    "pct_req_rsp_latency_25",
    "pct_req_rsp_latency_6",
    "pct_req_rsp_latency_15",
    "pct_req_rsp_latency_23",
    "pct_req_rsp_latency_4",
    "pct_req_rsp_latency_13",
    "pct_req_rsp_latency_31",
    "pct_req_rsp_latency_21",
    "pct_req_rsp_latency_2",
    "pct_req_rsp_latency_11",
    "pct_req_rsp_latency_0",
    "pct_req_rsp_latency_28",
    "pct_req_rsp_latency_9",
    "pct_req_rsp_latency_18",
    "pct_req_rsp_latency_26",
    "pct_req_rsp_latency_7",
    "pct_req_rsp_latency_16",
    "pct_req_rsp_latency_24",
    "pct_req_rsp_latency_5",
    "pct_req_rsp_latency_14",
    "pct_responses_received",
    "pct_retry_srb_requests",
    "pct_sct_timeouts",
    "pct_spt_timeouts",
    "pct_trs_rsp_nack_drops",
    "tou_ct_cmd_counts_7",
    "tou_ct_cmd_counts_5",
    "tou_ct_cmd_counts_15",
    "tou_ct_cmd_counts_3",
    "tou_ct_cmd_counts_13",
    "tou_ct_cmd_counts_1",
    "tou_ct_cmd_counts_11",
    "tou_ct_cmd_counts_8",
    "tou_ct_cmd_counts_6",
    "tou_ct_cmd_counts_4",
    "tou_ct_cmd_counts_14",
    "tou_ct_cmd_counts_2",
    "tou_ct_cmd_counts_12",
    "tou_ct_cmd_counts_0",
    "tou_ct_cmd_counts_10",
    "tou_ct_cmd_counts_9",
    "tou_num_list_rebuilds_3",
    "tou_num_list_rebuilds_1",
    "tou_num_list_rebuilds_2",
    "tou_num_list_rebuilds_0",
    "tou_num_trig_cmds_0",
    "tou_num_trig_cmds_3",
    "tou_num_trig_cmds_1",
    "tou_num_trig_cmds_2",
    "rh:connections_cancelled",
    "rh:nack_no_matching_conn",
    "rh:nack_no_target_conn",
    "rh:nack_no_target_mst",
    "rh:nack_no_target_trs",
    "rh:nack_resource_busy",
    "rh:nacks",
    "rh:nack_sequence_error",
    "rh:pkts_cancelled_o",
    "rh:pkts_cancelled_u",
    "rh:sct_in_use",
    "rh:sct_timeouts",
    "rh:spt_timeouts",
    "rh:spt_timeouts_0",
    "rh:spt_timeouts_u",
    "rh:tct_timeouts"
};

// ── Default active counters (only those typically nonzero) ──────────────────
static const std::vector<std::string> default_counter_names = {
    "rh:sct_timeouts",
    "rh:spt_timeouts",
    "rh:spt_timeouts_o",
    "rh:spt_timeouts_u",
    "rh:tct_timeouts",
    "atu_cache_evictions",
    "atu_cache_hit_base_page_size_0",
    "atu_cache_hit_derivative1_page_size_0",
    "lpe_net_match_priority_0",
    "lpe_net_match_overflow_0",
    "lpe_net_match_request_0",
    "lpe_rndzv_puts_0",
    "lpe_rndzv_puts_offloaded_0",
    "hni_rx_paused_0",
    "hni_rx_paused_1",
    "hni_tx_paused_0",
    "hni_tx_paused_1",
    "parbs_tarb_pi_posted_pkts",
    "parbs_tarb_pi_posted_blocked_cnt",
    "parbs_tarb_pi_non_posted_pkts",
    "parbs_tarb_pi_non_posted_blocked_cnt",
    "pct_no_tct_nacks",
    "pct_trs_rsp_nack_drops",
    "pct_mst_hit_on_som",
    "rh:connections_cancelled",
    "rh:nack_no_matching_conn",
    "rh:nack_no_target_conn",
    "rh:nack_no_target_mst",
    "rh:nack_no_target_trs",
    "rh:nack_resource_busy",
    "rh:nacks",
    "rh:nack_sequence_error",
    "rh:pkts_cancelled_o",
    "rh:pkts_cancelled_u",
    "rh:sct_in_use"
};

static const std::vector<std::string> timeout_counter_names = {
    "rh:sct_timeouts",
    "rh:spt_timeouts",
    "rh:spt_timeouts_o",
    "rh:spt_timeouts_u",
    "rh:tct_timeouts"
};


// Active counter list — initialised by init_counter_filter()
std::vector<std::string> counter_names = all_counter_names;  // safe default until init

// ── Counter filtering ───────────────────────────────────────────────────────

void init_counter_filter(int level) {
   
    if (level == 1) {
        counter_names = timeout_counter_names;
        return;
    }
    const char* env = getenv("GATHER_CXI_COUNTERS");
    if (env && std::string(env) == "all") {
        counter_names = all_counter_names;
        return;
    }
    if (env && env[0] != '\0') {
        // Parse comma-separated list
        std::set<std::string> valid(all_counter_names.begin(), all_counter_names.end());
        std::vector<std::string> filtered;
        std::istringstream ss(env);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // trim whitespace
            token.erase(0, token.find_first_not_of(" \t"));
            token.erase(token.find_last_not_of(" \t") + 1);
            if (valid.count(token)) {
                filtered.push_back(token);
            } else {
                std::cerr << "[counter_filter] WARNING: unknown counter '" << token
                          << "' — skipping" << std::endl;
            }
        }
        if (!filtered.empty()) {
            counter_names = filtered;
        } else {
            std::cerr << "[counter_filter] WARNING: no valid counters in GATHER_CXI_COUNTERS, "
                      << "using defaults" << std::endl;
            counter_names = default_counter_names;
        }
    } else {
        // No env var set → use defaults (nonzero-only)
        counter_names = default_counter_names;
    }
}

// ── FD-cached reader ────────────────────────────────────────────────────────
//
// Instead of open()/read()/close() per counter per interval, we open all
// sysfs and /run/cxi files once at startup and thereafter just lseek()+read().
// This eliminates 2 of 3 syscalls per counter read.

struct CachedCounterFd {
    int fd;           // open file descriptor (-1 if unavailable)
    std::string path; // for diagnostics
};

// Per-NIC, per-counter fd cache.  Indexed [nic_idx][counter_idx].
static std::vector<std::string> cached_devices;
static std::vector<std::vector<CachedCounterFd>> cached_fds;  // [device][counter]
static bool fds_initialised = false;

static std::vector<std::string> discover_hsn_devices() {
    std::vector<std::string> devices;
    DIR* dir = opendir("/sys/class/net");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name.substr(0, 3) == "hsn") {
                devices.push_back(name);
            }
        }
        closedir(dir);
    }
    std::sort(devices.begin(), devices.end());
    return devices;
}

static int device_num_from_name(const std::string& device) {
    if (device.size() > 3 && device.substr(0, 3) == "hsn") {
        try { return std::stoi(device.substr(3)); } catch (...) {}
    }
    return 0;
}

static std::string counter_path(const std::string& device, int device_num,
                                const std::string& counter) {
    if (counter.substr(0, 3) == "rh:") {
        return "/run/cxi/cxi" + std::to_string(device_num) + "/" + counter.substr(3);
    }
    return "/sys/class/net/" + device + "/device/telemetry/" + counter;
}

void init_counter_fds() {
    cleanup_counter_fds();
    cached_devices = discover_hsn_devices();
    cached_fds.resize(cached_devices.size());

    for (size_t d = 0; d < cached_devices.size(); ++d) {
        int dev_num = device_num_from_name(cached_devices[d]);
        cached_fds[d].resize(counter_names.size());
        for (size_t c = 0; c < counter_names.size(); ++c) {
            std::string path = counter_path(cached_devices[d], dev_num, counter_names[c]);
            int fd = open(path.c_str(), O_RDONLY);
            cached_fds[d][c] = {fd, path};
        }
    }
    fds_initialised = true;
}

void cleanup_counter_fds() {
    for (auto& dev_fds : cached_fds) {
        for (auto& cf : dev_fds) {
            if (cf.fd >= 0) close(cf.fd);
        }
    }
    cached_fds.clear();
    cached_devices.clear();
    fds_initialised = false;
}

// Read a single counter value from a cached fd.  Returns 0 on failure.
static uint64_t read_counter_fd(int fd) {
    if (fd < 0) return 0;
    // Seek to start and re-read
    if (lseek(fd, 0, SEEK_SET) != 0) return 0;
    char buf[64];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';
    // Parse: value is before '@' (format: "12345@0" in sysfs telemetry)
    char* at = strchr(buf, '@');
    if (at) {
        *at = '\0';
        return strtoull(buf, nullptr, 10);
    }
    // /run/cxi files may just be a plain number
    return strtoull(buf, nullptr, 10);
}

// ── Read functions ──────────────────────────────────────────────────────────

// ── Read functions ──────────────────────────────────────────────────────────

std::map<std::string, std::vector<uint64_t>> read_all_counters_per_nic() {
    std::map<std::string, std::vector<uint64_t>> nic_counters;

    if (fds_initialised) {
        // ── Fast path: cached fds ──
        for (size_t d = 0; d < cached_devices.size(); ++d) {
            std::vector<uint64_t> values(counter_names.size(), 0);
            for (size_t c = 0; c < counter_names.size(); ++c) {
                values[c] = read_counter_fd(cached_fds[d][c].fd);
            }
            nic_counters[cached_devices[d]] = values;
        }
    } else {
        // ── Fallback: open/read/close (used before init_counter_fds) ──
        auto devices = discover_hsn_devices();
        for (const auto& device : devices) {
            std::vector<uint64_t> values(counter_names.size(), 0);
            int dev_num = device_num_from_name(device);
            for (size_t i = 0; i < counter_names.size(); ++i) {
                std::string filepath = counter_path(device, dev_num, counter_names[i]);
                std::ifstream file(filepath);
                if (!file) continue;
                std::string line;
                std::getline(file, line);
                size_t at_pos = line.find('@');
                if (at_pos != std::string::npos) {
                    values[i] = std::stoull(line.substr(0, at_pos));
                }
            }
            nic_counters[device] = values;
        }
    }
    return nic_counters;
}

std::vector<uint64_t> read_all_counters() {
    std::vector<uint64_t> values(counter_names.size(), 0);

    if (fds_initialised) {
        for (size_t d = 0; d < cached_devices.size(); ++d) {
            for (size_t c = 0; c < counter_names.size(); ++c) {
                values[c] += read_counter_fd(cached_fds[d][c].fd);
            }
        }
    } else {
        auto devices = discover_hsn_devices();
        for (const auto& device : devices) {
            int dev_num = device_num_from_name(device);
            for (size_t i = 0; i < counter_names.size(); ++i) {
                std::string filepath = counter_path(device, dev_num, counter_names[i]);
                std::ifstream file(filepath);
                if (!file) continue;
                std::string line;
                std::getline(file, line);
                size_t at_pos = line.find('@');
                if (at_pos != std::string::npos) {
                    values[i] += std::stoull(line.substr(0, at_pos));
                }
            }
        }
    }
    return values;
}

uint64_t subtract_56_bit_integers(uint64_t final, uint64_t initial) {
    const uint64_t MASK_56 = (1ULL << 56) - 1;
    uint64_t f = final & MASK_56;
    uint64_t i = initial & MASK_56;
    if (f >= i) {
        return f - i;
    } else {
        return (1ULL << 56) + f - i;
    }
}
