/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


#include <string.h>

#include "labx_ethernet.h"
#include "mbbl-console.h"
#include "mbbl-timer.h"
#include "mbbl-platform.h"

#include "avb-boot.h"

static int tx_adp_frame_length=0;
static unsigned char tx_adp_frame[ADP_ETHERNET_SIZE],
  rx_frame[ETHER_MTU],broadcastmac[6]={0xff,0xff,0xff,0xff,0xff,0xff},
  adp_multicastmac[6]={0x91,0xe0,0xf0,0x01,0x00,0x00},
  zeroes[8]={0,0,0,0,0,0,0,0};

/* IP header checksum verification */
int check_checksum(unsigned char *header,unsigned int header_len,
		   unsigned int add_value)
{
  unsigned int i,sum=add_value;
  for(i=0;i<header_len;i+=2)
    {
      if(i+1<header_len)
	sum+=(((unsigned int)header[i])<<8)|((unsigned int)header[i+1]);
      else
	sum+=(((unsigned int)header[i])<<8);
    }
  return (((sum>>16)&0xffff)+(sum&0xffff))!=0xffff;
}

/* create ADP ENTITY_AVAILABLE frame */
static int create_adp_frame(unsigned char *primary_mac,unsigned char *dst,unsigned char *srcmac,
			    u32 serialnum,u32 entity_model_id)
{
  unsigned char *curr;

  /* frame header */

  /* MAC addresses */
  curr=dst;
  memcpy(curr,adp_multicastmac,6);
  curr+=6;
  memcpy(curr,srcmac,6);
  curr+=6;

  /* ethertype */
  *curr++=(AVDECC_AVTP_ETHERTYPE>>8)&0xff;
  *curr++=AVDECC_AVTP_ETHERTYPE&0xff;

  /* ADP packet */

  /* CD, subtype */
  *curr++=(ADP_DISCOVERY_CD<<7)|ADP_DISCOVERY_SUBTYPE;

  /* SV, version, message type */
  *curr++=(ADP_DISCOVERY_SV<<7)|(ADP_DISCOVERY_VERSION<<4)
    |ADP_CONTROL_DATA_ENTITY_AVAILABLE;

  /* valid time, length */
  *curr++=(CAL_ADP_ENTITY_VALID_TIME<<3)
    |(ADP_DISCOVERY_CONTROL_DATA_LENGTH>>8);
  *curr++=ADP_DISCOVERY_CONTROL_DATA_LENGTH&0xff;

  /* entity GUID is from primary_mac (assuming 24-bit OUI) */
  memcpy(curr,primary_mac,3);
  curr+=3;
  *curr++=0xff;
  *curr++=0xfe;
  memcpy(curr,primary_mac+3,3);
  curr+=3;
  
  /* vendor ID (Meyer Sound) */
  *curr++=(CAL_ADP_ENTITY_VENDOR_ID>>24)&0xff;
  *curr++=(CAL_ADP_ENTITY_VENDOR_ID>>16)&0xff;
  *curr++=(CAL_ADP_ENTITY_VENDOR_ID>>8)&0xff;
  *curr++=CAL_ADP_ENTITY_VENDOR_ID&0xff;
  
  /* entity model ID */
  *curr++=(entity_model_id>>24)&0xff;
  *curr++=(entity_model_id>>16)&0xff;
  *curr++=(entity_model_id>>8)&0xff;
  *curr++=entity_model_id&0xff;

  /* entity capabilities */
  *curr++=(CAL_ADP_ENTITY_CAP>>24)&0xff;
  *curr++=(CAL_ADP_ENTITY_CAP>>16)&0xff;
  *curr++=(CAL_ADP_ENTITY_CAP>>8)&0xff;
  *curr++=CAL_ADP_ENTITY_CAP&0xff;
  
  /* talker stream sources */
  *curr++=(CAL_ADP_TALKER_STREAM_SOURCES>>8)&0xff;
  *curr++=CAL_ADP_TALKER_STREAM_SOURCES&0xff;

  /* talker capabilities */
  *curr++=(CAL_ADP_TALKER_CAP>>8)&0xff;
  *curr++=CAL_ADP_TALKER_CAP&0xff;

  /* listener stream sources */
  *curr++=(CAL_ADP_LISTENER_STREAM_SINKS>>8)&0xff;
  *curr++=CAL_ADP_LISTENER_STREAM_SINKS&0xff;

  /* listener capabilities */
  *curr++=(CAL_ADP_LISTENER_CAP>>8)&0xff;
  *curr++=CAL_ADP_LISTENER_CAP&0xff;

  /* controller capabilities */
  *curr++=(CAL_ADP_CONTROLLER_CAP>>24)&0xff;
  *curr++=(CAL_ADP_CONTROLLER_CAP>>16)&0xff;
  *curr++=(CAL_ADP_CONTROLLER_CAP>>8)&0xff;
  *curr++=CAL_ADP_CONTROLLER_CAP&0xff;

  /* available index */
  *curr++=(CAL_ADP_AVAIL_INDEX>>24)&0xff;
  *curr++=(CAL_ADP_AVAIL_INDEX>>16)&0xff;
  *curr++=(CAL_ADP_AVAIL_INDEX>>8)&0xff;
  *curr++=CAL_ADP_AVAIL_INDEX&0xff;

  /* AS domain grandmaster ID */
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>56)&0xff;
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>48)&0xff;
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>40)&0xff;
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>32)&0xff;
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>24)&0xff;
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>16)&0xff;
  *curr++=(CAL_ADP_AS_GRANDMASTER_ID>>8)&0xff;
  *curr++=CAL_ADP_AS_GRANDMASTER_ID&0xff;

  /* two reserved fields */
  memset(curr,0,8);
  curr+=8;

  /* association ID */
  *curr++=primary_mac[0];
  *curr++=primary_mac[1];
  *curr++=primary_mac[2];
  *curr++=0x00;
  *curr++=0x01;
  *curr++=primary_mac[3];
  *curr++=primary_mac[4];
  *curr++=primary_mac[5];

  /* available index */
  *curr++=(CAL_ADP_ENT_TYPE>>24)&0xff;
  *curr++=(CAL_ADP_ENT_TYPE>>16)&0xff;
  *curr++=(CAL_ADP_ENT_TYPE>>8)&0xff;
  *curr++=CAL_ADP_ENT_TYPE&0xff;
 
  return curr-dst;
}

/* send ADP ENTITY_AVAILABLE frame */
int send_adp_frame(int eth_port,unsigned char *primary_mac,unsigned char *srcmac,u32 serialnum,u32 entity_model_id)
{
  if(!tx_adp_frame_length)
    tx_adp_frame_length=create_adp_frame(primary_mac,tx_adp_frame,srcmac,serialnum,
					 entity_model_id);
  memcpy(&tx_adp_frame[6],srcmac,6);
  return labx_eth_send(eth_port,tx_adp_frame,tx_adp_frame_length);
}


/* receive frame */

int receive_aem_frame(int eth_port,unsigned char *primary_mac,unsigned char *srcmac,u32 serialnum)
{
  int l,retval=0;
  unsigned int i,frame_ethertype,payload_start,header_len,total_len,
    payload_end,protocol,data_start,data_len,src_port,dst_port,
    user_data_start,user_data_len,src_ip,dst_ip,
    aem_command_type,aem_descr_type,aem_descr_id;
  l=labx_eth_recv(eth_port,rx_frame,sizeof(rx_frame));

  /* check size for validity */
  if(l<=0)
    return 0;

  if(l>sizeof(rx_frame))
    return 0;

#if 0
  print_simple("Frame received\r\n");
  for(i=0;i<sizeof(rx_frame)&&i<l;i++)
    {
      if((i&0x0f)==0)
	{
	  print_simple_hex_32(i);
	  putchar_simple(' ');
	}
      print_simple_hex_8(rx_frame[i]);
      if((i&0xf)==0xf)
	print_simple("\r\n");
      else
	putchar_simple(' ');
      
    }
  print_simple("\r\n");
#endif

  /* check frame for validity */
  if(l<ETH_FRAME_HEADER_SIZE)
    {
      print_simple("Frame is too short\r\n");
      return 0;
    }

  if(memcmp(srcmac,rx_frame,6)
     &&memcmp(adp_multicastmac,rx_frame,6)
     &&memcmp(broadcastmac,rx_frame,6))
    {
#if 0
      print_simple("Frame destination does not match\r\n");
#endif
      return 0;
    }
  
  /* ethertype */
  frame_ethertype=(((unsigned int)rx_frame[12])<<8)
    |((unsigned int)rx_frame[13]);

  /* determine the start position of payload */
  payload_start=14;

  /* skip VLAN tag, if any */
  if(frame_ethertype==0x8100)
    {
      payload_start+=4;
      if(l<payload_start)
	{
#if 0
	  print_simple("Frame is too short\r\n");
#endif
	  return 0;
	}
      frame_ethertype=(((unsigned int)rx_frame[16])<<8)
	|((unsigned int)rx_frame[17]);
    }

  /* process received frame by ethertype */
  switch(frame_ethertype)
    {
    case 0x0800:
      /* IPv4 */
      if(l<(payload_start+20))
	{
#if 0
	  print_simple("Frame is too short for IPv4\r\n");
#endif
	  return 0;
	}

      /* IPv4 version has to be 0x04 */
      if((rx_frame[payload_start]>>4)!=0x04)
	{
#if 0
	  print_simple("IPv4 packet has wrong protocol version\r\n");
#endif
	  return 0;
	}

      /* IPv4 header length */
      header_len=(rx_frame[payload_start]&0x0f)*4;

      if(header_len<20)
	{
#if 0
	  print_simple("Invalid packet header length\r\n");
#endif
	  return 0;
	}

      if((header_len+payload_start)>l)
	{
#if 0
	  print_simple("Packet header is truncated\r\n");
#endif
	  return 0;
	}

      /* IPv4 header checksum */
      if(check_checksum(rx_frame+payload_start,header_len,0))
	{
#if 0
	  print_simple("Invalid packet header checksum\r\n");
#endif
	  return 0;
	}

      /* IPv4 packet length */
      total_len=(((unsigned int)rx_frame[payload_start+2])<<8)
	|((unsigned int)rx_frame[payload_start+3]);
      payload_end=payload_start+total_len;
      if(payload_end>l)
	{
#if 0
	  print_simple("Packet is truncated\r\n");
#endif
	  return 0;
	}
      if(total_len<=header_len)
	{
#if 0
	  print_simple("IP packet contains no data\r\n");
#endif
	  return 0;
	}

      /* IPv4 addresses */
      src_ip=(((unsigned int)rx_frame[payload_start+12])<<24)
	|(((unsigned int)rx_frame[payload_start+13])<<16)
	|(((unsigned int)rx_frame[payload_start+14])<<8)
	|((unsigned int)rx_frame[payload_start+15]);

      dst_ip=(((unsigned int)rx_frame[payload_start+16])<<24)
	|(((unsigned int)rx_frame[payload_start+17])<<16)
	|(((unsigned int)rx_frame[payload_start+18])<<8)
	|((unsigned int)rx_frame[payload_start+19]);

      print_simple("IPv4 packet from ");
      print_simple_dec(rx_frame[payload_start+12]);
      putchar_simple('.');
      print_simple_dec(rx_frame[payload_start+13]);
      putchar_simple('.');
      print_simple_dec(rx_frame[payload_start+14]);
      putchar_simple('.');
      print_simple_dec(rx_frame[payload_start+15]);
      print_simple(" to ");
      print_simple_dec(rx_frame[payload_start+16]);
      putchar_simple('.');
      print_simple_dec(rx_frame[payload_start+17]);
      putchar_simple('.');
      print_simple_dec(rx_frame[payload_start+18]);
      putchar_simple('.');
      print_simple_dec(rx_frame[payload_start+19]);
      print_simple("\r\n");

      /* IPv4 protocol */
      protocol=rx_frame[payload_start+9];
      data_start=payload_start+header_len;
      switch(protocol)
	{
	case 0x01:
	  /* ICMP */
#if 0
	  print_simple("ICMP\r\n");
#endif
	  break;
	case 0x06:
	  /* TCP */
#if 0
	  print_simple("TCP\r\n");
#endif
	  break;
	case 0x11:
	  /* UDP */
	  if((payload_end-data_start)<8)
	    {
#if 0
	      print_simple("UDP header is truncated\r\n");
#endif
	      return 0;
	    }

	  /* UDP ports */
	  src_port=(((unsigned int)rx_frame[data_start])<<8)
	    |((unsigned int)rx_frame[data_start+1]);
	  dst_port=(((unsigned int)rx_frame[data_start+2])<<8)
	    |((unsigned int)rx_frame[data_start+3]);

	  /* UDP length */
	  data_len=(((unsigned int)rx_frame[data_start+4])<<8)
	    |((unsigned int)rx_frame[data_start+5]);
	  if(data_start+data_len>payload_end)
	    {
#if 0
	      print_simple("UDP datagram is truncated\r\n");
#endif
	      return 0;
	    }

	  /* UDP checksum */
	  if((rx_frame[data_start+6]!=0)
	     ||(rx_frame[data_start+7]!=0))
	    {
	      if(check_checksum(rx_frame+data_start,data_len,
				((src_ip>>16)&0xffff)+(src_ip&0xffff)
				+((dst_ip>>16)&0xffff)+(dst_ip&0xffff)
				+protocol+data_len))
		{
		  if((rx_frame[data_start+6]==0xff)
		     &&(rx_frame[data_start+7]==0xff))
		    {
		      rx_frame[data_start+6]=0;
		      rx_frame[data_start+7]=0;
		      if(check_checksum(rx_frame+data_start,data_len,
					((src_ip>>16)&0xffff)+(src_ip&0xffff)
					+((dst_ip>>16)&0xffff)+(dst_ip&0xffff)
					+protocol+data_len))
			{
#if 0
			  print_simple("Invalid UDP checksum\r\n");
#endif
			  return 0;
			}
		    }
		  else
		    {
#if 0
		      print_simple("Invalid UDP checksum\r\n");
#endif
		      return 0;
		    }
		}
	    }
#if 0
	  else
	    print_simple("No UDP checksum\r\n");
#endif

	  /* UDP data */
	  user_data_start=data_start+8;
	  user_data_len=data_len-8;
	  print_simple("UDP datagram, from port ");
	  print_simple_dec(src_port);
	  print_simple(" to port ");
	  print_simple_dec(dst_port);
	  print_simple(", ");
	  print_simple_dec(user_data_len);
	  print_simple(" bytes\r\n");
	  for(i=0;i<user_data_len;i++)
	    {
	      if((i&0x0f)==0)
		{
		  print_simple_hex_32(i);
		  putchar_simple(' ');
		}
	      print_simple_hex_8(rx_frame[i+user_data_start]);
	      if((i&0xf)==0xf)
		print_simple("\r\n");
	      else
		putchar_simple(' ');
	    }
	  print_simple("\r\n");
	  break;
	default:
#if 0
	  print_simple("Unsupported IPv4 protocol\r\n");
#endif
	  return 0;
	}
#if 0
      print_simple("IPv4 packet received\r\n");
#endif
      break;
    case 0x0806:
      /* ARP */
#if 0
      print_simple("ARP\r\n");
#endif
      break;
    case 0x86dd:
      /* IPv6 */
#if 0
      print_simple("IPv6\r\n");
#endif
      break;
    case AVDECC_AVTP_ETHERTYPE:
      /* AVTP */
      /* detect AEM command */
      if(((l-payload_start)>=(AEM_COMMAND_CONTROL_DATA_LENGTH+12))
	 &&(rx_frame[payload_start]
	  ==((AEM_COMMAND_CD<<7)|AEM_COMMAND_SUBTYPE))
	 &&((rx_frame[payload_start+1]&0x8f)
	    ==((AEM_DISCOVERY_SV<<7)|AEM_COMMAND))
	 &&(((((unsigned int)(rx_frame[payload_start+2]&0x07))<<8)
	     |((unsigned int)rx_frame[payload_start+3]))
	    >=AEM_COMMAND_CONTROL_DATA_LENGTH))
	{
	  /* our entity GUID */
	  if(!memcmp(rx_frame+payload_start+4,primary_mac,3)
	     &&(rx_frame[payload_start+7]==0xff)
	     &&(rx_frame[payload_start+8]==0xfe)
	     &&!memcmp(rx_frame+payload_start+9,primary_mac+3,3)
	     )
	    {
	      /* ignoring 16-bit sequence ID at payload_start+20 */
	      aem_command_type=(((unsigned int)
				 (rx_frame[payload_start+22]&0x7f))<<8)
		|(unsigned int)rx_frame[payload_start+23];
	      aem_descr_type=(((unsigned int)
			       rx_frame[payload_start+24])<<8)
		|(unsigned int)rx_frame[payload_start+25];
	      aem_descr_id=(((unsigned int)
			     rx_frame[payload_start+26])<<8)
		|(unsigned int)rx_frame[payload_start+27];
	      if(aem_command_type==AEM_COMMAND_TYPE_REBOOT)
		{
		  if(aem_descr_type==AEM_DESCRIPTOR_TYPE_MEMORY_OBJECT)
		    {
		      if(platform_rs_offset!=0)
			{
			  if(aem_descr_id==0)
			    {
			      reboot_request=0;
			      platform_buttons=1;

			      memcpy(rx_frame,rx_frame+6,6);
			      memcpy(rx_frame+6,srcmac,6);
				rx_frame[payload_start+1]=
				(AEM_DISCOVERY_SV<<7)|1;
			      rx_frame[payload_start+2]=(0<<3)
				|(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
			      rx_frame[payload_start+3]=
				(AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
			      labx_eth_send(eth_port,rx_frame,payload_start
					    +12
					    +AEM_COMMAND_CONTROL_DATA_LENGTH);
			      retval=1;
			    }
			  else
			    {
			      if(aem_descr_id==1)
				{
				  memcpy(rx_frame,rx_frame+6,6);
				  memcpy(rx_frame+6,srcmac,6);
				    rx_frame[payload_start+1]=
				    (AEM_DISCOVERY_SV<<7)|1;
				  rx_frame[payload_start+2]=(0<<3)
				    |(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
				  rx_frame[payload_start+3]=
				    (AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
				  labx_eth_send(eth_port,rx_frame,l);
				}
			      else
				{
				  memcpy(rx_frame,rx_frame+6,6);
				  memcpy(rx_frame+6,srcmac,6);
				    rx_frame[payload_start+1]=
				    (AEM_DISCOVERY_SV<<7)|1;
				  rx_frame[payload_start+2]=(2<<3)
				    |(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
				  rx_frame[payload_start+3]=
				    (AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
				  labx_eth_send(eth_port,rx_frame,l);
				}
			    }
			}
		      else
			{
			  if(aem_descr_id==0)
			    {
			      reboot_request=-1;
			      platform_buttons=1;

			      memcpy(rx_frame,rx_frame+6,6);
			      memcpy(rx_frame+6,srcmac,6);
				rx_frame[payload_start+1]=
				(AEM_DISCOVERY_SV<<7)|1;
			      rx_frame[payload_start+2]=(0<<3)
				|(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
			      rx_frame[payload_start+3]=
				(AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
			      labx_eth_send(eth_port,rx_frame,l);
			      retval=1;
			    }
			  else
			    {
			      if(aem_descr_id==1)
				{
				  memcpy(rx_frame,rx_frame+6,6);
				  memcpy(rx_frame+6,srcmac,6);
				    rx_frame[payload_start+1]=
				    (AEM_DISCOVERY_SV<<7)|1;
				  rx_frame[payload_start+2]=(0<<3)
				    |(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
				  rx_frame[payload_start+3]=
				    (AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
				  labx_eth_send(eth_port,rx_frame,l);
				}
			      else
				{
				  memcpy(rx_frame,rx_frame+6,6);
				  memcpy(rx_frame+6,srcmac,6);
				    rx_frame[payload_start+1]=
				    (AEM_DISCOVERY_SV<<7)|1;
				  rx_frame[payload_start+2]=(2<<3)
				    |(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
				  rx_frame[payload_start+3]=
				    (AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
				  labx_eth_send(eth_port,rx_frame,l);
				}
			    }
			}
		    }
		  else
		    {
		      memcpy(rx_frame,rx_frame+6,6);
		      memcpy(rx_frame+6,srcmac,6);
			rx_frame[payload_start+1]=
			(AEM_DISCOVERY_SV<<7)|1;
		      rx_frame[payload_start+2]=(2<<3)
			|(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
		      rx_frame[payload_start+3]=
			(AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
		      labx_eth_send(eth_port,rx_frame,l);
		    }
		}
	      else
		{
		  memcpy(rx_frame,rx_frame+6,6);
		  memcpy(rx_frame+6,srcmac,6);
		    rx_frame[payload_start+1]=
		    (AEM_DISCOVERY_SV<<7)|1;
		  rx_frame[payload_start+2]=(1<<3)
		    |(AEM_COMMAND_CONTROL_DATA_LENGTH>>8);
		  rx_frame[payload_start+3]=
		    (AEM_COMMAND_CONTROL_DATA_LENGTH&0xff);
		  labx_eth_send(eth_port,rx_frame,l);
		}
	    }
	}
      break;
    default:
#if 0
      print_simple("Unsupported ethertype\r\n");
#endif
      return 0;
    }

  return retval;
}
