/* packet-gsm_cell_broadcast.c
 * Routines for GSM Cell Broadcast Service dissection - A.K.A. 3GPP 23.041 (GSM 03.41) section 9.4
 *
 * Copyright 2011, Mike Morrin <mike.morrin [AT] ipaccess.com>
 *
 * $Id: packet-cell_broadcast.c 43258 2012-06-14 13:43:34Z wmeier $
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>

#include <epan/packet.h>

#include <epan/strutil.h>
#include <epan/asn1.h>

#include <epan/packet.h>
#include <epan/reassemble.h>

#include "packet-gsm_map.h"
#include "packet-gsm_sms.h"

#define GSM_CBS_PAGE_SIZE 88

const value_string message_id_values[] = {
    { 0, "Index"},
    { 10, "Flashes"},
    { 20, "Hospitals"},
    { 22, "Doctors"},
    { 24, "Pharmacy"},
    { 30, "Long Distance Road Reports"},
    { 32, "Local Road Reports"},
    { 34, "Taxis"},
    { 40, "Weather"},
    { 50, "Regional Services (local phone prefixes) / District (Base Station Identity)"},
    { 52, "Network Information"},
    { 54, "Operator Service"},
    { 56, "Directory Enquiries (national)"},
    { 57, "Directory Enquiries (international)"},
    { 58, "Customer Care (national)"},
    { 59, "Customer Care (international)"},
    { 60, "Local Date/Time Group incl. Time Zone"},
    { 100, "Regional Services (local phone prefixes)"},
    { 101, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 102, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 103, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 104, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 105, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 106, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 107, "planned for news, events, whether, traffic, TV station and News of the day"},
    { 123, "Mobile Auction"},
    { 140, "Handelsblatt (latest info from stock exchange)"},
    { 666, "PASSO Traffic Alert (brief teaser information for IVR)"},
    { 1000, "LCS CBS Message Identifier for E-OTD Assistance Data message"},
    { 1001, "LCS CBS Message Identifier for DGPS Correction Data message"},
    { 1002, "LCS CBS Message Identifier for GPS Ephemeris and Clock Correction Data message"},
    { 1003, "LCS CBS Message Identifier for GPS Almanac and Other Data message"},
    { 4352, "ETWS CBS Message Identifier for earthquake warning message"},
    { 4353, "ETWS CBS Message Identifier for tsunami warning message"},
    { 4354, "ETWS CBS Message Identifier for earthquake and tsunami combined warning message"},
    { 4355, "ETWS CBS Message Identifier for test message"},
    { 4356, "ETWS CBS Message Identifier for messages related to other emergency types"},
    { 4370, "CMAS CBS Message Identifier for CMAS Presidential Level Alerts"},
    { 4371, "CMAS CBS Message Identifier for CMAS Extreme Alerts with Severity of Extreme, Urgency of Immediate, and Certainty of Observed"},
    { 4372, "CMAS CBS Message Identifier for CMAS Extreme Alerts with Severity of Extreme, Urgency of Immediate, and Certainty of Likely"},
    { 4373, "CMAS CBS Message Identifier for CMAS Severe Alerts with Severity of Extreme, Urgency of Expected, and Certainty of Observed"},
    { 4374, "CMAS CBS Message Identifier for CMAS Severe Alerts with Severity of Extreme, Urgency of Expected, and Certainty of Likely"},
    { 4375, "CMAS CBS Message Identifier for CMAS Severe Alerts with Severity of Severe, Urgency of Immediate, and Certainty of Observed"},
    { 4376, "CMAS CBS Message Identifier for CMAS Severe Alerts with Severity of Severe, Urgency of Immediate, and Certainty of Likely"},
    { 4377, "CMAS CBS Message Identifier for CMAS Severe Alerts with Severity of Severe, Urgency of Expected, and Certainty of Observed"},
    { 4378, "CMAS CBS Message Identifier for CMAS Severe Alerts with Severity of Severe, Urgency of Expected, and Certainty of Likely"},
    { 4379, "CMAS CBS Message Identifier for Child Abduction Emergency (or Amber Alert)"},
    { 4380, "CMAS CBS Message Identifier for the Required Monthly Test"},
    { 4381, "CMAS CBS Message Identifier for CMAS Exercise"},
    { 4382, "CMAS CBS Message Identifier for operator defined use"},
    { 65535, "Message Identifier Reserved, and should not be used for new services"},
    {    0, NULL }
};

const value_string geographic_scope_values[] = {
    { 0x00, "Cell-wide (immediate display)" },
    { 0x01, "PLMN-wide" },
    { 0x02, "Location Area-wide (GSM) or Service Area-wide (UMTS)" },
    { 0x03, "Cell-wide (normal display)" },
    {    0, NULL }
};

/* Initialize the protocol and registered fields */
static int proto_cell_broadcast = -1;

static int hf_gsm_cbs_serial_number	= -1;
static int hf_gsm_cbs_geographic_scope	= -1;
static int hf_gsm_cbs_message_code	= -1;
static int hf_gsm_cbs_update_number	= -1;
static int hf_gsm_cbs_message_identifier	= -1;
static int hf_gsm_cbs_total_pages	= -1;
static int hf_gsm_cbs_current_page	= -1;

/* These fields are used when reassembling multi-page gsm cbs messages */
static int hf_gsm_cbs_pages			= -1;
static int hf_gsm_cbs_page_num			= -1;
static int hf_gsm_cbs_page_overlap		= -1;
static int hf_gsm_cbs_page_overlap_conflict	= -1;
static int hf_gsm_cbs_page_multiple_tails	= -1;
static int hf_gsm_cbs_page_too_long_fragment	= -1;
static int hf_gsm_cbs_page_error		= -1;
static int hf_gsm_cbs_page_count		= -1;
static int hf_gsm_cbs_message_reassembled_in		= -1;
static int hf_gsm_cbs_message_reassembled_length		= -1;
static int hf_gsm_cbs_page_content		= -1;
static int hf_gsm_cbs_message_content		= -1;

/* Initialize the subtree pointers */
static gint ett_cbs_msg = -1;
static gint ett_cbs_serial_no = -1;
static gint ett_cbs_coding = -1;
static gint ett_gsm_cbs_page           = -1;
static gint ett_gsm_cbs_page_content         = -1;
static gint ett_gsm_cbs_pages          = -1;

/* reassembly of GSM multi-page messages */
static GHashTable	*gsm_cbs_page_table = NULL;
static GHashTable	*gsm_cbs_message_table = NULL;

/* Structure needed for the fragmentation routines in reassemble.c */
static const fragment_items gsm_page_items = {
    &ett_gsm_cbs_page,
    &ett_gsm_cbs_pages,
    &hf_gsm_cbs_pages,
    &hf_gsm_cbs_page_num,
    &hf_gsm_cbs_page_overlap,
    &hf_gsm_cbs_page_overlap_conflict,
    &hf_gsm_cbs_page_multiple_tails,
    &hf_gsm_cbs_page_too_long_fragment,
    &hf_gsm_cbs_page_error,
    &hf_gsm_cbs_page_count,
    &hf_gsm_cbs_message_reassembled_in,
    &hf_gsm_cbs_message_reassembled_length,
    "pages"
};


static void gsm_cbs_message_reassembly_init(void)
{
  fragment_table_init(&gsm_cbs_page_table);
  reassembled_table_init(&gsm_cbs_message_table);
}

guint16 dissect_cbs_serial_number(tvbuff_t *tvb, proto_tree *tree, guint16 offset)
{
   guint16 serial_number = tvb_get_ntohs(tvb, offset) ;
   proto_item *item;
   proto_tree *subtree;

   item = proto_tree_add_item(tree, hf_gsm_cbs_serial_number, tvb, offset, 2, ENC_BIG_ENDIAN);
   proto_item_append_text(item, ", Message Code: %d, Update Number: %d", (serial_number & 0x3FF) >> 4, serial_number & 0x0F);
   subtree = proto_item_add_subtree(item, ett_cbs_serial_no);
   proto_tree_add_item(subtree, hf_gsm_cbs_geographic_scope, tvb, offset, 2, ENC_BIG_ENDIAN);
   proto_tree_add_item(subtree, hf_gsm_cbs_message_code, tvb, offset, 2, ENC_BIG_ENDIAN);
   proto_tree_add_item(subtree, hf_gsm_cbs_update_number, tvb, offset, 2, ENC_BIG_ENDIAN);
   offset +=2;
   return offset;
}

guint16 dissect_cbs_message_identifier(tvbuff_t *tvb, proto_tree *tree, guint16 offset)
{
   guint16 msg_id;
   const char *msg_id_string = NULL;

   msg_id = tvb_get_ntohs(tvb, offset);
   msg_id_string = match_strval(msg_id, message_id_values);
   if (msg_id_string == NULL)
   {
      if (msg_id < 1000)
      {
         msg_id_string = "Message ID to be allocated by GSMA";
      }
      else if (msg_id < 4096)
      {
         msg_id_string = "Message ID intended for standardization in future versions of 3GPP TS 23.041";
      }
      else if (msg_id < 4224)
      {
         msg_id_string = "Message ID reserved for Cell Broadcast Data Download (unsecured) to the SIM ";
      }
      else if (msg_id < 4352)
      {
         msg_id_string = "Message ID reserved for Cell Broadcast Data Download (secured) to the SIM ";
      }
      else if (msg_id < 4360)
      {
         msg_id_string = "ETWS CBS Message Identifier for future extension";
      }
      else if (msg_id < 4400)
      {
         msg_id_string = "CMAS CBS Message Identifier for future extension";
      }
      else if (msg_id < 6400)
      {
         msg_id_string = "CBS Message Identifier for future PWS use";
      }
      else if (msg_id < 40960)
      {
         msg_id_string = "Intended for standardization in future versions of 3GPP TS 23.041";
      }
      else if (msg_id < 43500)
      {
         msg_id_string = "Message ID in PLMN operator specific range";
      }
      else if (msg_id < 43530)
      {
         msg_id_string = "Traffic Information Traffic Master UK";
      }
      else if (msg_id < 43585)
      {
         msg_id_string = "Traffic information Mannesmann Telecommerce";
      }
      else if (msg_id < 45056)
      {
         msg_id_string = "Message ID in PLMN operator specific range";
      }
      else
      {
         msg_id_string = "Message ID intended as PLMN operator specific range in future versions of 3GPP TS 23.041";
      }
   }
   proto_tree_add_uint_format_value(tree, hf_gsm_cbs_message_identifier, tvb, offset, 2, msg_id, "%s (%d)", msg_id_string, msg_id);
   return 2;
}

static tvbuff_t * dissect_cbs_data(guint8 sms_encoding, tvbuff_t *tvb, proto_tree *tree, packet_info *pinfo, guint16 offset )
{
   tvbuff_t * tvb_out = NULL;
   guint8		out_len;
   int			length = tvb_length(tvb) - offset;
   gchar *utf8_text = NULL;
   static unsigned char msgbuf[1024];
   guint8 * input_string = tvb_get_ephemeral_string(tvb, offset, length);
   GIConv cd;
   GError *l_conv_error = NULL;

   switch(sms_encoding){
     case SMS_ENCODING_7BIT:
     case SMS_ENCODING_7BIT_LANG:
     out_len = gsm_sms_char_7bit_unpack(0, length, sizeof(msgbuf),
                                        input_string,
                                        msgbuf);
     msgbuf[out_len] = '\0';
     utf8_text = gsm_sms_chars_to_utf8(msgbuf, out_len);
     tvb_out = tvb_new_child_real_data(tvb, utf8_text, out_len, out_len);
     add_new_data_source(pinfo, tvb_out, "unpacked 7 bit data");
     break;

     case SMS_ENCODING_8BIT:
     tvb_out = tvb_new_subset(tvb, offset, length, length);
     break;

     case SMS_ENCODING_UCS2:
     case SMS_ENCODING_UCS2_LANG:
     if ((cd = g_iconv_open("UTF-8","UCS-2BE")) != (GIConv) -1)
     {
         utf8_text = g_convert_with_iconv(input_string, length, cd, NULL, NULL, &l_conv_error);
         if(!l_conv_error)
         {
            tvb_out = tvb_new_subset(tvb, offset, length, length);
         }
         else
         proto_tree_add_text(tree, tvb, offset, length, "CBS String: g_convert_with_iconv FAILED");

         g_free(utf8_text);
         g_iconv_close(cd);
     }
     else
     {
            proto_tree_add_text(tree, tvb, offset, length, "CBS String: g_iconv_open FAILED contact wireshark");
     }
     break;

      default:
         proto_tree_add_text(tree, tvb, offset, length, "Unhandled encoding %d of CBS String", sms_encoding);
     break;
   }
   return tvb_out;
}

static void
dissect_gsm_cell_broadcast(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
   guint8 sms_encoding, total_pages, current_page;
   guint32       offset = 0;
   guint32       len, msg_key;
   proto_item     *cbs_page_item = NULL;
   proto_tree    *cbs_page_tree = NULL;
   guint16 serial_number, message_id;
   tvbuff_t *cbs_page_tvb = NULL;
   tvbuff_t *cbs_msg_tvb = NULL;
   fragment_data * frag_data = NULL;

   len = tvb_length(tvb);

   col_append_str(pinfo->cinfo, COL_PROTOCOL, " Cell Broadcast");
   col_append_str(pinfo->cinfo, COL_INFO, " (CBS Page)");

   cbs_page_item = proto_tree_add_protocol_format(proto_tree_get_root(tree), proto_cell_broadcast, tvb, 0, len, "GSM Cell Broadcast");
   cbs_page_tree = proto_item_add_subtree(cbs_page_item, ett_gsm_cbs_page);

   serial_number = tvb_get_ntohs(tvb, offset);
   offset += dissect_cbs_serial_number(tvb, cbs_page_tree, offset);
   message_id = tvb_get_ntohs(tvb, offset);
   offset += dissect_cbs_message_identifier(tvb, cbs_page_tree, offset);
   sms_encoding = dissect_cbs_data_coding_scheme(tvb, pinfo, cbs_page_tree, offset++);
   total_pages = tvb_get_guint8(tvb, offset);
   current_page = (total_pages & 0xF0) >> 4;
   total_pages &= 0x0F;
   proto_tree_add_item(cbs_page_tree, hf_gsm_cbs_current_page, tvb, offset, 1, ENC_BIG_ENDIAN);
   proto_tree_add_item(cbs_page_tree, hf_gsm_cbs_total_pages, tvb, offset++, 1, ENC_BIG_ENDIAN);
   cbs_page_tvb = dissect_cbs_data(sms_encoding, tvb, cbs_page_tree, pinfo, offset );

   if (cbs_page_tvb != NULL)
   {
     if (tree != NULL)
     {
        proto_item *item = proto_tree_add_text(cbs_page_tree, tvb, offset, -1, "Cell Broadcast Page Contents");
        proto_tree *cbs_page_subtree = proto_item_add_subtree(item, ett_gsm_cbs_page_content);
        gint len = tvb_length(cbs_page_tvb);
        proto_tree_add_string(cbs_page_subtree, hf_gsm_cbs_page_content, cbs_page_tvb, 0, len, tvb_get_ephemeral_string(cbs_page_tvb, 0, len));
     }
     if (total_pages == 1)
     {
        /* no need for reassembly */
        cbs_msg_tvb = cbs_page_tvb;
     }
     else
     {
         /* now we have a complete page, try to concatenate the full message */
        /* we can use the serial number and message ID as keys, as they are the same for all pages of a message */
        msg_key = (serial_number << 16) + message_id;
        frag_data = fragment_add_check(cbs_page_tvb, 0, pinfo, msg_key,
                                       gsm_cbs_page_table, gsm_cbs_message_table,
                                       ((current_page -1) * GSM_CBS_PAGE_SIZE),
                                       GSM_CBS_PAGE_SIZE, (current_page!=total_pages));
        cbs_msg_tvb = process_reassembled_data(cbs_page_tvb, 0, pinfo, "Reassembled Cell Broadcast message",
                                            frag_data, &gsm_page_items, NULL, cbs_page_tree);
     }
   }
   if (cbs_msg_tvb != NULL)
   {
      guint16 len;
      proto_item     *cbs_msg_item = NULL;
      proto_tree    *cbs_msg_tree = NULL;

      len = tvb_length(cbs_msg_tvb);
      col_append_str(pinfo->cinfo, COL_INFO, " (CBS Message)");

      cbs_msg_item = proto_tree_add_protocol_format(proto_tree_get_root(tree), proto_cell_broadcast, cbs_msg_tvb, 0, len, "GSM Cell Broadcast Message");
      cbs_msg_tree = proto_item_add_subtree(cbs_msg_item, ett_cbs_msg);

      proto_tree_add_string(cbs_msg_tree, hf_gsm_cbs_message_content, cbs_msg_tvb, 0, len, tvb_get_ephemeral_string(cbs_msg_tvb, 0, len));
   }
}

void dissect_umts_cell_broadcast_message(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
   guint8 sms_encoding;
   guint32       offset = 0;
   guint32       len;
   proto_item    *cbs_item = NULL, *cbs_item2 = NULL;
   proto_tree    *cbs_tree = NULL, *cbs_subtree = NULL;
   guint16  msg_len;
   tvbuff_t * cbs_msg_tvb = NULL;

   len = tvb_length(tvb);

   col_append_str(pinfo->cinfo, COL_PROTOCOL, " Cell Broadcast");
   col_append_str(pinfo->cinfo, COL_INFO, " (CBS Message)");

   cbs_item = proto_tree_add_protocol_format(proto_tree_get_root(tree), proto_cell_broadcast, tvb, 0, len, "Cell Broadcast");
   cbs_tree = proto_item_add_subtree(cbs_item, ett_cbs_msg);

   sms_encoding = dissect_cbs_data_coding_scheme(tvb, pinfo, cbs_tree, 0);
   offset++;
   cbs_msg_tvb = dissect_cbs_data(sms_encoding, tvb, cbs_tree, pinfo, offset );

   msg_len = tvb_length(cbs_msg_tvb);
   cbs_item2 = proto_tree_add_text(cbs_tree, tvb, offset, -1, "Cell Broadcast Message Contents (length: %d)", msg_len);
   cbs_subtree = proto_item_add_subtree(cbs_item2, ett_cbs_msg);
   proto_tree_add_text(cbs_subtree, cbs_msg_tvb , 0, tvb_length(cbs_msg_tvb), "%s", tvb_get_ephemeral_string(cbs_msg_tvb, 0, msg_len));
}

/* Register the protocol with Wireshark */
void
proto_register_cbs(void)
{
    /* Setup list of header fields */
    static hf_register_info hf_cbs[] =
        {
           { &hf_gsm_cbs_serial_number,
             { "GSM CBS Serial Number",	"gsm_cbs.serial_number",
       FT_UINT16, BASE_HEX_DEC, NULL, 0x00,
       NULL, HFILL }
           },
            { &hf_gsm_cbs_message_code,
              { "GSM CBS Message Code",	"gsm_cbs.message_code",
		FT_UINT16, BASE_DEC_HEX, NULL, 0x3FF0,
		NULL, HFILL }
            },
            { &hf_gsm_cbs_geographic_scope,
              { "GSM CBS Geographic Scope",	"gsm_cbs.geographic_scope",
		FT_UINT16, BASE_DEC, VALS(geographic_scope_values), 0xC000,
		NULL, HFILL }
            },
            { &hf_gsm_cbs_update_number,
              { "GSM CBS Update Number",	"gsm_cbs.update_number",
		FT_UINT16, BASE_DEC, NULL, 0x000F,
		NULL, HFILL }
            },
            { &hf_gsm_cbs_message_identifier,
              { "GSM CBS Message Identifier",	"gsm_cbs.message-identifier",
		FT_UINT16, BASE_DEC_HEX, NULL, 0x00,
		NULL, HFILL }
            },
            { &hf_gsm_cbs_total_pages,
              { "GSM CBS Total Pages",	"gsm_cbs.total_pages",
		FT_UINT8, BASE_DEC, NULL, 0x0F,
		NULL, HFILL }
            },
            { &hf_gsm_cbs_current_page,
              { "GSM CBS Current Page",	"gsm_cbs.current_page",
		FT_UINT8, BASE_DEC, NULL, 0xF0,
		NULL, HFILL }
            },
            /* Fragment fields
             */
            { &hf_gsm_cbs_page_overlap,
              {	"page overlap",
            "gsm_cbs.page.overlap",
            FT_BOOLEAN, BASE_NONE, NULL, 0x0,
            "GSM CBS page overlaps with other fragments", HFILL
              }
            },
            { &hf_gsm_cbs_page_overlap_conflict,
              {	"Conflicting data in page overlap",
            "gsm_cbs.page.overlap.conflict",
            FT_BOOLEAN, BASE_NONE, NULL, 0x0,
            "Overlapping pages contained conflicting data", HFILL
              }
            },
            { &hf_gsm_cbs_page_multiple_tails,
              {	"Multiple final pages found",
            "gsm_cbs.page.multipletails",
            FT_BOOLEAN, BASE_NONE, NULL, 0x0,
            "Several copies of the final page were found when reassembling the message", HFILL
              }
            },
            { &hf_gsm_cbs_page_too_long_fragment,
              {	"Page too long",
            "gsm_cbs.page.toolongfragment",
            FT_BOOLEAN, BASE_NONE, NULL, 0x0,
            "Page contained data past end of packet", HFILL
              }
            },
            { &hf_gsm_cbs_page_error,
              {	"Reassembly error",
            "gsm_cbs.fragment.error",
            FT_FRAMENUM, BASE_NONE, NULL, 0x0,
            "Reassembly error due to illegal fragments", HFILL
              }
            },
            { &hf_gsm_cbs_page_count,
              {	"Fragment count",
            "gsm_cbs.fragment.count",
            FT_UINT32, BASE_DEC, NULL, 0x0,
            "Count of Page Fragment", HFILL
              }
            },
            { &hf_gsm_cbs_message_reassembled_in,
              {	"Reassembled in",
            "gsm_cbs.reassembled.in",
            FT_FRAMENUM, BASE_NONE, NULL, 0x0,
            "CBS pages are reassembled in the given packet", HFILL
              }
            },
            { &hf_gsm_cbs_message_reassembled_length,
              {	"Reassembled message length",
            "gsm_cbs.reassembled.length",
            FT_UINT32, BASE_DEC, NULL, 0x0,
            "The total length of the reassembled message", HFILL
              }
            },
            { &hf_gsm_cbs_page_num,
              {	"CBS Page Number",
            "gsm_cbs.page_number",
            FT_FRAMENUM, BASE_NONE, NULL, 0x0,
            NULL, HFILL
              }
            },
            { &hf_gsm_cbs_pages,
              {	"CBS Pages",
            "gsm_cbs.pages",
            FT_NONE, BASE_NONE, NULL, 0x0,
            NULL, HFILL
              }
            },
            { &hf_gsm_cbs_page_content,
              {	"CBS Page Content",
            "gsm_cbs.page_content",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL
              }
            },
            { &hf_gsm_cbs_message_content,
              {	"CBS Message Content",
            "gsm_cbs.message_content",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL
              }
            }
            };

    /* Setup protocol subtree array */
    static gint *ett[] = {
       &ett_cbs_msg,
       &ett_cbs_serial_no,
       &ett_cbs_coding,
       &ett_gsm_cbs_page,
       &ett_gsm_cbs_page_content,
       &ett_gsm_cbs_pages,
    };

    /* Register the protocol name and description */
    proto_cell_broadcast = proto_register_protocol("GSM Cell Broadcast Service", "GSM Cell Broadcast Service", "gsm_cell_broadcast");

    proto_register_field_array(proto_cell_broadcast, hf_cbs, array_length(hf_cbs));
    register_init_routine(gsm_cbs_message_reassembly_init);

    /* subdissector code */
    register_dissector("gsm_cell_broadcast", dissect_gsm_cell_broadcast, proto_cell_broadcast);
    register_dissector("umts_cell_broadcast", dissect_umts_cell_broadcast_message, proto_cell_broadcast);

    /* subtree array */
    proto_register_subtree_array(ett, array_length(ett));
}
