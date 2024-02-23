/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2024 root <<user@hostname.org>>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-h264encrypt
 *
 * FIXME:Describe h264encrypt here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! h264encrypt ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/base/base.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/controller/controller.h>
#include <gst/gst.h>

#include "ciphers/aes.h"
#include "gsth264encrypt.h"
#include "gsth264encryptionmode.h"

GST_DEBUG_CATEGORY_STATIC(gst_h264_encrypt_debug);
#define GST_CAT_DEFAULT gst_h264_encrypt_debug
#define DEFAULT_ENCRYPTION_MODE GST_H264_ENCRYPTION_MODE_AES_CTR

/* h264encrypt signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  PROP_0,
  PROP_SILENT,
  PROP_ENCRYPTION_MODE,
};

static uint8_t key[16] = {10, 10, 10, 20, 20, 20, 30, 30,
                          30, 04, 04, 04, 04, 05, 05, 05};

/* the capabilities of the inputs and outputs.
 *
 * FIXME:describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264,alignment=au,stream-format=byte-stream"));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS("video/x-h264,alignment=au,stream-format=byte-stream"));

#define gst_h264_encrypt_parent_class parent_class
G_DEFINE_TYPE(GstH264Encrypt, gst_h264_encrypt, GST_TYPE_BASE_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE(h264encrypt, "h264encrypt", GST_RANK_NONE,
                            GST_TYPE_H264_ENCRYPT);

static void gst_h264_encrypt_set_property(GObject *object, guint prop_id,
                                          const GValue *value,
                                          GParamSpec *pspec);
static void gst_h264_encrypt_get_property(GObject *object, guint prop_id,
                                          GValue *value, GParamSpec *pspec);
static void gst_h264_encrypt_dispose(GObject *object);
static void gst_h264_encrypt_finalize(GObject *object);

static GstFlowReturn gst_h264_encrypt_transform_ip(GstBaseTransform *base,
                                                   GstBuffer *outbuf);

/* GObject vmethod implementations */

/* initialize the h264encrypt's class */
static void gst_h264_encrypt_class_init(GstH264EncryptClass *klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *)klass;
  gstelement_class = (GstElementClass *)klass;

  gobject_class->set_property = gst_h264_encrypt_set_property;
  gobject_class->get_property = gst_h264_encrypt_get_property;
  gobject_class->dispose = gst_h264_encrypt_dispose;
  gobject_class->finalize = gst_h264_encrypt_finalize;

  g_object_class_install_property(
      gobject_class, PROP_SILENT,
      g_param_spec_boolean("silent", "Silent", "Produce verbose output ?",
                           FALSE, G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE));

  gst_element_class_set_details_simple(
      gstelement_class, "h264encrypt", "Codec/Encryptor/Video",
      "H264 Video Encryptor", "Oguzhan Oztaskin <oguzhanoztaskin@gmail.com>");
  g_object_class_install_property(
      gobject_class, PROP_ENCRYPTION_MODE,
      g_param_spec_enum("encryption-mode", "Encryption Mode",
                        "Mode of encryption to perform",
                        GST_TYPE_H264_ENCRYPTION_MODE, DEFAULT_ENCRYPTION_MODE,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
                            GST_PARAM_MUTABLE_PAUSED));

  gst_element_class_add_pad_template(
      gstelement_class, gst_static_pad_template_get(&src_template));
  gst_element_class_add_pad_template(
      gstelement_class, gst_static_pad_template_get(&sink_template));

  GST_BASE_TRANSFORM_CLASS(klass)->transform_ip =
      GST_DEBUG_FUNCPTR(gst_h264_encrypt_transform_ip);

  GST_DEBUG_CATEGORY_INIT(gst_h264_encrypt_debug, "h264encrypt", 0,
                          "h264encrypt general logs");

  gst_type_mark_as_plugin_api(GST_TYPE_H264_ENCRYPTION_MODE, 0);
}

/* initialize the new element
 * initialize instance structure
 */
static void gst_h264_encrypt_init(GstH264Encrypt *h264encrypt) {
  h264encrypt->silent = FALSE;
  h264encrypt->nalparser = gst_h264_nal_parser_new();
  h264encrypt->encryption_mode = DEFAULT_ENCRYPTION_MODE;
}

static void gst_h264_encrypt_dispose(GObject *object) {}

static void gst_h264_encrypt_finalize(GObject *object) {
  GstH264Encrypt *h264encrypt = GST_H264_ENCRYPT(object);
  gst_h264_nal_parser_free(h264encrypt->nalparser);
  h264encrypt->nalparser = NULL;
}

static void gst_h264_encrypt_set_property(GObject *object, guint prop_id,
                                          const GValue *value,
                                          GParamSpec *pspec) {
  GstH264Encrypt *h264encrypt = GST_H264_ENCRYPT(object);

  switch (prop_id) {
    case PROP_SILENT:
      h264encrypt->silent = g_value_get_boolean(value);
      break;
    case PROP_ENCRYPTION_MODE:
      h264encrypt->encryption_mode = g_value_get_enum(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void gst_h264_encrypt_get_property(GObject *object, guint prop_id,
                                          GValue *value, GParamSpec *pspec) {
  GstH264Encrypt *h264encrypt = GST_H264_ENCRYPT(object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean(value, h264encrypt->silent);
      break;
    case PROP_ENCRYPTION_MODE:
      g_value_set_enum(value, h264encrypt->encryption_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

/* GstBaseTransform vmethod implementations */

/* this function does the actual processing
 */
static GstFlowReturn gst_h264_encrypt_transform_ip(GstBaseTransform *base,
                                                   GstBuffer *outbuf) {
  GstH264Encrypt *h264encrypt = GST_H264_ENCRYPT(base);

  if (GST_CLOCK_TIME_IS_VALID(GST_BUFFER_TIMESTAMP(outbuf)))
    gst_object_sync_values(GST_OBJECT(h264encrypt),
                           GST_BUFFER_TIMESTAMP(outbuf));

  if (h264encrypt->silent == FALSE) g_print("I'm plugged, therefore I'm in.\n");

  /* FIXME: do something interesting here.  This simply copies the source
   * to the destination. */

  GstMapInfo map_info;
  if (G_UNLIKELY(!gst_buffer_map(outbuf, &map_info, GST_MAP_READWRITE))) {
    GST_ERROR_OBJECT(base, "Unable to map buffer for rw!");
    return GST_FLOW_ERROR;
  }
  GstH264NalUnit nalu;
  GstH264ParserResult result = gst_h264_parser_identify_nalu(
      h264encrypt->nalparser, map_info.data, 0, map_info.size, &nalu);
  struct AES_ctx ctx, ctx2;
  AES_init_ctx_iv(&ctx, key, key);
  AES_init_ctx_iv(&ctx2, key, key);
  while (result == GST_H264_PARSER_OK || result == GST_H264_PARSER_NO_NAL_END) {
    // Process the data
    if (nalu.type != 1 && nalu.type != 9) g_print("I found: %d\n\n", nalu.type);
    /**
     * TODO: Encrypt the following NALU types:
     *
     * GST_H264_NAL_SLICE        = 1,
      GST_H264_NAL_SLICE_DPA    = 2,
      GST_H264_NAL_SLICE_DPB    = 3,
      GST_H264_NAL_SLICE_DPC    = 4,
      GST_H264_NAL_SLICE_IDR    = 5,
    */
    if (nalu.type >= GST_H264_NAL_SLICE &&
        nalu.type <= GST_H264_NAL_SLICE_IDR) {
      guint payload_offset = nalu.offset + nalu.header_bytes;
      AES_CTR_xcrypt_buffer(&ctx, &nalu.data[payload_offset],
                            nalu.size - nalu.header_bytes);
      // AES_CTR_xcrypt_buffer(&ctx2, &nalu.data[payload_offset],
      //                       nalu.size - nalu.header_bytes);
    }
    result =
        gst_h264_parser_identify_nalu(h264encrypt->nalparser, map_info.data,
                                      nalu.offset + nalu.size,  //
                                      map_info.size, &nalu);
  }
  gst_buffer_unmap(outbuf, &map_info);
  return GST_FLOW_OK;
}
