#ifndef AVB_BOOT_H
#define AVB_BOOT_H

/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


#define ETH_FRAME_HEADER_SIZE                (14)
#define ETH_FRAME_HEADER_SIZE_MAX            (28)

#define AVDECC_PROTOCOL_VERSION              (0x0001)
#define AVDECC_AVTP_ETHERTYPE                (0x22f0)
#define AVDECC_AVTP_OUI                      (0x91e0f0)

#define AVDECC_CD (1)

/* discovery */
#define AVDECC_SUBTYPE_ADP (0x7a)

/* enumeration and control */
#define AVDECC_SUBTYPE_AECP (0x7b)

/* connection management */
#define AVDECC_SUBTYPE_ACMP (0x7c)


/* used in discovery */

/* global, protocol constants */
#define ADP_DISCOVERY_CD                     AVDECC_CD
#define ADP_DISCOVERY_SUBTYPE                AVDECC_SUBTYPE_ADP
#define ADP_DISCOVERY_SV                     (0)
#define ADP_DISCOVERY_VERSION                (0x0)
#define ADP_DISCOVERY_CONTROL_DATA_LENGTH    (56)
#define ADP_CONTROL_DATA_ENTITY_AVAILABLE    (0x0)
#define ADP_CONTROL_DATA_ENTITY_DEPARTING    (0x1)
#define ADP_CONTROL_DATA_ENTITY_DISCOVER     (0x2)

#define ADP_ENT_CAP_DFU_MODE                 (0x00000001)
#define ADP_ENT_CAP_ADDRESS_ACCESS_SUPPORTED (0x00000002)
#define ADP_ENT_CAP_GATEWAY_ENTITY           (0x00000004)
#define ADP_ENT_CAP_AEM_SUPPORTED            (0x00000008)
#define ADP_ENT_CAP_LEGACY_AVC               (0x00000010)
#define ADP_ENT_CAP_ASSOCIATION_ID_SUPPORTED (0x00000020)
#define ADP_ENT_CAP_ASSOCIATION_ID_VALID     (0x00000040)
#define ADP_ENT_CAP_VENDOR_UNIQUE_SUPPORTED  (0x00000080)

#define ADP_LIST_CAP_IMPLEMENTED             (0x0001)
#define ADP_LIST_CAP_AUDIO_SINK              (0x4000)

#define ADP_ENT_TYPE_LOUDSPEAKER             (0x00000004)

#define ADP_ETHERNET_SIZE (ADP_DISCOVERY_CONTROL_DATA_LENGTH +8 \
			   + ETH_FRAME_HEADER_SIZE)
  

/* device-specific constants (update to match the product) */
#define CAL_ADP_ENTITY_VALID_TIME            (15)
#define CAL_ADP_ENTITY_VENDOR_ID             (0x001cab00)
#define CAL_ADP_ENTITY_MODEL_ID_DEFAULT      (0x00000001)
#define CAL_ADP_ENTITY_CAP                   (ADP_ENT_CAP_ASSOCIATION_ID_SUPPORTED|ADP_ENT_CAP_ASSOCIATION_ID_VALID)
#define CAL_ADP_TALKER_STREAM_SOURCES        (0x0000)
#define CAL_ADP_TALKER_CAP                   (0x0000)
#define CAL_ADP_LISTENER_STREAM_SINKS        (0x0000)
#define CAL_ADP_LISTENER_CAP                 (0x0000)
#define CAL_ADP_CONTROLLER_CAP               (0x00000000)
#define CAL_ADP_AVAIL_INDEX                  (0x00000000)
#define CAL_ADP_AS_GRANDMASTER_ID            (0x0000000000000000ULL)
#define CAL_ADP_ENT_TYPE                     (0x00000000)

/* used in reboot command */

/* global, protocol constants */
#define AEM_COMMAND_CD                       AVDECC_CD
#define AEM_COMMAND_SUBTYPE                  AVDECC_SUBTYPE_AECP
#define AEM_DISCOVERY_SV                     (0)
#define AEM_COMMAND_CONTROL_DATA_LENGTH      (16)
#define AEM_COMMAND                          (0x0)
#define AEM_COMMAND_TYPE_REBOOT              (0x002a)
#define AEM_DESCRIPTOR_TYPE_MEMORY_OBJECT    (0x000b)

/* device-specific constants */
#define AEM_REBOOT_MEMORY_ID_MAINTENANCE     (0)
#define AEM_REBOOT_MEMORY_ID_REGULAR         (1)

int send_adp_frame(int eth_port,unsigned char *primary_mac,unsigned char *srcmac,
		   u32 serialnum,u32 entity_model_id);
int receive_aem_frame(int eth_port,unsigned char *primary_mac,unsigned char *srcmac,u32 serialnum);

#endif
