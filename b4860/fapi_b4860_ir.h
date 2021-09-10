/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef FAPI_B4860_IR_H
#define FAPI_B4860_IR_H

#include <stdint.h>

#define IR_MSG_RRU_PARAMETERS_CONFIGURATION_REQ 61
#define IR_MSG_RRU_PARAMETERS_CONFIGURATION_RESP 62
#define IR_MSG_RRU_INIT_CALIBRATION_REPORT 71
#define IR_MSG_RRU_PERIODIC_CALIBRATION_REQ 81
#define IR_MSG_RRU_PERIODIC_CALIBRATION_RESP 82
#define IR_MSG_RRU_DELAY_MEASURE_REQ 101
#define IR_MSG_RRU_DELAY_MEASURE_RESP 102
#define IR_MSG_RRU_DELAY_CONFIG_REQ 103
#define IR_MSG_RRU_DELAY_CONFIG_RESP 104
#define IR_MSG_RRU_ALARM_REPORT 111
#define IR_MSG_RRU_RESET_REQ 141
#define IR_MSG_RRU_CELL_CONFIGURATION_REQ 193
#define IR_MSG_RRU_CELL_CONFIGURATION_RESP 194
#define IR_MSG_RRU_RESET_EVENT 221

// Ir common message header
typedef struct ir_msg_header_s
{
	uint32_t msg_no;
	uint32_t msg_len;
	uint8_t rru_id;
	uint8_t bbu_id;
	uint8_t port;
	uint32_t flow;
} __attribute__((__packed__)) ir_msg_header_t;

typedef struct ir_ie_header_s
{
	uint16_t ie_no;
	uint16_t ie_len;
} __attribute__((__packed__)) ir_ie_header_t;

// RRU identification, 100b
typedef struct ir_ie_1_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t rru_manufacturer[16];
	uint8_t rru_vendor[16];
	uint8_t serial[16];
	uint8_t date_prod[16];
	uint8_t date_service[16];
	uint8_t info[16];
} __attribute__((__packed__)) ir_ie_1_t;

// Channel establish reason, 9b
typedef struct ir_ie_2_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t reason;
	uint32_t alarm_code;
} __attribute__((__packed__)) ir_ie_2_t;

// RRU capability, 31b
typedef struct ir_ie_3_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint32_t td_scdma_carriers;
	uint32_t lte_carriers;
	uint8_t n_ant;
	uint16_t max_power;        // in 1/256dBm
	uint8_t master;            // 0 - Main, 1 - From
	uint32_t dl_calrru_max;    // in ns
	uint32_t ul_calrru_max;    // in ns
	uint16_t patterns;         // bit0 - TDD-LTE, bit1 - TD-SCDMA, bit2 - FDD-LTE, bit3-15 - reserved
	uint8_t indep_ant_cal;     // 1 - support independent calibration
	uint8_t main_corr_channel; // When the BBU sends the calibration sequence to the RRU, the selection of the
	                           // downstream channel. 0: Select the first channel 1: Select the second channel 2: Select
	                           // the 3rd channel And so on 0xFF: Invalid Value
	uint8_t prep_corr_channel; // same as above
	uint8_t main_harvest_corr_channel; // same as above
	uint8_t pickup_corr_channel;       // same as above
} __attribute__((__packed__)) ir_ie_3_t;

// RRU sync (???), 5b
typedef struct ir_ie_4_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t n_syn; // 1-N indicates 1 to N Grade RRU, respectively
} __attribute__((__packed__)) ir_ie_4_t;

// RRU hardware type and version information, 52b
typedef struct ir_ie_5_s
{
	uint16_t ie_no;
	uint16_t ie_len;

	uint8_t hardware_type[32];
	/* 1 to 16 bytes: factory identification (space sits later)
	    17 to 18 bytes: Number of antennas
	    19 to 20 bytes: Carriers
	    21 to 22 bytes: maximum transmit power
	    23 to 32 bytes: The rest of the manufacturers custom
	*/

	uint8_t hardware_version[16];
	/* RRU hardware version number format: vendor number, HW plus version number,
	    vendor number is 2 16-in-one characters. */

} __attribute__((__packed__)) ir_ie_5_t;

// RRU Software Version Information IE, 84b
typedef struct ir_ie_6_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t software_version[40];
	/* RRU software version number format: vendor number, sW plus version number. */

	uint8_t formware_version[40];
	/* Firmware software version number format: vendor number and FW plus version number. */
} __attribute__((__packed__)) ir_ie_6_t;

// RRU Band Capability IE, 13b
typedef struct ir_ie_7_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint16_t freq_start;     // in 100kHz, boundary
	uint16_t freq_stop;      // in 100kHz, boundary
	uint16_t td_scdma_width; // in 200 kHz
	uint8_t td_scdma_carriers;
	uint8_t lte_carriers;
	uint8_t band_interval; // The number of the Band Interval Number rRU for this band interval.
} __attribute__((__packed__)) ir_ie_7_t;

// RRU RF Channel Capability IE, 15b
typedef struct ir_ie_8_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t chan_no; // The "RF Channel Number" is numbered from 1.
	uint8_t ant_no;  // which antenna the RF channel is integrated into.
	uint8_t td_scdma_carrier_start;
	uint8_t td_scdma_carrier_stop;
	uint8_t lte_carrier_start;
	uint8_t lte_carrier_stop;
	uint16_t max_power; // in 1/256dBm
	uint16_t band_interval;
	/* Bit0: corresponding band interval 0
	    Bit1: Corresponding band interval 1
	    ...
	    Bit15: corresponding band interval 15
	*/
	uint8_t send_recv_props; // LTE only
							 /* 1: Receive
	                             2: Send
	                             3: Send and receive
	                         */
} __attribute__((__packed__)) ir_ie_8_t;

// Carrier Capability Combination IE, 12b
typedef struct ir_ie_9_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t ch_no;
	uint8_t band_capability;
	uint8_t num_1_4_carriers;
	uint8_t num_3_carriers;
	uint8_t num_5_carriers;
	uint8_t num_10_carriers;
	uint8_t num_15_carriers;
	uint8_t num_20_carriers;
	/* For example, support for 1 20M LTE carrier, then "20 M carrier number" fill 1,
	    the remaining fields fill in 0;
	    For example, support 4 5MLTE carriers, then "5 M carrier number" fill 4,
	    the remaining fields fill in 0;
	    For example, 1 10M LTE carrier and 2 5M LTE carriers are supported, then
	    "10M carrier number" fills 1, "5M carrier number" fills 2, and the remaining
	    fields fill in 0;
	*/
} __attribute__((__packed__)) ir_ie_9_t;

// System time, 11b
typedef struct ir_ie_11_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t second;
	uint8_t minute;
	uint8_t hour;
	uint8_t day;
	uint8_t month;
	uint16_t year; // 1997-2099
} __attribute__((__packed__)) ir_ie_11_t;

// Access answer address IE, 8b
typedef struct ir_ie_12_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t bbu_ftp_addr[4];
	/* When the BBU's IP address is not the same as the IP address of the FTP server,
	    the GATEWAY's IP address is the BBU's IP address.
	*/
} __attribute__((__packed__)) ir_ie_12_t;

// RRU operating mode setting IE, 8b
typedef struct ir_ie_13_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint32_t status;
	/* 0: RRU up and running
	    1: Test status
	    2: (RRU dual-modnetunre normal state)
	    3: (RRU dual-modal network test normal state)
	    Note: Changing the RRU operating mode requires a restart of the RRU.
	*/
} __attribute__((__packed__)) ir_ie_13_t;

// Software version check result, 289b
typedef struct ir_ie_14_s
{
	uint16_t ie_no;
	uint16_t ie_len;

	uint8_t software_type; // 0 - software, 1 - firmware
	uint32_t result;       // 0 - OK, 1 - inconsistent
	uint8_t file_path[200];
	uint8_t file_name[16];
	uint32_t file_length;
	uint8_t file_time[20];
	uint8_t file_version[40];
} __attribute__((__packed__)) ir_ie_14_t;

// Channel establishment configuration response IE, 8b
typedef struct ir_ie_21_s
{
	uint16_t ie_no;
	uint16_t ie_len;

	uint32_t result;

} __attribute__((__packed__)) ir_ie_21_t;

// RF channel status response, 13b
typedef struct ir_ie_352_s
{
	uint16_t ie_no;
	uint16_t ie_len;

	uint32_t uplink_status;
	/*
	    0 - not enabled
	    1 - enabled, no fault
	    2 - enabled, faulty
	*/

	uint32_t downlink_status; // same as uplink
} __attribute__((__packed__)) ir_ie_352_t;

// IQ data channel configuration IE, 8b
typedef struct ir_ie_501_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t carrier;
	uint8_t antenna;
	uint8_t axc;
	uint8_t fiber;
} __attribute__((__packed__)) ir_ie_501_t;

// Ir port working mode configuration, 8b
typedef struct ir_ie_504_s
{
	uint16_t ie_no;
	uint16_t ie_len;

	uint32_t mode;
	/*
	    1: normal mode
	    2: cascade mode
	    3: standby mode
	    4: Load sharing m
	*/
} __attribute__((__packed__)) ir_ie_504_t;

// Antenna configuration IE, 8b
typedef struct ir_ie_507_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t mode;      // 1 - smart antenna, 2 - distributed antenna
	uint8_t group;     // 1-8
	uint16_t uplink;   // 0 - enable, 1 - disable, bits 0-7 = ant 1-8
	uint16_t downlink; // 0 - enable, 1 - disable, bits 0-7 = ant 1-8
} __attribute__((__packed__)) ir_ie_507_t;

// Calibration request IE, 10b
typedef struct ir_ie_701_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint32_t cell_id;
	uint8_t ant_group;
	uint8_t state;
} __attribute__((__packed__)) ir_ie_701_t;

// Delay measure request IE, 5b
typedef struct ir_ie_901_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t port;
} __attribute__((__packed__)) ir_ie_901_t;

// Delay measure response IE, 29b
typedef struct ir_ie_911_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t port;
	uint32_t t_offset;
	uint32_t tb_delay_dl;
	uint32_t tb_delay_ul;
	uint32_t t2a;
	uint32_t ta3;
	uint32_t N;
} __attribute__((__packed__)) ir_ie_911_t;

// Delay configuration request IE, 25b
typedef struct ir_ie_921_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t port;
	uint32_t t12;
	uint32_t t32;
	uint32_t dl_offset;
	uint32_t dl_cal_rru;
	uint32_t ul_cal_rru;
} __attribute__((__packed__)) ir_ie_921_t;

// Delay configuration response IE, 6b
typedef struct ir_ie_931_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t port;
	uint8_t result;
} __attribute__((__packed__)) ir_ie_931_t;

// RRU reset IE, 8b
typedef struct ir_ie_1301_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint32_t reset_type;
} __attribute__((__packed__)) ir_ie_1301_t;

// Cell configuration, 13b
typedef struct ir_ie_1501_s
{
	uint16_t ie_no;
	uint16_t ie_len;

	uint8_t reason; // 0: establish, 1: reconfig, 2: delete
	uint32_t cell_id;
	uint16_t power;    // 1/256
	uint8_t ant_group; // antenna group, 1-8
	uint8_t n_freqs;   // number of IE 1502 for this cell
} __attribute__((__packed__)) ir_ie_1501_t;

// Frequency point configuration, TDD, 29b
typedef struct ir_ie_1502_tdd_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t reason; // 0: establish, 1:delete
	uint32_t cell_id;
	uint8_t carrier;          // carrier no, from IE 501
	uint32_t center;          // in 100kHz
	uint32_t type;            // 0 - primary, 1 - aux (rel10 TDD)
	uint8_t tdd_config;       // 0 - 6
	uint32_t system_subframe; // in 10ms, 0-4095
	uint32_t bandwidth;       // 5/10/15/20
	uint8_t sff_config;       // 0 - 8
	uint8_t cp_length;        // 0 - normal, 1 - extended
} __attribute__((__packed__)) ir_ie_1502_t;

// Frequency point configuration, FDD, 31b
typedef struct ir_ie_1503_s
{
	uint16_t ie_no;
	uint16_t ie_len;
	uint8_t reason; // 0: establish, 1:delete
	uint32_t cell_id;
	uint8_t carrier;       // carrier no, from IE 501
	uint32_t dl_mid_freq;  // in 100kHz
	uint32_t ul_mid_freq;  // in 100kHz
	uint32_t type;         // 0 - primary, 1 - aux (rel10 TDD)
	uint32_t dl_bandwidth; // 5/10/15/20
	uint32_t ul_bandwidth;
	uint8_t cp_length; // 0 - normal, 1 - extended
} __attribute__((__packed__)) ir_ie_1503_t;

#endif /* FAPI_B4860_IR_H */
