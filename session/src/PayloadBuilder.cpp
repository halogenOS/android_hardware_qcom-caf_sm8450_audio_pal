/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "PAL: PayloadBuilder"
#include "ResourceManager.h"
#include "PayloadBuilder.h"
#include "SessionGsl.h"
#include "StreamSoundTrigger.h"
#include "spr_api.h"
#include <agm/agm_api.h>
#include <bt_intf.h>
#include <bt_ble.h>
#include "sp_vi.h"
#include "sp_rx.h"

#if defined(FEATURE_IPQ_OPENWRT) || defined(LINUX_ENABLED)
#define USECASE_XML_FILE "/etc/usecaseKvManager.xml"
#else
#define USECASE_XML_FILE "/vendor/etc/usecaseKvManager.xml"
#endif

#define PARAM_ID_DISPLAY_PORT_INTF_CFG   0x8001154

#define PARAM_ID_USB_AUDIO_INTF_CFG                               0x080010D6

/* ID of the Output Media Format parameters used by MODULE_ID_MFC */
#define PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT            0x08001024
#include "spf_begin_pack.h"
#include "spf_begin_pragma.h"
/* Payload of the PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT parameter in the
 Media Format Converter Module. Following this will be the variable payload for channel_map. */

struct param_id_mfc_output_media_fmt_t
{
   int32_t sampling_rate;
   /**< @h2xmle_description  {Sampling rate in samples per second\n
                              ->If the resampler type in the MFC is chosen to be IIR,
                              ONLY the following sample rates are ALLOWED:
                              PARAM_VAL_NATIVE =-1;\n
                              PARAM_VAL_UNSET = -2;\n
                              8 kHz = 8000;\n
                              16kHz = 16000;\n
                              24kHz = 24000;\n
                              32kHz = 32000;\n
                              48kHz = 48000 \n
                              -> For resampler type FIR, all values in the range
                              below are allowed}
        @h2xmle_rangeList   {"PARAM_VAL_UNSET" = -2;
                             "PARAM_VAL_NATIVE" =-1;
                             "8 kHz"=8000;
                             "11.025 kHz"=11025;
                             "12 kHz"=12000;
                             "16 kHz"=16000;
                             "22.05 kHz"=22050;
                             "24 kHz"=24000;
                             "32 kHz"=32000;
                             "44.1 kHz"=44100;
                             "48 kHz"=48000;
                             "88.2 kHz"=88200;
                             "96 kHz"=96000;
                             "176.4 kHz"=176400;
                             "192 kHz"=192000;
                             "352.8 kHz"=352800;
                             "384 kHz"=384000}
        @h2xmle_default      {-1} */

   int16_t bit_width;
   /**< @h2xmle_description  {Bit width of audio samples \n
                              ->Samples with bit width of 16 (Q15 format) are stored in 16 bit words \n
                              ->Samples with bit width 24 bits (Q27 format) or 32 bits (Q31 format) are stored in 32 bit words}
        @h2xmle_rangeList    {"PARAM_VAL_NATIVE"=-1;
                              "PARAM_VAL_UNSET"=-2;
                              "16-bit"= 16;
                              "24-bit"= 24;
                              "32-bit"=32}
        @h2xmle_default      {-1}
   */

   int16_t num_channels;
   /**< @h2xmle_description  {Number of channels. \n
                              ->Ranges from -2 to 32 channels where \n
                              -2 is PARAM_VAL_UNSET and -1 is PARAM_VAL_NATIVE}
        @h2xmle_range        {-2..32}
        @h2xmle_default      {-1}
   */

   uint16_t channel_type[0];
   /**< @h2xmle_description  {Channel mapping array. \n
                              ->Specify a channel mapping for each output channel \n
                              ->If the number of channels is not a multiple of four, zero padding must be added
                              to the channel type array to align the packet to a multiple of 32 bits. \n
                              -> If num_channels field is set to PARAM_VAL_NATIVE (-1) or PARAM_VAL_UNSET(-2)
                              this field will be ignored}
        @h2xmle_variableArraySize {num_channels}
        @h2xmle_range        {1..63}
        @h2xmle_default      {1}    */
}
#include "spf_end_pragma.h"
#include "spf_end_pack.h"
;
/* Structure type def for above payload. */
typedef struct param_id_mfc_output_media_fmt_t param_id_mfc_output_media_fmt_t;

struct param_id_usb_audio_intf_cfg_t
{
   uint32_t usb_token;
   uint32_t svc_interval;
};

std::vector<allKVs> PayloadBuilder::all_streams;
std::vector<allKVs> PayloadBuilder::all_streampps;
std::vector<allKVs> PayloadBuilder::all_devices;
std::vector<allKVs> PayloadBuilder::all_devicepps;

template <typename T>
void PayloadBuilder::populateChannelMap(T pcmChannel, uint8_t numChannel)
{
    if (numChannel == 1) {
        pcmChannel[0] = PCM_CHANNEL_C;
    } else if (numChannel == 2) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
    } else if (numChannel == 3) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
    } else if (numChannel == 4) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_LB;
        pcmChannel[3] = PCM_CHANNEL_RB;
    } else if (numChannel == 5) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LB;
        pcmChannel[4] = PCM_CHANNEL_RB;
    } else if (numChannel == 6) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LFE;
        pcmChannel[4] = PCM_CHANNEL_LB;
        pcmChannel[5] = PCM_CHANNEL_RB;
    } else if (numChannel == 7) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LS;
        pcmChannel[4] = PCM_CHANNEL_RS;
        pcmChannel[5] = PCM_CHANNEL_LB;
        pcmChannel[6] = PCM_CHANNEL_RB;
    } else if (numChannel == 8) {
        pcmChannel[0] = PCM_CHANNEL_L;
        pcmChannel[1] = PCM_CHANNEL_R;
        pcmChannel[2] = PCM_CHANNEL_C;
        pcmChannel[3] = PCM_CHANNEL_LS;
        pcmChannel[4] = PCM_CHANNEL_RS;
        pcmChannel[5] = PCM_CHANNEL_CS;
        pcmChannel[6] = PCM_CHANNEL_LB;
        pcmChannel[7] = PCM_CHANNEL_RB;
    }
}

void PayloadBuilder::payloadUsbAudioConfig(uint8_t** payload, size_t* size,
    uint32_t miid, struct usbAudioConfig *data)
{
    struct apm_module_param_data_t* header;
    struct param_id_usb_audio_intf_cfg_t *usbConfig;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0;

    payloadSize = sizeof(struct apm_module_param_data_t) +
       sizeof(struct param_id_usb_audio_intf_cfg_t);


    if (payloadSize % 8 != 0)
        payloadSize = payloadSize + (8 - payloadSize % 8);

    payloadInfo = new uint8_t[payloadSize]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo new failed %s", strerror(errno));
        return;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    usbConfig = (struct param_id_usb_audio_intf_cfg_t*)(payloadInfo + sizeof(struct apm_module_param_data_t));
    header->module_instance_id = miid;
    header->param_id = PARAM_ID_USB_AUDIO_INTF_CFG;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_ERR(LOG_TAG,"header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                     header->module_instance_id, header->param_id,
                     header->error_code, header->param_size);

    usbConfig->usb_token = data->usb_token;
    usbConfig->svc_interval = data->svc_interval;
    PAL_VERBOSE(LOG_TAG,"customPayload address %pK and size %zu", payloadInfo, payloadSize);

    *size = payloadSize;
    *payload = payloadInfo;

}

void PayloadBuilder::payloadDpAudioConfig(uint8_t** payload, size_t* size,
    uint32_t miid, struct dpAudioConfig *data)
{
    PAL_DBG(LOG_TAG, "Enter:");
    struct apm_module_param_data_t* header;
    struct dpAudioConfig *dpConfig;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0;

    payloadSize = sizeof(struct apm_module_param_data_t) +
        sizeof(struct dpAudioConfig);

    if (payloadSize % 8 != 0)
        payloadSize = payloadSize + (8 - payloadSize % 8);

    payloadInfo = new uint8_t[payloadSize]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    dpConfig = (struct dpAudioConfig*)(payloadInfo + sizeof(struct apm_module_param_data_t));
    header->module_instance_id = miid;
    header->param_id = PARAM_ID_DISPLAY_PORT_INTF_CFG;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_ERR(LOG_TAG,"header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    dpConfig->channel_allocation = data->channel_allocation;
    dpConfig->mst_idx = data->mst_idx;
    dpConfig->dptx_idx = data->dptx_idx;
    PAL_ERR(LOG_TAG,"customPayload address %pK and size %zu", payloadInfo, payloadSize);

    *size = payloadSize;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "Exit:");
}

void PayloadBuilder::payloadMFCConfig(uint8_t** payload, size_t* size,
        uint32_t miid, struct sessionToPayloadParam* data)
{
    struct apm_module_param_data_t* header = NULL;
    struct param_id_mfc_output_media_fmt_t *mfcConf;
    int numChannels;
    uint16_t* pcmChannel = NULL;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;

    if (!data) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }
    numChannels = data->numChannel;
    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct param_id_mfc_output_media_fmt_t) +
                  sizeof(uint16_t)*numChannels;
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    mfcConf = (struct param_id_mfc_output_media_fmt_t*)(payloadInfo +
               sizeof(struct apm_module_param_data_t));
    pcmChannel = (uint16_t*)(payloadInfo + sizeof(struct apm_module_param_data_t) +
                                       sizeof(struct param_id_mfc_output_media_fmt_t));

    header->module_instance_id = miid;
    header->param_id = PARAM_ID_MFC_OUTPUT_MEDIA_FORMAT;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_DBG(LOG_TAG, "header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    mfcConf->sampling_rate = data->sampleRate;
    mfcConf->bit_width = data->bitWidth;
    mfcConf->num_channels = data->numChannel;
    if (data->ch_info) {
        for (int i = 0; i < data->numChannel; ++i) {
            pcmChannel[i] = (uint16_t) data->ch_info->ch_map[i];
        }
    } else {
        populateChannelMap(pcmChannel, data->numChannel);
    }

    if ((2 == data->numChannel) && (PAL_SPEAKER_ROTATION_RL == data->rotation_type))
    {
        // Swapping the channel
        pcmChannel[0] = PCM_CHANNEL_R;
        pcmChannel[1] = PCM_CHANNEL_L;
    }

    *size = payloadSize + padBytes;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "sample_rate:%d bit_width:%d num_channels:%d Miid:%d",
                      mfcConf->sampling_rate, mfcConf->bit_width,
                      mfcConf->num_channels, header->module_instance_id);
    PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", payloadInfo,
                *size);
}

PayloadBuilder::PayloadBuilder()
{

}

PayloadBuilder::~PayloadBuilder()
{

}

uint16_t numOfBitsSet(uint32_t lines)
{
    uint16_t numBitsSet = 0;
    while (lines) {
        numBitsSet++;
        lines = lines & (lines - 1);
    }
    return numBitsSet;
}

void PayloadBuilder::resetDataBuf(struct user_xml_data *data)
{
     data->offs = 0;
     data->data_buf[data->offs] = '\0';
}

void PayloadBuilder::processGraphKVData(struct user_xml_data *data, const XML_Char **attr)
{
    struct kvPairs kv = {};
    int size = -1, selector_size = -1;
    std::string key(attr[1]);
    std::string value(attr[3]);

    if (strcmp(attr[0], "key") !=0) {
          PAL_ERR(LOG_TAG, "key not found");
          return;
     }
    kv.key = ResourceManager::convertCharToHex(key);

    if (strcmp(attr[2], "value") !=0) {
        PAL_ERR(LOG_TAG, "value not found");
        return;
    }
    kv.value = ResourceManager::convertCharToHex(value);

    if (data->is_parsing_streams) {
        size = all_streams.size() - 1;
        if (size >= 0) {
            selector_size = all_streams[size].keys_values.size() - 1;
            all_streams[size].keys_values[selector_size].kv_pairs.push_back(kv);
            PAL_DBG(LOG_TAG, "stream_graph_kv done size: %zu",
                all_streams[size].keys_values[selector_size].kv_pairs.size());
        }
        return;
    } else if (data->is_parsing_streampps) {
        size = all_streampps.size() - 1;
        if (size >= 0) {
            selector_size = all_streampps[size].keys_values.size() - 1;
            all_streampps[size].keys_values[selector_size].kv_pairs.push_back(kv);
            PAL_DBG(LOG_TAG, "streampp_graph_kv:streampp size: %zu",
                all_streampps[size].keys_values[selector_size].kv_pairs.size());
        }
        return;
    } else if (data->is_parsing_devices) {
        size = all_devices.size() - 1;
        if (size >= 0) {
            selector_size = all_devices[size].keys_values.size() - 1;
            all_devices[size].keys_values[selector_size].kv_pairs.push_back(kv);
            PAL_DBG(LOG_TAG, "device_grap_kv:device size: %zu",
                all_devices[size].keys_values[selector_size].kv_pairs.size());
        }
        return;
    } else if (data->is_parsing_devicepps) {
        size = all_devicepps.size() - 1;
        if (size >= 0) {
            selector_size = all_devicepps[size].keys_values.size() - 1;
            all_devicepps[size].keys_values[selector_size].kv_pairs.push_back(kv);
            PAL_DBG(LOG_TAG, "devicepp_graph_kv: devicepp size: %zu",
                all_devicepps[size].keys_values[selector_size].kv_pairs.size());
        }
        return;
    } else {
        PAL_INFO(LOG_TAG, "No matching XML data\n");
    }
}

std::string PayloadBuilder::removeSpaces(const std::string& str)
{
    /* Remove leading and trailing spaces */
    return std::regex_replace(str, std::regex("^ +| +$|( ) +"), "$1");
}

std::vector<std::string> PayloadBuilder::splitStrings(const std::string& str)
{
    std::vector<std::string> tokens;
    std::stringstream check(str);
    std::string intermediate;

    while (getline(check, intermediate, ',')) {
        if (!removeSpaces(intermediate).empty())
            tokens.push_back(removeSpaces(intermediate));
    }

    return tokens;
}

void PayloadBuilder:: processKVSelectorData(struct user_xml_data *data,
    const XML_Char **attr)
{
    struct kvInfo kvinfo = {};
    int size = -1;

    std::vector<std::string> sel_values_superset;
    PAL_INFO(LOG_TAG, "process kv selectors stream:%d streampp:%d device:%d devicepp:%d",
        data->is_parsing_streams, data->is_parsing_streampps,
        data->is_parsing_devices, data->is_parsing_devicepps);

    for (int i = 0; attr[i]; i += 2) {
        kvinfo.selector_names.push_back(attr[i]);
        sel_values_superset.push_back(attr[i + 1]);
        PAL_DBG(LOG_TAG, "key_values attr :%s-%s", attr[i], attr[i + 1]);
    }

    for (int i = 0; i < kvinfo.selector_names.size(); i++) {
        PAL_DBG(LOG_TAG, "process kv selectors:%s", kvinfo.selector_names[i].c_str());
        selector_type_t selector_type =  selectorstypeLUT.at(kvinfo.selector_names[i]);

        std::vector<std::string> selector_values =
            splitStrings(sel_values_superset[i]);

        for (int j = 0; j < selector_values.size(); j++) {
            kvinfo.selector_pairs.push_back(std::make_pair(selector_type,
                selector_values[j]));
            PAL_DBG(LOG_TAG, "selector pair type:%d, value:%s", selector_type,
                selector_values[j].c_str());
        }
    }

    if (data->is_parsing_streams) {
        size = all_streams.size() - 1;
        if (size >= 0) {
            all_streams[size].keys_values.push_back(kvinfo);
            PAL_DBG(LOG_TAG, "stream_keys_values after push_back size:%zu",
                all_streams[size].keys_values.size());
        }
        return;
    } else if (data->is_parsing_streampps) {
        size = all_streampps.size() - 1;
        if (size >= 0) {
            all_streampps[size].keys_values.push_back(kvinfo);
            PAL_DBG(LOG_TAG, "streampp_keys_values after push_back size:%zu",
                all_streampps[size].keys_values.size());
        }
        return;
    } else if (data->is_parsing_devices) {
        size = all_devices.size() - 1;
        if (size >= 0) {
            all_devices[size].keys_values.push_back(kvinfo);
            PAL_DBG(LOG_TAG, "device_keys_values after push_back size:%zu",
                all_devices[size].keys_values.size());
        }
        return;
    } else if (data->is_parsing_devicepps) {
        size = all_devicepps.size() - 1;
        if (size >= 0) {
            all_devicepps[size].keys_values.push_back(kvinfo);
            PAL_DBG(LOG_TAG, "devicepp_keys_values after push_back size:%zu",
                all_devicepps[size].keys_values.size());
        }
        return;
    } else {
        PAL_INFO(LOG_TAG, "No match for is_parsing");
    }
}

void PayloadBuilder:: processKVTypeData(struct user_xml_data *data,const XML_Char **attr)
{
    struct allKVs sdTypeKV = {};
    int32_t stream_id, dev_id;
    std::vector<std::string> typeNames;

    PAL_DBG(LOG_TAG, "stream-device ID/type:%s, tag_name:%d", attr[1], data->tag);
    if (data->tag == TAG_STREAM_SEL || data->tag == TAG_STREAMPP_SEL) {
        if (!strcmp(attr[0], "type")) {
            typeNames = splitStrings(attr[1]);
            for (int i = 0; i < typeNames.size(); i++) {
                stream_id = ResourceManager::getStreamType(typeNames[i]);
                sdTypeKV.id_type.push_back(stream_id);
                PAL_DBG(LOG_TAG, "type name:%s", typeNames[i].c_str());
            }
            if (data->tag == TAG_STREAM_SEL) {
                all_streams.push_back(sdTypeKV);
                PAL_DBG(LOG_TAG, "stream types all_size: %zu", all_streams.size());
            } else if (data->tag == TAG_STREAMPP_SEL) {
                all_streampps.push_back(sdTypeKV);
                PAL_DBG(LOG_TAG, "streampp types all_size: %zu", all_streampps.size());
            }
        }
        return;
    }
    if (data->tag == TAG_DEVICE_SEL || data->tag == TAG_DEVICEPP_SEL) {
        if (!strcmp(attr[0], "id")) {
            typeNames = splitStrings(attr[1]);
            for (int i = 0; i < typeNames.size(); i++) {
                dev_id = ResourceManager::getDeviceId(typeNames[i]);
                sdTypeKV.id_type.push_back(dev_id);
                PAL_DBG(LOG_TAG, "device ID name:%s", typeNames[i].c_str());
            }
            if (data->tag == TAG_DEVICE_SEL) {
                all_devices.push_back(sdTypeKV);
                PAL_DBG(LOG_TAG, " device types all_size: %zu", all_devices.size());
            } else if (data->tag == TAG_DEVICEPP_SEL ) {
                all_devicepps.push_back(sdTypeKV);
                PAL_DBG(LOG_TAG, "devicepp types all_size: %zu", all_devicepps.size() );
            }
        }
        return;
    }
    PAL_INFO(LOG_TAG, "No matching tags found");
    return;
}

void PayloadBuilder::startTag(void *userdata, const XML_Char *tag_name,
    const XML_Char **attr)
{
    struct user_xml_data *data = ( struct user_xml_data *)userdata;

    PAL_DBG(LOG_TAG, "StartTag :%s", tag_name);
    if (!strcmp(tag_name, "graph_key_value_pair_info")) {
        data->tag = TAG_USECASEXML_ROOT;
    } else if (!strcmp(tag_name, "streams")) {
        data->is_parsing_streams = true;
    } else if (!strcmp(tag_name, "stream")) {
        data->tag = TAG_STREAM_SEL;
        processKVTypeData(data, attr);
    } else if (!strcmp(tag_name, "keys_and_values")) {
        processKVSelectorData(data, attr);
    } else if (!strcmp(tag_name, "graph_kv")) {
        processGraphKVData(data, attr);
    } else if (!strcmp(tag_name, "streampps")) {
        data->is_parsing_streampps = true;
    } else if (!strcmp(tag_name, "streampp")) {
        data->tag = TAG_STREAMPP_SEL;
        processKVTypeData(data, attr);
    } else if (!strcmp(tag_name, "devices")) {
        data->is_parsing_devices = true;
    } else if (!strcmp(tag_name, "device")){
        data->tag = TAG_DEVICE_SEL;
        processKVTypeData(data, attr);
    } else if (!strcmp(tag_name, "devicepps")){
        data->is_parsing_devicepps = true;
    } else if (!strcmp(tag_name, "devicepp")){
        data->tag = TAG_DEVICEPP_SEL;
        processKVTypeData(data, attr);
    } else {
        PAL_INFO(LOG_TAG, "No matching Tag found");
    }
}

void PayloadBuilder::endTag(void *userdata, const XML_Char *tag_name)
{
    struct user_xml_data *data = ( struct user_xml_data *)userdata;

    PAL_DBG(LOG_TAG, "Endtag: %s", tag_name);
    if ( !strcmp(tag_name, "keys_and_values") || !strcmp(tag_name, "graph_kv") ||
            !strcmp(tag_name, "stream") || !strcmp(tag_name, "device") ||
            !strcmp(tag_name, "streampp") || !strcmp(tag_name, "devicepp")) {
        return;
    }
    if (!strcmp(tag_name, "streams")) {
        data->is_parsing_streams = false;
        PAL_DBG(LOG_TAG, "is_parsing_streams: %d", data->is_parsing_streams);
        return;
    }
    if (!strcmp(tag_name, "streampps")){
        data->is_parsing_streampps = false;
        PAL_DBG(LOG_TAG, "is_parsing_streampps: %d", data->is_parsing_streampps);
        return;
    }
    if (!strcmp(tag_name, "devices")) {
        data->is_parsing_devices = false;
        PAL_DBG(LOG_TAG, "is_parsing_devices: %d", data->is_parsing_devices);
        return;
    }
    if (!strcmp(tag_name, "devicepps")) {
        data->is_parsing_devicepps = false;
        PAL_DBG(LOG_TAG, "is_parsing_devicepps : %d", data->is_parsing_devicepps);
        return;
    }
    return;
}

void PayloadBuilder::handleData(void *userdata, const char *s, int len)
{
   struct user_xml_data *data = (struct user_xml_data *)userdata;
   if (len + data->offs >= sizeof(data->data_buf) ) {
       data->offs += len;
       /* string length overflow, return */
       return;
   } else {
        memcpy(data->data_buf + data->offs, s, len);
         data->offs += len;
   }
}

int PayloadBuilder::init()
{
    XML_Parser parser;
    FILE *file = NULL;
    int ret = 0;
    int bytes_read;
    void *buf = NULL;
    struct user_xml_data tag_data;
    memset(&tag_data, 0, sizeof(tag_data));
    all_streams.clear();
    all_streampps.clear();
    all_devices.clear();
    all_devicepps.clear();

    PAL_DBG(LOG_TAG, "Enter.");
    file = fopen(USECASE_XML_FILE, "r");
    if (!file) {
        PAL_ERR(LOG_TAG, "Failed to open xml");
        ret = -EINVAL;
        goto done;
    }

    parser = XML_ParserCreate(NULL);
    if (!parser) {
        PAL_ERR(LOG_TAG, "Failed to create XML");
        goto closeFile;
    }
    XML_SetUserData(parser,&tag_data);
    XML_SetElementHandler(parser, startTag, endTag);
    XML_SetCharacterDataHandler(parser, handleData);

    while (1) {
        buf = XML_GetBuffer(parser, 1024);
        if (buf == NULL) {
            PAL_ERR(LOG_TAG, "XML_Getbuffer failed");
            ret = -EINVAL;
            goto freeParser;
        }

        bytes_read = fread(buf, 1, 1024, file);
        if (bytes_read < 0) {
            PAL_ERR(LOG_TAG, "fread failed");
            ret = -EINVAL;
            goto freeParser;
        }

        if (XML_ParseBuffer(parser, bytes_read, bytes_read == 0) == XML_STATUS_ERROR) {
            PAL_ERR(LOG_TAG, "XML ParseBuffer failed ");
            ret = -EINVAL;
            goto freeParser;
        }
        if (bytes_read == 0)
            break;
    }
    PAL_DBG(LOG_TAG, "Exit.");

freeParser:
    XML_ParserFree(parser);
closeFile:
    fclose(file);
done:
    return ret;
}

void PayloadBuilder::payloadTimestamp(uint8_t **payload, size_t *size, uint32_t moduleId)
{
    size_t payloadSize, padBytes;
    uint8_t *payloadInfo = NULL;
    struct apm_module_param_data_t* header;
    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct param_id_spr_session_time_t);
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);
    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = moduleId;
    header->param_id = PARAM_ID_SPR_SESSION_TIME;
    header->error_code = 0x0;
    header->param_size = payloadSize -  sizeof(struct apm_module_param_data_t);
    PAL_VERBOSE(LOG_TAG, "header params IID:%x param_id:%x error_code:%d param_size:%d",
                  header->module_instance_id, header->param_id,
                  header->error_code, header->param_size);
    *size = payloadSize + padBytes;;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "payload %pK size %zu", *payload, *size);
}

int PayloadBuilder::payloadACDBParam(uint8_t **alsaPayload, size_t *size,
            uint8_t *payload,
            uint32_t moduleInstanceId, uint32_t sampleRate) {
    struct apm_module_param_data_t* header;
    //uint8_t* payloadInfo = NULL;
    struct agm_acdb_param *payloadInfo = NULL;
    size_t paddedSize = 0;
    uint32_t payloadSize = 0;
    uint32_t dataLength = 0;
    struct agm_acdb_param *acdbParam = (struct agm_acdb_param *)payload;
    uint32_t appendSampleRateInCKV = 1;
    uint8_t *ptrSrc = nullptr;
    uint8_t *ptrDst = nullptr;
    uint32_t *ptr = nullptr;
    pal_effect_custom_payload_t *effectCustomPayload = nullptr;

    if (!acdbParam)
        return -EINVAL;

    if (sampleRate != 0 && acdbParam->isTKV == PARAM_TKV) {
        PAL_ERR(LOG_TAG, "Sample Rate %d CKV and TKV are not compatible.",
                    sampleRate);
        return -EINVAL;
    }

    // step 1: get param data size = blob size - kv size - param id size
    payloadSize = acdbParam->blob_size -
                    acdbParam->num_kvs * sizeof(struct gsl_key_value_pair)
                    - sizeof(pal_effect_custom_payload_t);

    paddedSize = PAL_ALIGN_8BYTE(payloadSize);
    if (sampleRate) {
        //CKV
        // step 1. check sample rate is in kv or not
        PAL_INFO(LOG_TAG, "CKV param to ACDB");
        pal_key_value_pair_t *rawCKVPair = nullptr;
        rawCKVPair = (pal_key_value_pair_t *)acdbParam->blob;
        for (int k = 0; k < acdbParam->num_kvs; k++) {
            if (rawCKVPair[k].key == SAMPLINGRATE) {
                PAL_INFO(LOG_TAG, "Sample rate is in CKV. No need to append.");
                appendSampleRateInCKV = 0;
                break;
            }
        }
        PAL_DBG(LOG_TAG, "is sample rate appended in CKV? %x",
                                appendSampleRateInCKV);
    } else {
        //TKV
        appendSampleRateInCKV = 0;
    }

    payloadInfo = (struct agm_acdb_param *)calloc(1,
        sizeof(struct agm_acdb_param) +
        (acdbParam->num_kvs + appendSampleRateInCKV) *
        sizeof(struct gsl_key_value_pair) +
        sizeof(struct apm_module_param_data_t) + paddedSize -
        sizeof(pal_effect_custom_payload_t));

    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "failed to allocate memory.");
        return -ENOMEM;
    }

    // copy acdb meta + kv
    dataLength = sizeof(struct agm_acdb_param) +
                    acdbParam->num_kvs * sizeof(struct gsl_key_value_pair);
    ar_mem_cpy((uint8_t *)payloadInfo, dataLength,
                (uint8_t *)acdbParam, dataLength);
    //update blob size
    payloadInfo->blob_size = payloadInfo->blob_size +
                            sizeof(struct apm_module_param_data_t) -
                            sizeof(pal_effect_custom_payload_t) +
                            appendSampleRateInCKV * sizeof(struct gsl_key_value_pair);
    payloadInfo->num_kvs = payloadInfo->num_kvs + appendSampleRateInCKV;
    if (appendSampleRateInCKV) {
        ptr = (uint32_t *)((uint8_t *)payloadInfo + dataLength);
        *ptr++ = SAMPLINGRATE;
        *ptr = sampleRate;
        header = (struct apm_module_param_data_t *)((uint8_t *)payloadInfo +
                    dataLength + sizeof(struct gsl_key_value_pair));
    } else {
        header = (struct apm_module_param_data_t *)
                    ((uint8_t *)payloadInfo + dataLength);
    }

    effectCustomPayload = (pal_effect_custom_payload_t *)
                                ((uint8_t *)acdbParam + dataLength);
    header->module_instance_id = moduleInstanceId;
    header->param_id = effectCustomPayload->paramId;
    header->param_size = paddedSize;
    header->error_code = 0x0;

    if (paddedSize) {
        ptrDst = (uint8_t *)header + sizeof(struct apm_module_param_data_t);
        ptrSrc = (uint8_t *)effectCustomPayload->data;
        // padded bytes are zereo by calloc. no need to copy.
        ar_mem_cpy(ptrDst, payloadSize, ptrSrc, payloadSize);
    }
    *size = dataLength + paddedSize + sizeof(struct apm_module_param_data_t);
    *alsaPayload = (uint8_t *)payloadInfo;
    PAL_DBG(LOG_TAG, "ALSA payload %pK size %zu", *alsaPayload, *size);

    return 0;
}

int PayloadBuilder::payloadCustomParam(uint8_t **alsaPayload, size_t *size,
            uint32_t *customPayload, uint32_t customPayloadSize,
            uint32_t moduleInstanceId, uint32_t paramId) {
    struct apm_module_param_data_t* header;
    uint8_t* payloadInfo = NULL;
    size_t alsaPayloadSize = 0;

    alsaPayloadSize = PAL_ALIGN_8BYTE(sizeof(struct apm_module_param_data_t)
                                        + customPayloadSize);
    payloadInfo = (uint8_t *)calloc(1, (size_t)alsaPayloadSize);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "failed to allocate memory.");
        return -ENOMEM;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = moduleInstanceId;
    header->param_id = paramId;
    header->error_code = 0x0;
    header->param_size = customPayloadSize;
    if (customPayloadSize)
        ar_mem_cpy(payloadInfo + sizeof(struct apm_module_param_data_t),
                         customPayloadSize,
                         customPayload,
                         customPayloadSize);
    *size = alsaPayloadSize;
    *alsaPayload = payloadInfo;

    PAL_DBG(LOG_TAG, "ALSA payload %pK size %zu", *alsaPayload, *size);

    return 0;
}

int PayloadBuilder::payloadSVAConfig(uint8_t **payload, size_t *size,
            uint8_t *config, size_t config_size,
            uint32_t miid, uint32_t param_id) {
    struct apm_module_param_data_t* header = nullptr;
    uint8_t* payloadInfo = nullptr;
    size_t payloadSize = 0;

    payloadSize = PAL_ALIGN_8BYTE(
        sizeof(struct apm_module_param_data_t) + config_size);
    payloadInfo = (uint8_t *)calloc(1, payloadSize);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "failed to allocate memory.");
        return -ENOMEM;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = miid;
    header->param_id = param_id;
    header->error_code = 0x0;
    header->param_size = config_size;
    if (config_size)
        ar_mem_cpy(payloadInfo + sizeof(struct apm_module_param_data_t),
            config_size, config, config_size);
    *size = payloadSize;
    *payload = payloadInfo;

    PAL_INFO(LOG_TAG, "PID 0x%x, payload %pK size %zu", param_id, *payload, *size);

    return 0;
}

void PayloadBuilder::payloadQuery(uint8_t **payload, size_t *size,
                    uint32_t moduleId, uint32_t paramId, uint32_t querySize)
{
    struct apm_module_param_data_t* header;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;

    payloadSize = sizeof(struct apm_module_param_data_t) + querySize;
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = moduleId;
    header->param_id = paramId;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);

    *size = payloadSize + padBytes;
    *payload = payloadInfo;
}

void PayloadBuilder::payloadDOAInfo(uint8_t **payload, size_t *size, uint32_t moduleId)
{
    struct apm_module_param_data_t* header;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;

    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct ffv_doa_tracking_monitor_t);
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = moduleId;
    header->param_id = PARAM_ID_FFV_DOA_TRACKING_MONITOR;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);

    *size = payloadSize + padBytes;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "payload %pK size %zu", *payload, *size);
}

void PayloadBuilder::payloadTWSConfig(uint8_t** payload, size_t* size,
        uint32_t miid, bool isTwsMonoModeOn, uint32_t codecFormat)
{
    struct apm_module_param_data_t* header = NULL;
    uint8_t* payloadInfo = NULL;
    uint32_t param_id = 0, val = 2;
    size_t payloadSize = 0, customPayloadSize = 0;
    param_id_aptx_classic_switch_enc_pcm_input_payload_t *aptx_classic_payload;
    param_id_aptx_adaptive_enc_switch_to_mono_t *aptx_adaptive_payload;

    if (codecFormat == CODEC_TYPE_APTX_DUAL_MONO) {
        param_id = PARAM_ID_APTX_CLASSIC_SWITCH_ENC_PCM_INPUT;
        customPayloadSize = sizeof(param_id_aptx_classic_switch_enc_pcm_input_payload_t);
    } else {
        param_id = PARAM_ID_APTX_ADAPTIVE_ENC_SWITCH_TO_MONO;
        customPayloadSize = sizeof(param_id_aptx_adaptive_enc_switch_to_mono_t);
    }
    payloadSize = PAL_ALIGN_8BYTE(sizeof(struct apm_module_param_data_t)
                                        + customPayloadSize);
    payloadInfo = (uint8_t *)calloc(1, (size_t)payloadSize);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "failed to allocate memory.");
        return;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = miid;
    header->param_id = param_id;
    header->error_code = 0x0;
    header->param_size = customPayloadSize;
    val = (isTwsMonoModeOn == true) ? 1 : 2;
    if (codecFormat == CODEC_TYPE_APTX_DUAL_MONO) {
        aptx_classic_payload =
            (param_id_aptx_classic_switch_enc_pcm_input_payload_t*)(payloadInfo +
             sizeof(struct apm_module_param_data_t));
        aptx_classic_payload->transition_direction = val;
        ar_mem_cpy(payloadInfo + sizeof(struct apm_module_param_data_t),
                         customPayloadSize,
                         aptx_classic_payload,
                         customPayloadSize);
    } else {
        aptx_adaptive_payload =
            (param_id_aptx_adaptive_enc_switch_to_mono_t*)(payloadInfo +
             sizeof(struct apm_module_param_data_t));
        aptx_adaptive_payload->switch_between_mono_and_stereo = val;
        ar_mem_cpy(payloadInfo + sizeof(struct apm_module_param_data_t),
                         customPayloadSize,
                         aptx_adaptive_payload,
                         customPayloadSize);
    }

    *size = payloadSize;
    *payload = payloadInfo;
}

void PayloadBuilder::payloadLC3Config(uint8_t** payload, size_t* size,
        uint32_t miid, bool isLC3MonoModeOn)
{
    struct apm_module_param_data_t* header = NULL;
    uint8_t* payloadInfo = NULL;
    uint32_t param_id = 0, val = 2;
    size_t payloadSize = 0, customPayloadSize = 0;
    param_id_lc3_encoder_switch_enc_pcm_input_payload_t *lc3_payload;

    param_id = PARAM_ID_LC3_ENC_DOWNMIX_2_MONO;
    customPayloadSize = sizeof(param_id_lc3_encoder_switch_enc_pcm_input_payload_t);

    payloadSize = PAL_ALIGN_8BYTE(sizeof(struct apm_module_param_data_t)
                                        + customPayloadSize);
    payloadInfo = (uint8_t *)calloc(1, (size_t)payloadSize);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "failed to allocate memory.");
        return;
    }

    header = (struct apm_module_param_data_t*)payloadInfo;
    header->module_instance_id = miid;
    header->param_id = param_id;
    header->error_code = 0x0;
    header->param_size = customPayloadSize;
    val = (isLC3MonoModeOn == true) ? 1 : 2;

    lc3_payload =
        (param_id_lc3_encoder_switch_enc_pcm_input_payload_t *)(payloadInfo +
         sizeof(struct apm_module_param_data_t));
    lc3_payload->transition_direction = val;
    ar_mem_cpy(payloadInfo + sizeof(struct apm_module_param_data_t),
                     customPayloadSize,
                     lc3_payload,
                     customPayloadSize);

    *size = payloadSize;
    *payload = payloadInfo;
}

void PayloadBuilder::payloadRATConfig(uint8_t** payload, size_t* size,
        uint32_t miid, struct pal_media_config *data)
{
    struct apm_module_param_data_t* header = NULL;
    struct param_id_rat_mf_t *ratConf;
    int numChannel;
    uint32_t bitWidth;
    uint16_t* pcmChannel = NULL;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;

    if (!data) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }

    numChannel = data->ch_info.channels;
    bitWidth = data->bit_width;
    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct param_id_rat_mf_t) +
                  sizeof(uint16_t)*numChannel;
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    ratConf = (struct param_id_rat_mf_t*)(payloadInfo +
               sizeof(struct apm_module_param_data_t));
    pcmChannel = (uint16_t*)(payloadInfo + sizeof(struct apm_module_param_data_t) +
                                       sizeof(struct param_id_rat_mf_t));

    header->module_instance_id = miid;
    header->param_id = PARAM_ID_RAT_MEDIA_FORMAT;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_DBG(LOG_TAG, "header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    ratConf->sample_rate = data->sample_rate;
    if (bitWidth == 16 || bitWidth == 32) {
        ratConf->bits_per_sample = bitWidth;
        ratConf->q_factor =  bitWidth - 1;
    } else if (bitWidth == 24) {
        ratConf->bits_per_sample = 32;
        ratConf->q_factor = 27;
    }
    ratConf->data_format = DATA_FORMAT_FIXED_POINT;
    ratConf->num_channels = numChannel;
    populateChannelMap(pcmChannel, numChannel);
    *size = payloadSize + padBytes;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "sample_rate:%d bits_per_sample:%d q_factor:%d data_format:%d num_channels:%d",
                      ratConf->sample_rate, ratConf->bits_per_sample, ratConf->q_factor,
                      ratConf->data_format, ratConf->num_channels);
    PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", payloadInfo,
                *size);
}

void PayloadBuilder::payloadPcmCnvConfig(uint8_t** payload, size_t* size,
        uint32_t miid, struct pal_media_config *data)
{
    struct apm_module_param_data_t* header = NULL;
    struct media_format_t *mediaFmtHdr;
    struct payload_pcm_output_format_cfg_t *mediaFmtPayload;
    int numChannels;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;
    uint8_t *pcmChannel;

    if (!data) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }

    numChannels = data->ch_info.channels;
    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct media_format_t) +
                  sizeof(struct payload_pcm_output_format_cfg_t) +
                  sizeof(uint8_t)*numChannels;
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
        return;
    }
    header          = (struct apm_module_param_data_t*)payloadInfo;
    mediaFmtHdr     = (struct media_format_t*)(payloadInfo +
                      sizeof(struct apm_module_param_data_t));
    mediaFmtPayload = (struct payload_pcm_output_format_cfg_t*)(payloadInfo +
                      sizeof(struct apm_module_param_data_t) +
                      sizeof(struct media_format_t));
    pcmChannel      = (uint8_t*)(payloadInfo + sizeof(struct apm_module_param_data_t) +
                      sizeof(struct media_format_t) +
                      sizeof(struct payload_pcm_output_format_cfg_t));

    header->module_instance_id = miid;
    header->param_id           = PARAM_ID_PCM_OUTPUT_FORMAT_CFG;
    header->error_code         = 0x0;
    header->param_size         = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_DBG(LOG_TAG, "header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    mediaFmtHdr->data_format  = DATA_FORMAT_FIXED_POINT;
    mediaFmtHdr->fmt_id       = MEDIA_FMT_ID_PCM;
    mediaFmtHdr->payload_size = sizeof(payload_pcm_output_format_cfg_t) +
                                sizeof(uint8_t) * numChannels;
    PAL_DBG(LOG_TAG, "mediaFmtHdr data_format:%x fmt_id:%x payload_size:%d channels:%d",
                      mediaFmtHdr->data_format, mediaFmtHdr->fmt_id,
                      mediaFmtHdr->payload_size, numChannels);

    mediaFmtPayload->endianness      = PCM_LITTLE_ENDIAN;
    mediaFmtPayload->num_channels    = data->ch_info.channels;
    if ((data->bit_width == 16) || (data->bit_width == 32)) {
        mediaFmtPayload->bit_width       = data->bit_width;
        mediaFmtPayload->bits_per_sample = data->bit_width;
        mediaFmtPayload->q_factor        = data->bit_width - 1;
        mediaFmtPayload->alignment       = PCM_LSB_ALIGNED;
    } else if (data->bit_width == 24) {
        // convert to Q31 that's expected by HD encoders.
        mediaFmtPayload->bit_width       = BIT_WIDTH_24;
        mediaFmtPayload->bits_per_sample = BITS_PER_SAMPLE_32;
        mediaFmtPayload->q_factor        = PCM_Q_FACTOR_31;
        mediaFmtPayload->alignment       = PCM_MSB_ALIGNED;
    } else {
        PAL_ERR(LOG_TAG, "invalid bit width %d", data->bit_width);
        delete[] payloadInfo;
        *size = 0;
        *payload = NULL;
        return;
    }
    mediaFmtPayload->interleaved     = PCM_INTERLEAVED;
    PAL_DBG(LOG_TAG, "interleaved:%d bit_width:%d bits_per_sample:%d q_factor:%d",
                  mediaFmtPayload->interleaved, mediaFmtPayload->bit_width,
                  mediaFmtPayload->bits_per_sample, mediaFmtPayload->q_factor);
    populateChannelMap(pcmChannel, numChannels);
    *size = (payloadSize + padBytes);
    *payload = payloadInfo;

    PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", payloadInfo,
                *size);
}

void PayloadBuilder::payloadCopPackConfig(uint8_t** payload, size_t* size,
        uint32_t miid, struct pal_media_config *data)
{
    struct apm_module_param_data_t* header = NULL;
    struct param_id_cop_pack_output_media_fmt_t *copPack  = NULL;
    int numChannel;
    uint16_t* pcmChannel = NULL;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;

    if (!data) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }

    numChannel = data->ch_info.channels;
    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct param_id_cop_pack_output_media_fmt_t) +
                  sizeof(uint16_t)*numChannel;
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo alloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    copPack = (struct param_id_cop_pack_output_media_fmt_t*)(payloadInfo +
               sizeof(struct apm_module_param_data_t));
    pcmChannel = (uint16_t*)(payloadInfo +
                          sizeof(struct apm_module_param_data_t) +
                          sizeof(struct param_id_cop_pack_output_media_fmt_t));

    header->module_instance_id = miid;
    header->param_id = PARAM_ID_COP_PACKETIZER_OUTPUT_MEDIA_FORMAT;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_DBG(LOG_TAG, "header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    copPack->sampling_rate = data->sample_rate;
    copPack->bits_per_sample = data->bit_width;
    copPack->num_channels = numChannel;
    populateChannelMap(pcmChannel, numChannel);
    *size = payloadSize + padBytes;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "sample_rate:%d bits_per_sample:%d num_channels:%d",
                      copPack->sampling_rate, copPack->bits_per_sample, copPack->num_channels);
    PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", payloadInfo,
                *size);
}

void PayloadBuilder::payloadCopV2PackConfig(uint8_t** payload, size_t* size,
        uint32_t miid, void *codecInfo)
{
    struct apm_module_param_data_t* header = NULL;
    struct param_id_cop_v2_stream_info_t *streamInfo = NULL;
    uint8_t* payloadInfo = NULL;
    audio_lc3_codec_cfg_t *bleCfg = NULL;
    struct cop_v2_stream_info_map_t* streamMap = NULL;
    size_t payloadSize = 0, padBytes = 0;
    uint64_t channel_mask = 0;
    int i = 0;

    bleCfg = (audio_lc3_codec_cfg_t *)codecInfo;
    if (!bleCfg) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }

    payloadSize = sizeof(struct apm_module_param_data_t) +
                  sizeof(struct param_id_cop_pack_output_media_fmt_t) +
                  sizeof(struct cop_v2_stream_info_map_t) * bleCfg->enc_cfg.stream_map_size;
    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo alloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    streamInfo = (struct param_id_cop_v2_stream_info_t*)(payloadInfo +
                  sizeof(struct apm_module_param_data_t));
    streamMap = (struct cop_v2_stream_info_map_t*)(payloadInfo +
                 sizeof(struct apm_module_param_data_t) +
                 sizeof(struct param_id_cop_v2_stream_info_t));

    header->module_instance_id = miid;
    header->param_id = PARAM_ID_COP_V2_STREAM_INFO;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_DBG(LOG_TAG, "header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    streamInfo->num_streams = bleCfg->enc_cfg.stream_map_size;;
    for (i = 0; i < streamInfo->num_streams; i++) {
        channel_mask = convert_channel_map(bleCfg->enc_cfg.streamMapOut[i].audio_location);
        streamMap[i].stream_id = bleCfg->enc_cfg.streamMapOut[i].stream_id;
        streamMap[i].direction = bleCfg->enc_cfg.streamMapOut[i].direction;
        streamMap[i].channel_mask_lsw = channel_mask  & 0x00000000FFFFFFFF;
        streamMap[i].channel_mask_msw = (channel_mask & 0xFFFFFFFF00000000) >> 32;
    }

    *size = payloadSize + padBytes;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", payloadInfo, *size);
}

void PayloadBuilder::payloadCopV2DepackConfig(uint8_t** payload, size_t* size,
        uint32_t miid, void *codecInfo, bool isStreamMapDirIn)
{
    struct apm_module_param_data_t* header = NULL;
    struct param_id_cop_v2_stream_info_t *streamInfo = NULL;
    uint8_t* payloadInfo = NULL;
    audio_lc3_codec_cfg_t *bleCfg = NULL;
    struct cop_v2_stream_info_map_t* streamMap = NULL;
    size_t payloadSize = 0, padBytes = 0;
    uint64_t channel_mask = 0;
    int i = 0;

    bleCfg = (audio_lc3_codec_cfg_t *)codecInfo;
    if (!bleCfg) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }

    if (!isStreamMapDirIn) {
        payloadSize = sizeof(struct apm_module_param_data_t) +
                      sizeof(struct param_id_cop_pack_output_media_fmt_t) +
                      sizeof(struct cop_v2_stream_info_map_t) * bleCfg->enc_cfg.stream_map_size;
    } else if (isStreamMapDirIn && bleCfg->dec_cfg.stream_map_size != 0) {
        payloadSize = sizeof(struct apm_module_param_data_t) +
                      sizeof(struct param_id_cop_pack_output_media_fmt_t) +
                      sizeof(struct cop_v2_stream_info_map_t) * bleCfg->dec_cfg.stream_map_size;
    } else if (isStreamMapDirIn && bleCfg->dec_cfg.stream_map_size == 0) {
        PAL_ERR(LOG_TAG, "isStreamMapDirIn is true, but empty streamMapIn");
        return;
    }

    padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

    payloadInfo = new uint8_t[payloadSize + padBytes]();
    if (!payloadInfo) {
        PAL_ERR(LOG_TAG, "payloadInfo alloc failed %s", strerror(errno));
        return;
    }
    header = (struct apm_module_param_data_t*)payloadInfo;
    streamInfo = (struct param_id_cop_v2_stream_info_t*)(payloadInfo +
                  sizeof(struct apm_module_param_data_t));
    streamMap = (struct cop_v2_stream_info_map_t*)(payloadInfo +
                 sizeof(struct apm_module_param_data_t) +
                 sizeof(struct param_id_cop_v2_stream_info_t));

    header->module_instance_id = miid;
    header->param_id = PARAM_ID_COP_V2_STREAM_INFO;
    header->error_code = 0x0;
    header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    PAL_DBG(LOG_TAG, "header params \n IID:%x param_id:%x error_code:%d param_size:%d",
                      header->module_instance_id, header->param_id,
                      header->error_code, header->param_size);

    if (!isStreamMapDirIn) {
        streamInfo->num_streams = bleCfg->enc_cfg.stream_map_size;;
        for (i = 0; i < streamInfo->num_streams; i++) {
            channel_mask = convert_channel_map(bleCfg->enc_cfg.streamMapOut[i].audio_location);
            streamMap[i].stream_id = bleCfg->enc_cfg.streamMapOut[i].stream_id;
            streamMap[i].direction = bleCfg->enc_cfg.streamMapOut[i].direction;
            streamMap[i].channel_mask_lsw = channel_mask  & 0x00000000FFFFFFFF;
            streamMap[i].channel_mask_msw = (channel_mask & 0xFFFFFFFF00000000) >> 32;
        }
    } else {
        streamInfo->num_streams = bleCfg->dec_cfg.stream_map_size;
        for (i = 0; i < streamInfo->num_streams; i++) {
            channel_mask = convert_channel_map(bleCfg->dec_cfg.streamMapIn[i].audio_location);
            streamMap[i].stream_id = bleCfg->dec_cfg.streamMapIn[i].stream_id;
            streamMap[i].direction = bleCfg->dec_cfg.streamMapIn[i].direction;
            streamMap[i].channel_mask_lsw = channel_mask  & 0x00000000FFFFFFFF;
            streamMap[i].channel_mask_msw = (channel_mask & 0xFFFFFFFF00000000) >> 32;
        }
    }
    *size = payloadSize + padBytes;
    *payload = payloadInfo;
    PAL_DBG(LOG_TAG, "customPayload address %pK and size %zu", payloadInfo, *size);
}

/* Used for VI feedback device KV as of now */
int PayloadBuilder::getDeviceKV(int dev_id, std::vector<std::pair<int,int>>& deviceKV)
{
    PAL_DBG(LOG_TAG, "Enter: device ID: %d", dev_id);
    std::vector<std::pair<selector_type_t, std::string>> empty_selector_pairs;

    return retrieveKVs(empty_selector_pairs, dev_id, all_devices, deviceKV);
}

/** Used for BT device KVs only */
int PayloadBuilder::getBtDeviceKV(int dev_id, std::vector<std::pair<int,int>>& deviceKV,
    uint32_t codecFormat, bool isAbrEnabled, bool isHostless)
{
    PAL_INFO(LOG_TAG, "Enter: codecFormat:0x%x, isabrEnabled:%d, isHostless:%d",
        codecFormat, isAbrEnabled, isHostless);
    std::vector<std::pair<selector_type_t, std::string>> filled_selector_pairs;

    filled_selector_pairs.push_back(std::make_pair(CODECFORMAT_SEL,
                                   btCodecFormatLUT.at(codecFormat)));

    if (dev_id == PAL_DEVICE_OUT_BLUETOOTH_A2DP) {
        filled_selector_pairs.push_back(std::make_pair(ABR_ENABLED_SEL,
            isAbrEnabled ? "TRUE" : "FALSE"));
        filled_selector_pairs.push_back(std::make_pair(HOSTLESS_SEL,
            isHostless ? "TRUE" : "FALSE"));
    } else if (dev_id == PAL_DEVICE_IN_BLUETOOTH_A2DP) {
        filled_selector_pairs.push_back(std::make_pair(HOSTLESS_SEL,
            isHostless ? "TRUE" : "FALSE"));
    }
    return retrieveKVs(filled_selector_pairs, dev_id, all_devices, deviceKV);
}

/** Used for Loopback stream types only */
int PayloadBuilder::populateStreamKV(Stream* s, std::vector<std::pair<int,int>> &keyVectorRx,
        std::vector<std::pair<int,int>> &keyVectorTx, struct vsid_info vsidinfo)
{
    int status = 0;
    struct pal_stream_attributes *sattr = NULL;
    std::vector<std::string> selector_names;
    std::vector<std::pair<selector_type_t, std::string>> filled_selector_pairs;


    PAL_DBG(LOG_TAG, "enter");
    sattr = new struct pal_stream_attributes();
    if (!sattr) {
        PAL_ERR(LOG_TAG, "sattr alloc failed %s status %d", strerror(errno), status);
        status = -ENOMEM;
        goto exit;
    }
    status = s->getStreamAttributes(sattr);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getStreamAttributes failed status %d", status);
        goto free_sattr;
    }

    PAL_DBG(LOG_TAG, "stream attribute type %d", sattr->type);
    if (sattr->type == PAL_STREAM_LOOPBACK) {
        if (sattr->info.opt_stream_info.loopback_type == PAL_STREAM_LOOPBACK_HFP_RX) {
            filled_selector_pairs.push_back(std::make_pair(DIRECTION_SEL, "RX"));
            filled_selector_pairs.push_back(std::make_pair(SUB_TYPE_SEL,
                loopbackLUT.at(sattr->info.opt_stream_info.loopback_type)));
            retrieveKVs(filled_selector_pairs, sattr->type, all_streams, keyVectorRx);

            filled_selector_pairs.clear();
            filled_selector_pairs.push_back(std::make_pair(DIRECTION_SEL, "TX"));
            filled_selector_pairs.push_back(std::make_pair(SUB_TYPE_SEL,
                loopbackLUT.at(sattr->info.opt_stream_info.loopback_type)));
            retrieveKVs(filled_selector_pairs ,sattr->type, all_streams, keyVectorTx);
        } else if (sattr->info.opt_stream_info.loopback_type == PAL_STREAM_LOOPBACK_HFP_TX) {
           /* no StreamKV for HFP TX */
        } else {
            selector_names = retrieveSelectors(sattr->type, all_streams);
            if (selector_names.empty() != true)
               filled_selector_pairs = getSelectorValues(selector_names, s);
            retrieveKVs(filled_selector_pairs ,sattr->type, all_streams, keyVectorRx);
        }
    } else if (sattr->type == PAL_STREAM_VOICE_CALL) {
        filled_selector_pairs.push_back(std::make_pair(DIRECTION_SEL, "RX"));
        filled_selector_pairs.push_back(std::make_pair(VSID_SEL,
            vsidLUT.at(sattr->info.voice_call_info.VSID)));
        retrieveKVs(filled_selector_pairs ,sattr->type, all_streams, keyVectorRx);

        filled_selector_pairs.clear();
        filled_selector_pairs.push_back(std::make_pair(DIRECTION_SEL, "TX"));
        filled_selector_pairs.push_back(std::make_pair(VSID_SEL,
            vsidLUT.at(sattr->info.voice_call_info.VSID)));
        retrieveKVs(filled_selector_pairs ,sattr->type, all_streams, keyVectorTx);
    } else {
        PAL_INFO(LOG_TAG, "KVs not provided for stream type:%d", sattr->type);
    }
free_sattr:
    delete sattr;
exit:
    return status;
}

/** Used for Loopback stream types only */
int PayloadBuilder::populateStreamPPKV(Stream* s, std::vector <std::pair<int,int>> &keyVectorRx,
        std::vector <std::pair<int,int>> &keyVectorTx __unused)
{
    int status = 0;
    struct pal_stream_attributes *sattr = NULL;
    std::vector <std::string> selectors;
    std::vector <std::pair<selector_type_t, std::string>> filled_selector_pairs;

    PAL_DBG(LOG_TAG, "enter");
    sattr = new struct pal_stream_attributes();
    if (!sattr) {
        PAL_ERR(LOG_TAG, "sattr alloc failed %s status %d", strerror(errno), status);
        status = -ENOMEM;
        goto exit;
    }
    status = s->getStreamAttributes(sattr);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getStreamAttributes Failed status %d", status);
        goto free_sattr;
    }

    PAL_DBG(LOG_TAG, "stream attribute type %d", sattr->type);

    if (sattr->type == PAL_STREAM_VOICE_CALL) {
        selectors = retrieveSelectors(sattr->type, all_streampps);
        if (selectors.empty() != true)
            filled_selector_pairs = getSelectorValues(selectors, s);
        retrieveKVs(filled_selector_pairs ,sattr->type, all_streampps, keyVectorRx);
    } else {
        PAL_INFO(LOG_TAG, "KVs not provided for stream type:%d", sattr->type);
    }

free_sattr:
    delete sattr;
exit:
    return status;
}

bool PayloadBuilder::compareSelectorPairs(
    std::vector<std::pair<selector_type_t, std::string>>& selector_pairs,
    std::vector<std::pair<selector_type_t, std::string>>& filled_selector_pairs)
{
    int count = 0;
    bool result = false;

    PAL_DBG(LOG_TAG, "Enter: selector size: %zu filled_sel size: %zu",
        selector_pairs.size(), filled_selector_pairs.size());
    if (selector_pairs.size() == filled_selector_pairs.size()) {
        result = std::equal(selector_pairs.begin(), selector_pairs.end(),
            filled_selector_pairs.begin());
        if (result) {
             PAL_DBG(LOG_TAG,"Return True");
            return true;
        }
    }
    else {
        for (int i = 0; i < filled_selector_pairs.size(); i++) {
            if (selector_pairs.end() != std::find(selector_pairs.begin(),
                selector_pairs.end(), filled_selector_pairs[i])) {
                count++;
                PAL_DBG(LOG_TAG,"Inside the find loop count=%d", count);
            }
        }
        PAL_DBG(LOG_TAG, "After find count:%d", count);
        if (filled_selector_pairs.size() == count) {
            PAL_DBG(LOG_TAG,"Return True");
            return true;
        }
    }
    PAL_DBG(LOG_TAG, "No matching selectors found");
    return false;
}

int PayloadBuilder::retrieveKVs(std::vector<std::pair<selector_type_t, std::string>>
    &filled_selector_pairs, uint32_t type, std::vector<allKVs> any_type,
    std::vector<std::pair<int, int>> &keyVector)
{
    bool found = false;

    PAL_DBG(LOG_TAG, "enter");

    for (int32_t i = 0; i < any_type.size(); i++) {
        if (isIdTypeAvailable(type, any_type[i].id_type)) {
            for (int32_t j = 0; j < any_type[i].keys_values.size(); j++) {
                if (filled_selector_pairs.empty() != true) {
                    if (compareSelectorPairs(any_type[i].keys_values[j].selector_pairs,
                           filled_selector_pairs)) {
                        for (int32_t k = 0; k < any_type[i].keys_values[j].kv_pairs.size(); k++) {
                            keyVector.push_back(
                                std::make_pair(any_type[i].keys_values[j].kv_pairs[k].key,
                                any_type[i].keys_values[j].kv_pairs[k].value));
                            PAL_INFO(LOG_TAG, "key: 0x%x value: 0x%x\n",
                                any_type[i].keys_values[j].kv_pairs[k].key,
                                any_type[i].keys_values[j].kv_pairs[k].value);
                        }
                        found = true;
                        break;
                    }
                } else {
                    for (int32_t k = 0; k < any_type[i].keys_values[j].kv_pairs.size(); k++) {
                        keyVector.push_back(
                            std::make_pair(any_type[i].keys_values[j].kv_pairs[k].key,
                            any_type[i].keys_values[j].kv_pairs[k].value));
                        PAL_INFO(LOG_TAG, "key: 0x%x value: 0x%x\n",
                            any_type[i].keys_values[j].kv_pairs[k].key,
                            any_type[i].keys_values[j].kv_pairs[k].value);
                    }
                    found = true;
                    break;
                }
            }
        }
    }

    if (found) {
        PAL_INFO(LOG_TAG, "KVs found for the stream type/dev id: %d", type);
        return 0;
    } else {
        PAL_ERR(LOG_TAG, "No KVs found for the stream type/dev id: %d", type);
        return -EINVAL;
    }
}

std::vector<std::pair<selector_type_t, std::string>> PayloadBuilder::getSelectorValues(
    std::vector<std::string> &selector_names, Stream* s)
{
    int instance_id = 0;
    int status = 0;
    struct pal_stream_attributes *sattr = NULL;
    std::stringstream st;
    struct pal_device dAttr;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::vector<std::pair<selector_type_t, std::string>> filled_selector_pairs;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    PAL_INFO(LOG_TAG, "enter");
    sattr = new struct pal_stream_attributes();
    if (!sattr) {
        PAL_ERR(LOG_TAG, "sattr alloc failed %s status %d", strerror(errno));
        goto exit;
    }
    status = s->getStreamAttributes(sattr);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getStreamAttributes failed status %d", status);
        goto free_sattr;
    }
    memset (&dAttr, 0, sizeof(struct pal_device));

    PAL_INFO(LOG_TAG, "stream attribute type:%d Direction:%d",
        sattr->type, sattr->direction);

    for (int i = 0; i < selector_names.size(); i++) {
        PAL_DBG(LOG_TAG, "selectors_strings :%s", selector_names[i].c_str());
        selector_type_t selector_type =  selectorstypeLUT.at(selector_names[i]);
        switch (selector_type) {
            case DIRECTION_SEL:
                if (sattr->direction == PAL_AUDIO_OUTPUT)
                    filled_selector_pairs.push_back(std::make_pair(selector_type, "RX"));
                else if (sattr->direction == PAL_AUDIO_INPUT)
                    filled_selector_pairs.push_back(std::make_pair(selector_type, "TX"));
                else if (sattr->direction == PAL_AUDIO_INPUT_OUTPUT)
                    filled_selector_pairs.push_back(std::make_pair(selector_type, "RX_TX"));
                else
                    PAL_ERR(LOG_TAG, "Invalid stream direction %d", sattr->direction);
                PAL_INFO(LOG_TAG, "Direction = %d", sattr->direction);
            break;
            case BITWIDTH_SEL:
                /* If any usecase defined with bitwidth,need to update */
            break;
            case INSTANCE_SEL:
                if (sattr->type == PAL_STREAM_VOICE_UI)
                    instance_id = s->getInstanceId();
                else
                    instance_id = rm->getStreamInstanceID(s);
                if (instance_id < INSTANCE_1) {
                    PAL_ERR(LOG_TAG, "Invalid instance id %d", instance_id);
                    goto free_sattr;
                }
                st << instance_id;
                filled_selector_pairs.push_back(std::make_pair(selector_type, st.str()));
                PAL_INFO(LOG_TAG, "Instance = %d", instance_id);
                break;
            case SUB_TYPE_SEL:
                if (sattr->type == PAL_STREAM_PROXY) {
                    if (sattr->direction == PAL_AUDIO_INPUT) {
                        if (sattr->info.opt_stream_info.tx_proxy_type == PAL_STREAM_PROXY_TX_WFD)
                            filled_selector_pairs.push_back(std::make_pair(selector_type,
                                "PAL_STREAM_PROXY_TX_WFD"));
                        else if (sattr->info.opt_stream_info.tx_proxy_type == PAL_STREAM_PROXY_TX_TELEPHONY_RX)
                            filled_selector_pairs.push_back(std::make_pair(selector_type,
                                "PAL_STREAM_PROXY_TX_TELEPHONY_RX"));
                        PAL_INFO(LOG_TAG, "Proxy type = %d",
                            sattr->info.opt_stream_info.tx_proxy_type);
                    }
                } else if (sattr->type == PAL_STREAM_LOOPBACK) {
                    filled_selector_pairs.push_back(std::make_pair(selector_type,
                        loopbackLUT.at(sattr->info.opt_stream_info.loopback_type)));
                    PAL_INFO(LOG_TAG, "Loopback type = %d",
                        sattr->info.opt_stream_info.loopback_type);
                }
                break;
            case VUI_MODULE_TYPE_SEL:
                if (!s) {
                    PAL_ERR(LOG_TAG, "Invalid stream");
                    goto free_sattr;
                }

                filled_selector_pairs.push_back(std::make_pair(selector_type,
                    s->getStreamSelector()));
                PAL_INFO(LOG_TAG, "VUI module type:%s", s->getStreamSelector().c_str());
                break;
            case ACD_MODULE_TYPE_SEL:
                if (!s) {
                    PAL_ERR(LOG_TAG, "Invalid stream");
                    goto free_sattr;
                }

                filled_selector_pairs.push_back(std::make_pair(selector_type,
                    s->getStreamSelector()));
                PAL_INFO(LOG_TAG, "ACD module type:%s", s->getStreamSelector().c_str());
                break;
            case DEVICEPP_TYPE_SEL:
                filled_selector_pairs.push_back(std::make_pair(selector_type,
                    s->getDevicePPSelector()));
                PAL_INFO(LOG_TAG, "devicePP_type:%s", s->getDevicePPSelector().c_str());
                break;
            case STREAM_TYPE_SEL:
                filled_selector_pairs.push_back(std::make_pair(selector_type,
                    streamNameLUT.at(sattr->type)));
                PAL_INFO(LOG_TAG, "stream type :%d", sattr->type);
                break;
            case AUD_FMT_SEL:
                if (isPalPCMFormat(sattr->out_media_config.aud_fmt_id)) {
                   filled_selector_pairs.push_back(std::make_pair(AUD_FMT_SEL,
                        "PAL_AUDIO_FMT_PCM"));
                } else {
                   filled_selector_pairs.push_back(std::make_pair(AUD_FMT_SEL,
                        "PAL_AUDIO_FMT_NON_PCM"));
                }
                PAL_INFO(LOG_TAG, "audio format :%d",
                     sattr->out_media_config.aud_fmt_id);
                break;
            case CUSTOM_CONFIG_SEL:
                status = s->getAssociatedDevices(associatedDevices);
                if (0 != status) {
                    PAL_ERR(LOG_TAG,"getAssociatedDevices failed");
                    goto free_sattr;
                }
                PAL_INFO(LOG_TAG,"associatedDevices.size = %zu",
                    associatedDevices.size());

                for (int i = 0; i < associatedDevices.size(); i++) {
                    status = associatedDevices[i]->getDeviceAttributes(&dAttr);
                    if (0 != status) {
                        PAL_ERR(LOG_TAG,"getAssociatedDevices failed");
                        goto free_sattr;
                    }
                    if (strlen(dAttr.custom_config.custom_key)) {
                        filled_selector_pairs.push_back(
                            std::make_pair(CUSTOM_CONFIG_SEL,
                            dAttr.custom_config.custom_key));
                        PAL_INFO(LOG_TAG, "custom config key: %s",
                            dAttr.custom_config.custom_key);
                    }
                }
                break;
            default:
                PAL_INFO(LOG_TAG, "No matching selector found");
        }
    }
free_sattr:
    delete sattr;
exit:
    return filled_selector_pairs;
}

void PayloadBuilder::removeDuplicateSelectors(std::vector<std::string> &gkv_selectors)
{
    auto end = gkv_selectors.end();
    for (auto i = gkv_selectors.begin(); i != end; ++i) {
        end = std::remove(i + 1, end, *i);
    }
    gkv_selectors.erase(end, gkv_selectors.end());
}

bool PayloadBuilder::isIdTypeAvailable(int32_t type, std::vector<int>& id_type)
{
    for (int32_t i = 0; i < id_type.size(); i++) {
       if (type == id_type[i]){
           PAL_DBG(LOG_TAG,"idtype :%d passed type :%d", id_type[i], type);
           return true;
       }
    }
    return false;
}

std::vector<std::string> PayloadBuilder::retrieveSelectors(int32_t type, std::vector<allKVs> any_type)
{
    std::vector<std::string> gkv_selectors;
    PAL_DBG(LOG_TAG, "Enter: size_of_all :%zu type:%d", any_type.size(), type);

    /* looping for all keys_and_values selectors and store in the gkv_selectors */
    for (int32_t i = 0; i < any_type.size(); i++) {
         if (isIdTypeAvailable(type, any_type[i].id_type)) {
             PAL_DBG(LOG_TAG, "KeysAndValues_size: %zu", any_type[i].keys_values.size());
             for(int32_t j = 0; j < any_type[i].keys_values.size(); j++) {
                 for(int32_t k = 0; k < any_type[i].keys_values[j].selector_names.size(); k++) {
                     gkv_selectors.push_back(any_type[i].keys_values[j].selector_names[k]);
                 }
             }
         }
    }

    if (gkv_selectors.size())
          removeDuplicateSelectors(gkv_selectors);

    for (int32_t i = 0; i < gkv_selectors.size(); i++) {
         PAL_DBG(LOG_TAG, "gkv_selectors: %s", gkv_selectors[i].c_str());
    }
    return gkv_selectors;
}

int PayloadBuilder::populateStreamKV(Stream* s,
        std::vector <std::pair<int,int>> &keyVector)
{
    int status = -EINVAL;
    struct pal_stream_attributes *sattr = NULL;
    std::vector <std::string> selectors;
    std::vector <std::pair<selector_type_t, std::string>> filled_selector_pairs;

    PAL_DBG(LOG_TAG, "enter");
    sattr = new struct pal_stream_attributes;
    if (!sattr) {
        status = -ENOMEM;
        PAL_ERR(LOG_TAG,"sattr malloc failed %s status %d", strerror(errno), status);
        goto exit;
    }
    memset(sattr, 0, sizeof(struct pal_stream_attributes));

    status = s->getStreamAttributes(sattr);
    if (0 != status) {
        PAL_ERR(LOG_TAG,"getStreamAttributes Failed status %d", status);
        goto free_sattr;
    }
    PAL_DBG(LOG_TAG, "stream attribute type %d", sattr->type);
    selectors = retrieveSelectors(sattr->type, all_streams);
    if (selectors.empty() != true)
        filled_selector_pairs = getSelectorValues(selectors, s);
    retrieveKVs(filled_selector_pairs ,sattr->type, all_streams, keyVector);

free_sattr:
    delete sattr;
exit:
    return status;
}

int PayloadBuilder::populateStreamDeviceKV(Stream* s __unused, int32_t beDevId __unused,
        std::vector <std::pair<int,int>> &keyVector __unused)
{
    int status = 0;

    PAL_VERBOSE(LOG_TAG,"enter");
    return status;
}

int PayloadBuilder::populateStreamDeviceKV(Stream* s, int32_t rxBeDevId,
        std::vector <std::pair<int,int>> &keyVectorRx, int32_t txBeDevId,
        std::vector <std::pair<int,int>> &keyVectorTx, struct vsid_info vsidinfo,
                                           sidetone_mode_t sidetoneMode)
{
    int status = 0;
    std::vector <std::pair<int, int>> emptyKV;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    PAL_VERBOSE(LOG_TAG,"enter");
    if (rm->isOutputDevId(rxBeDevId)) {
        status = populateStreamKV(s, keyVectorRx, emptyKV, vsidinfo);
        if (status)
            goto exit;
    }
    if (rm->isInputDevId(txBeDevId)) {
        status = populateStreamKV(s, emptyKV, keyVectorTx, vsidinfo);
        if (status)
            goto exit;
    }

    status = populateDeviceKV(s, rxBeDevId, keyVectorRx, txBeDevId,
            keyVectorTx, sidetoneMode);

exit:
    return status;
}

bool PayloadBuilder::isBtA2DPDevice(int32_t beDevId)
{
    switch (beDevId) {
        case PAL_DEVICE_OUT_BLUETOOTH_A2DP:
        case PAL_DEVICE_IN_BLUETOOTH_A2DP:
            return true;
        default:
            return false;
    }
}


int PayloadBuilder::populateDeviceKV(Stream* s, int32_t beDevId,
        std::vector <std::pair<int,int>> &keyVector)
{
    int status = 0;
    std::vector <std::string> selectors;
    std::vector <std::pair<selector_type_t, std::string>> filled_selector_pairs;

    PAL_DBG(LOG_TAG, "enter beDevId: %d", beDevId);

    /* For A2DP, device KV will be populated from Bluetooth device only, so skipping here */
    if (isBtA2DPDevice(beDevId))
        return status;
    selectors = retrieveSelectors(beDevId, all_devices);
    if (selectors.empty() != true)
        filled_selector_pairs = getSelectorValues(selectors, s);
    retrieveKVs(filled_selector_pairs, beDevId, all_devices, keyVector);
    return 0;
}

int PayloadBuilder::populateDeviceKV(Stream* s, int32_t rxBeDevId,
        std::vector <std::pair<int,int>> &keyVectorRx, int32_t txBeDevId,
        std::vector <std::pair<int,int>> &keyVectorTx, sidetone_mode_t sidetoneMode)
{
    int status = 0;
    struct pal_stream_attributes sAttr;
    std::vector <std::pair<selector_type_t, std::string>> filled_selector_pairs;

    PAL_DBG(LOG_TAG, "enter");

    status = s->getStreamAttributes(&sAttr);
    if (0 != status) {
        PAL_ERR(LOG_TAG,"getStreamAttributes Failed \n");
        return status;
    }

    populateDeviceKV(s, rxBeDevId, keyVectorRx);
    populateDeviceKV(s, txBeDevId, keyVectorTx);

    /* add sidetone kv if needed */
    if (sAttr.type == PAL_STREAM_VOICE_CALL && sidetoneMode == SIDETONE_SW) {
        PAL_DBG(LOG_TAG, "SW sidetone mode push kv");
        filled_selector_pairs.push_back(std::make_pair(SIDETONE_MODE_SEL, "SW"));
        retrieveKVs(filled_selector_pairs, txBeDevId, all_devices, keyVectorTx);
    }

    return status;
}

int PayloadBuilder::populateDevicePPKV(Stream* s, int32_t rxBeDevId,
        std::vector <std::pair<int,int>> &keyVectorRx, int32_t txBeDevId,
        std::vector <std::pair<int,int>> &keyVectorTx, std::vector<kvpair_info> kvpair __unused)
{
    int status = 0;
    struct pal_stream_attributes *sattr = NULL;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    struct pal_device dAttr;
    std::vector <std::string> selectors;
    std::vector <std::pair<selector_type_t, std::string>> filled_selector_pairs;

    PAL_DBG(LOG_TAG, "enter");
    sattr = new struct pal_stream_attributes;
    if (!sattr) {
        status = -ENOMEM;
        PAL_ERR(LOG_TAG, "sattr malloc failed %s status %d", strerror(errno), status);
        goto exit;
    }
    memset (&dAttr, 0, sizeof(struct pal_device));
    memset (sattr, 0, sizeof(struct pal_stream_attributes));

    status = s->getStreamAttributes(sattr);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getStreamAttributes failed status %d", status);
        goto free_sattr;
    }
    status = s->getAssociatedDevices(associatedDevices);
    if (0 != status) {
       PAL_ERR(LOG_TAG, "getAssociatedDevices failed");
       return status;
    }
    for (int i = 0; i < associatedDevices.size();i++) {
       status = associatedDevices[i]->getDeviceAttributes(&dAttr);
       if (0 != status) {
          PAL_ERR(LOG_TAG, "getAssociatedDevices failed");
          return status;
       }
       if ((dAttr.id == rxBeDevId) || (dAttr.id == txBeDevId)) {
          PAL_DBG(LOG_TAG, "channels %d, id %d",
              dAttr.config.ch_info.channels, dAttr.id);
       }
       filled_selector_pairs.clear();
       selectors.clear();

       selectors = retrieveSelectors(dAttr.id, all_devicepps);
       if (selectors.empty() != true)
           filled_selector_pairs = getSelectorValues(selectors, s);

       if (dAttr.id == rxBeDevId)
           retrieveKVs(filled_selector_pairs, dAttr.id, all_devicepps, keyVectorRx);
       if (dAttr.id == txBeDevId)
           retrieveKVs(filled_selector_pairs, dAttr.id, all_devicepps, keyVectorTx);
    }
free_sattr:
    delete sattr;
exit:
    return status;
}

int PayloadBuilder::populateStreamCkv(Stream *s,
        std::vector <std::pair<int,int>> &keyVector,
        int tag __unused,
        struct pal_volume_data **volume_data __unused)
{
    int status = 0;
    struct pal_stream_attributes sAttr;

    PAL_DBG(LOG_TAG, "Enter");
    memset(&sAttr, 0, sizeof(struct pal_stream_attributes));

    status = s->getStreamAttributes(&sAttr);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getStreamAttributes failed status %d", status);
        goto exit;
    }

    switch (sAttr.type) {
        case PAL_STREAM_VOICE_UI:
            PAL_INFO(LOG_TAG, "stream channels %d",
                sAttr.in_media_config.ch_info.channels);
            /* Push stream channels CKV for SVA/PDK module calibration */
            keyVector.push_back(std::make_pair(STREAM_CHANNELS,
                sAttr.in_media_config.ch_info.channels));
            break;
        default:
            /*
             * Sending volume minimum as we want to ramp up instead of ramping
             * down while setting the desired volume. Thus avoiding glitch
             * TODO: Decide what to send as ckv in graph open
             */
            keyVector.push_back(std::make_pair(VOLUME,LEVEL_15));
            PAL_DBG(LOG_TAG, "Entered default %x %x", VOLUME, LEVEL_15);
            break;
     }
exit:
    return status;
}

int PayloadBuilder::populateDevicePPCkv(Stream *s, std::vector <std::pair<int,int>> &keyVector)
{
    int status = 0;
    struct pal_stream_attributes *sattr = NULL;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    struct pal_device dAttr;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    PAL_DBG(LOG_TAG,"enter");
    sattr = new struct pal_stream_attributes;
    if (!sattr) {
        status = -ENOMEM;
        PAL_ERR(LOG_TAG,"sattr malloc failed %s status %d", strerror(errno), status);
        goto exit;
    }
    memset (&dAttr, 0, sizeof(struct pal_device));
    memset (sattr, 0, sizeof(struct pal_stream_attributes));

    status = s->getStreamAttributes(sattr);
    if (0 != status) {
        PAL_ERR(LOG_TAG,"getStreamAttributes Failed status %d\n",status);
        goto free_sattr;
    }
    status = s->getAssociatedDevices(associatedDevices);
    if (0 != status) {
       PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
       return status;
    }
    for (int i = 0; i < associatedDevices.size();i++) {
        status = associatedDevices[i]->getDeviceAttributes(&dAttr);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
            return status;
        }

        switch (sattr->type) {
            case PAL_STREAM_VOICE_UI:
                PAL_INFO(LOG_TAG,"channels %d, id %d\n",dAttr.config.ch_info.channels, dAttr.id);
                /* Push Channels CKV for FFNS or FFECNS channel based calibration */
                keyVector.push_back(std::make_pair(CHANNELS,
                                                   dAttr.config.ch_info.channels));
                break;
            case PAL_STREAM_ACD:
            case PAL_STREAM_SENSOR_PCM_DATA:
                PAL_DBG(LOG_TAG,"channels %d, id %d\n",dAttr.config.ch_info.channels, dAttr.id);
                /* Push Channels CKV for FFECNS channel based calibration */
                keyVector.push_back(std::make_pair(CHANNELS,
                                                   dAttr.config.ch_info.channels));
                break;
            case PAL_STREAM_LOW_LATENCY:
            case PAL_STREAM_DEEP_BUFFER:
            case PAL_STREAM_PCM_OFFLOAD:
            case PAL_STREAM_COMPRESSED:
                if (dAttr.id == PAL_DEVICE_OUT_SPEAKER) {
                    PAL_INFO(LOG_TAG,"SpeakerProt Status[%d], RAS Status[%d]\n",
                            rm->isSpeakerProtectionEnabled, rm->isRasEnabled);
                }
                if (rm->isSpeakerProtectionEnabled == true &&
                    rm->isRasEnabled == true &&
                    dAttr.id == PAL_DEVICE_OUT_SPEAKER) {
                    if (dAttr.config.ch_info.channels == 2) {
                        PAL_INFO(LOG_TAG,"Enabling RAS - device channels[%d]\n",
                                dAttr.config.ch_info.channels);
                        keyVector.push_back(std::make_pair(RAS_SWITCH, RAS_ON));
                    } else {
                        PAL_INFO(LOG_TAG,"Disabling RAS - device channels[%d] \n",
                                dAttr.config.ch_info.channels);
                        keyVector.push_back(std::make_pair(RAS_SWITCH, RAS_OFF));
                    }
                }

                if ((dAttr.id == PAL_DEVICE_OUT_SPEAKER) ||
                    (dAttr.id == PAL_DEVICE_OUT_WIRED_HEADSET) ||
                    (dAttr.id == PAL_DEVICE_OUT_WIRED_HEADPHONE)) {
                    PAL_DBG(LOG_TAG, "Entered default %x %x", GAIN, GAIN_0);
                    keyVector.push_back(std::make_pair(GAIN, GAIN_0));
                }

                /* TBD: Push Channels for these types once Channels are added */
                //keyVector.push_back(std::make_pair(CHANNELS,
                //                                   dAttr.config.ch_info.channels));
                break;
            default:
                PAL_VERBOSE(LOG_TAG,"stream type %d doesn't support DevicePP CKV ", sattr->type);
                goto free_sattr;
        }
    }
free_sattr:
    delete sattr;
exit:
    return status;
}

int PayloadBuilder::populateCalKeyVector(Stream *s, std::vector <std::pair<int,int>> &ckv, int tag) {
    int status = 0;
    PAL_VERBOSE(LOG_TAG,"enter \n");
    std::vector <std::pair<int,int>> keyVector;
    struct pal_stream_attributes sAttr;
    std::shared_ptr<CaptureProfile> cap_prof = nullptr;
    struct pal_device dAttr;
    int level = -1;
    std::vector<std::shared_ptr<Device>> associatedDevices;
    std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

    status = s->getStreamAttributes(&sAttr);
    if (0 != status) {
        PAL_ERR(LOG_TAG, "getStreamAttributes Failed");
        return status;
    }

    float voldB = 0.0f;
    struct pal_volume_data *voldata = NULL;
    voldata = (struct pal_volume_data *)calloc(1, (sizeof(uint32_t) +
                      (sizeof(struct pal_channel_vol_kv) * (0xFFFF))));
    if (!voldata) {
        status = -ENOMEM;
        goto exit;
    }

    status = s->getVolumeData(voldata);
    if (0 != status) {
        PAL_ERR(LOG_TAG,"getVolumeData Failed \n");
        goto error_1;
    }

    PAL_VERBOSE(LOG_TAG,"volume sent:%f \n",(voldata->volume_pair[0].vol));
    voldB = (voldata->volume_pair[0].vol);

    switch (static_cast<uint32_t>(tag)) {
    case TAG_STREAM_VOLUME:
        if (voldB == 0.0f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_15));
        }
        else if (voldB <= 0.002172f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_14));
        }
        else if (voldB <= 0.004660f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_13));
        }
        else if (voldB <= 0.01f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_12));
        }
        else if (voldB <= 0.014877f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_11));
        }
        else if (voldB <= 0.023646f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_10));
        }
        else if (voldB <= 0.037584f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_9));
        }
        else if (voldB <= 0.055912f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_8));
        }
        else if (voldB <= 0.088869f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_7));
        }
        else if (voldB <= 0.141254f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_6));
        }
        else if (voldB <= 0.189453f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_5));
        }
        else if (voldB <= 0.266840f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_4));
        }
        else if (voldB <= 0.375838f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_3));
        }
        else if (voldB <= 0.504081f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_2));
        }
        else if (voldB <= 0.709987f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_1));
        }
        else if (voldB <= 1.0f) {
            ckv.push_back(std::make_pair(VOLUME,LEVEL_0));
        }
        break;
    case TAG_DEVICE_PP_MBDRC:
        level = s->getGainLevel();
        if (level != -1)
            ckv.push_back(std::make_pair(GAIN, level));
        break;
    case SPKR_PROT_ENABLE :
        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
            return status;
        }

        for (int i = 0; i < associatedDevices.size(); i++) {
            status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"getAssociatedDevices Failed \n");
                return status;
            }
            if (dAttr.id == PAL_DEVICE_OUT_SPEAKER) {
                if (dAttr.config.ch_info.channels > 1) {
                    PAL_DBG(LOG_TAG, "Multi channel speaker");
                    ckv.push_back(std::make_pair(SPK_PRO_DEV_MAP, LEFT_RIGHT));
                }
                else {
                    PAL_DBG(LOG_TAG, "Mono channel speaker");
                    ckv.push_back(std::make_pair(SPK_PRO_DEV_MAP, RIGHT_MONO));
                }
                break;
            }
        }
        break;
    case SPKR_VI_ENABLE :
        status = s->getAssociatedDevices(associatedDevices);
        if (0 != status) {
            PAL_ERR(LOG_TAG,"%s: getAssociatedDevices Failed \n", __func__);
            return status;
        }

        for (int i = 0; i < associatedDevices.size(); i++) {
            status = associatedDevices[i]->getDeviceAttributes(&dAttr);
            if (0 != status) {
                PAL_ERR(LOG_TAG,"%s: getAssociatedDevices Failed \n", __func__);
                return status;
            }
            if (dAttr.id == PAL_DEVICE_IN_VI_FEEDBACK) {
                if (dAttr.config.ch_info.channels > 1) {
                    PAL_DBG(LOG_TAG, "Multi channel speaker");
                    ckv.push_back(std::make_pair(SPK_PRO_VI_MAP, STEREO_SPKR));
                }
                else {
                    PAL_DBG(LOG_TAG, "Mono channel speaker");
                    ckv.push_back(std::make_pair(SPK_PRO_VI_MAP, RIGHT_SPKR));
                }
                break;
            }
        }
    break;
    default:
        break;
    }

    PAL_VERBOSE(LOG_TAG,"exit status- %d", status);
error_1:
    free(voldata);
exit:
    return status;
}

int PayloadBuilder::populateTagKeyVector(Stream *s, std::vector <std::pair<int,int>> &tkv, int tag, uint32_t* gsltag)
{
    int status = 0;
    PAL_VERBOSE(LOG_TAG,"enter, tag 0x%x", tag);
    struct pal_stream_attributes sAttr;

    status = s->getStreamAttributes(&sAttr);

    if (status != 0) {
        PAL_ERR(LOG_TAG,"stream get attributes failed");
        return status;
    }

    switch (tag) {
    case MUTE_TAG:
       tkv.push_back(std::make_pair(MUTE,ON));
       *gsltag = TAG_MUTE;
       break;
    case UNMUTE_TAG:
       tkv.push_back(std::make_pair(MUTE,OFF));
       *gsltag = TAG_MUTE;
       break;
    case VOICE_SLOW_TALK_OFF:
       tkv.push_back(std::make_pair(TAG_KEY_SLOW_TALK, TAG_VALUE_SLOW_TALK_OFF));
       *gsltag = TAG_STREAM_SLOW_TALK;
       break;
    case VOICE_SLOW_TALK_ON:
       tkv.push_back(std::make_pair(TAG_KEY_SLOW_TALK, TAG_VALUE_SLOW_TALK_ON));
       *gsltag = TAG_STREAM_SLOW_TALK;
       break;
    case PAUSE_TAG:
       tkv.push_back(std::make_pair(PAUSE,ON));
       *gsltag = TAG_PAUSE;
       break;
    case RESUME_TAG:
       tkv.push_back(std::make_pair(PAUSE,OFF));
       *gsltag = TAG_PAUSE;
       break;
    case MFC_SR_8K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_8K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_16K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_16K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_32K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_32K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_44K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_44K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_48K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_48K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_96K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_96K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_192K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_192K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case MFC_SR_384K:
       tkv.push_back(std::make_pair(SAMPLINGRATE,SAMPLINGRATE_384K));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case ECNS_ON_TAG:
       tkv.push_back(std::make_pair(ECNS,ECNS_ON));
       *gsltag = TAG_ECNS;
       break;
    case ECNS_OFF_TAG:
       tkv.push_back(std::make_pair(ECNS,ECNS_OFF));
       *gsltag = TAG_ECNS;
       break;
    case EC_ON_TAG:
       tkv.push_back(std::make_pair(ECNS,EC_ON));
       *gsltag = TAG_ECNS;
       break;
    case NS_ON_TAG:
       tkv.push_back(std::make_pair(ECNS,NS_ON));
       *gsltag = TAG_ECNS;
       break;
    case CHS_1:
       tkv.push_back(std::make_pair(CHANNELS, CHANNELS_1));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case CHS_2:
       tkv.push_back(std::make_pair(CHANNELS, CHANNELS_2));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case CHS_3:
       tkv.push_back(std::make_pair(CHANNELS, CHANNELS_3));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case CHS_4:
       tkv.push_back(std::make_pair(CHANNELS, CHANNELS_4));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case BW_16:
       tkv.push_back(std::make_pair(BITWIDTH, BITWIDTH_16));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case BW_24:
       tkv.push_back(std::make_pair(BITWIDTH, BITWIDTH_24));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case BW_32:
       tkv.push_back(std::make_pair(BITWIDTH, BITWIDTH_32));
       if (sAttr.direction == PAL_AUDIO_INPUT)
            *gsltag = TAG_STREAM_MFC_SR;
       else
            *gsltag = TAG_DEVICE_MFC_SR;
       break;
    case INCALL_RECORD_UPLINK:
       tkv.push_back(std::make_pair(TAG_KEY_MUX_DEMUX_CONFIG, TAG_VALUE_MUX_DEMUX_CONFIG_UPLINK));
       *gsltag = TAG_STREAM_MUX_DEMUX;
       break;
    case INCALL_RECORD_DOWNLINK:
       tkv.push_back(std::make_pair(TAG_KEY_MUX_DEMUX_CONFIG, TAG_VALUE_MUX_DEMUX_CONFIG_DOWNLINK));
       *gsltag = TAG_STREAM_MUX_DEMUX;
       break;
    case INCALL_RECORD_UPLINK_DOWNLINK_MONO:
       tkv.push_back(std::make_pair(TAG_KEY_MUX_DEMUX_CONFIG, TAG_VALUE_MUX_DEMUX_CONFIG_UPLINK_DOWNLINK_MONO));
       *gsltag = TAG_STREAM_MUX_DEMUX;
       break;
    case INCALL_RECORD_UPLINK_DOWNLINK_STEREO:
       tkv.push_back(std::make_pair(TAG_KEY_MUX_DEMUX_CONFIG, TAG_VALUE_MUX_DEMUX_CONFIG_UPLINK_DOWNLINK_STEREO));
       *gsltag = TAG_STREAM_MUX_DEMUX;
       break;
    case LPI_LOGGING_ON:
       tkv.push_back(std::make_pair(LOGGING, LOGGING_ON));
       *gsltag = TAG_DATA_LOGGING;
       break;
    case LPI_LOGGING_OFF:
       tkv.push_back(std::make_pair(LOGGING, LOGGING_OFF));
       *gsltag = TAG_DATA_LOGGING;
   case DEVICE_MUTE:
       tkv.push_back(std::make_pair(MUTE,ON));
       *gsltag = TAG_DEV_MUTE;
       break;
    case DEVICE_UNMUTE:
       tkv.push_back(std::make_pair(MUTE,OFF));
       *gsltag = TAG_DEV_MUTE;
       break;
    default:
       PAL_ERR(LOG_TAG,"Tag not supported \n");
       break;
    }

    PAL_VERBOSE(LOG_TAG,"exit status- %d", status);
    return status;
}

void PayloadBuilder::payloadSPConfig(uint8_t** payload, size_t* size, uint32_t miid,
                int param_id, void *param)
{
    struct apm_module_param_data_t* header = NULL;
    uint8_t* payloadInfo = NULL;
    size_t payloadSize = 0, padBytes = 0;

    if (!param) {
        PAL_ERR(LOG_TAG, "Invalid input parameters");
        return;
    }


    switch(param_id) {
        case PARAM_ID_SP_TH_VI_R0T0_CFG :
            {
                param_id_sp_th_vi_r0t0_cfg_t *spConf;
                param_id_sp_th_vi_r0t0_cfg_t *data = NULL;
                vi_r0t0_cfg_t* r0t0 = NULL;
                data = (param_id_sp_th_vi_r0t0_cfg_t *) param;

                payloadSize = sizeof(struct apm_module_param_data_t) +
                              sizeof(param_id_sp_th_vi_r0t0_cfg_t) +
                              sizeof(vi_r0t0_cfg_t) * data->num_speakers;

                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);
                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;

                spConf = (param_id_sp_th_vi_r0t0_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));
                r0t0 = (vi_r0t0_cfg_t*) (payloadInfo +
                                sizeof(struct apm_module_param_data_t)
                                + sizeof(param_id_sp_th_vi_r0t0_cfg_t));

                spConf->num_speakers = data->num_speakers;
                for(int i = 0; i < data->num_speakers; i++) {
                    r0t0[i].r0_cali_q24 = data->vi_r0t0_cfg[i].r0_cali_q24;
                    r0t0[i].t0_cali_q6 = data->vi_r0t0_cfg[i].t0_cali_q6;
                }
            }
        break;
        case PARAM_ID_SP_VI_OP_MODE_CFG :
            {
                param_id_sp_vi_op_mode_cfg_t *spConf;
                param_id_sp_vi_op_mode_cfg_t *data;
                uint32_t *channelMap;

                data = (param_id_sp_vi_op_mode_cfg_t *) param;

                payloadSize = sizeof(struct apm_module_param_data_t) +
                              sizeof(param_id_sp_vi_op_mode_cfg_t) +
                              sizeof(uint32_t) * data->num_speakers;

                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);
                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;

                spConf = (param_id_sp_vi_op_mode_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));

                channelMap = (uint32_t *) (payloadInfo +
                                    sizeof(struct apm_module_param_data_t)
                                    + sizeof(param_id_sp_vi_op_mode_cfg_t));

                spConf->num_speakers = data->num_speakers;
                spConf->th_operation_mode = data->th_operation_mode;
                spConf->th_quick_calib_flag = data->th_quick_calib_flag;
                for(int i = 0; i < data->num_speakers; i++) {
                    if (spConf->th_operation_mode == 0) {
                        channelMap[i] = 0;
                    }
                    else if (spConf->th_operation_mode == 1) {
                        channelMap[i] = 0;
                    }
                }
            }
        break;
        case PARAM_ID_SP_VI_CHANNEL_MAP_CFG :
            {
                param_id_sp_vi_channel_map_cfg_t *spConf;
                param_id_sp_vi_channel_map_cfg_t *data;
                int32_t *channelMap;

                data = (param_id_sp_vi_channel_map_cfg_t *) param;

                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(param_id_sp_vi_channel_map_cfg_t) +
                                    (sizeof(int32_t) * data->num_ch);

                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;

                spConf = (param_id_sp_vi_channel_map_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));
                channelMap = (int32_t *) (payloadInfo +
                                    sizeof(struct apm_module_param_data_t)
                                    + sizeof(param_id_sp_vi_channel_map_cfg_t));

                spConf->num_ch = data->num_ch;
                for (int i = 0; i < data->num_ch; i++) {
                    channelMap[i] = i+1;
                }
            }
        break;
        case PARAM_ID_SP_OP_MODE :
            {
                param_id_sp_op_mode_t *spConf;
                param_id_sp_op_mode_t *data;

                data = (param_id_sp_op_mode_t *) param;
                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(param_id_sp_op_mode_t);

                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;

                spConf = (param_id_sp_op_mode_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));

                spConf->operation_mode = data->operation_mode;
            }
        break;
        case PARAM_ID_SP_EX_VI_MODE_CFG :
            {
                param_id_sp_ex_vi_mode_cfg_t *spConf;
                param_id_sp_ex_vi_mode_cfg_t *data;

                data = (param_id_sp_ex_vi_mode_cfg_t *) param;
                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(param_id_sp_ex_vi_mode_cfg_t);

                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;

                spConf = (param_id_sp_ex_vi_mode_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));

                spConf->operation_mode = data->operation_mode;
            }
        break;
        case PARAM_ID_SP_TH_VI_FTM_CFG:
        case PARAM_ID_SP_TH_VI_V_VALI_CFG :
            {
                param_id_sp_th_vi_ftm_cfg_t *spConf;
                param_id_sp_th_vi_ftm_cfg_t *data;
                vi_th_ftm_cfg_t *ftmCfg;
                std::shared_ptr<ResourceManager> rm = ResourceManager::getInstance();

                data = (param_id_sp_th_vi_ftm_cfg_t *) param;
                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(param_id_sp_th_vi_ftm_cfg_t) +
                                    sizeof(vi_th_ftm_cfg_t) * data->num_ch;

                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);
                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;
                spConf = (param_id_sp_th_vi_ftm_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));
                ftmCfg = (vi_th_ftm_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t)
                                + sizeof(param_id_sp_th_vi_ftm_cfg_t));

                spConf->num_ch = data->num_ch;
                for (int i = 0; i < data->num_ch; i++) {
                    ftmCfg[i].wait_time_ms =
                            rm->mSpkrProtModeValue.spkrHeatupTime;
                    ftmCfg[i].ftm_time_ms =
                            rm->mSpkrProtModeValue.operationModeRunTime;
                }
            }
        break;
        case PARAM_ID_SP_TH_VI_FTM_PARAMS:
            {
                param_id_sp_th_vi_ftm_params_t *data;
                data = (param_id_sp_th_vi_ftm_params_t *) param;
                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(param_id_sp_th_vi_ftm_params_t) +
                                    sizeof(vi_th_ftm_params_t) * data->num_ch;
                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);
                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;
            }
        break;
        case PARAM_ID_SP_TH_VI_V_VALI_PARAMS:
            {
                param_id_sp_th_vi_v_vali_params_t *data;
                data = (param_id_sp_th_vi_v_vali_params_t *) param;
                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(param_id_sp_th_vi_v_vali_params_t) +
                                    sizeof(vi_th_v_vali_params_t) * data->num_ch;
                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);
                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;
            }
        break;
        case PARAM_ID_CPS_LPASS_HW_INTF_CFG:
            {
                lpass_swr_hw_reg_cfg_t *data = NULL;
                lpass_swr_hw_reg_cfg_t *cfgPayload = NULL;
                param_id_cps_lpass_hw_intf_cfg_t *spConf = NULL;
                data = (lpass_swr_hw_reg_cfg_t *) param;
                payloadSize = sizeof(struct apm_module_param_data_t) +
                                    sizeof(lpass_swr_hw_reg_cfg_t) +
                                    sizeof(pkd_reg_addr_t) * data->num_spkr +
                                    sizeof(uint32_t);
                padBytes = PAL_PADDING_8BYTE_ALIGN(payloadSize);

                payloadInfo = (uint8_t*) calloc(1, payloadSize + padBytes);
                if (!payloadInfo) {
                    PAL_ERR(LOG_TAG, "payloadInfo malloc failed %s", strerror(errno));
                    return;
                }
                header = (struct apm_module_param_data_t*) payloadInfo;
                spConf = (param_id_cps_lpass_hw_intf_cfg_t *) (payloadInfo +
                                sizeof(struct apm_module_param_data_t));
                cfgPayload = (lpass_swr_hw_reg_cfg_t * ) (payloadInfo +
                                sizeof(struct apm_module_param_data_t) +
                                sizeof(uint32_t));
                spConf->lpass_hw_intf_cfg_mode = 1;

                memcpy(cfgPayload, data, sizeof(lpass_swr_hw_reg_cfg_t) +
                                sizeof(pkd_reg_addr_t) * data->num_spkr);
            }
        break;
        default:
            {
                PAL_ERR(LOG_TAG, "unknown param id 0x%x", param_id);
            }
        break;
    }

    if (header) {
        header->module_instance_id = miid;
        header->param_id = param_id;
        header->error_code = 0x0;
        header->param_size = payloadSize - sizeof(struct apm_module_param_data_t);
    }

    *size = payloadSize + padBytes;
    *payload = payloadInfo;
}
