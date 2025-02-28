#include "wled.h"

#define MAX_3_CH_LEDS_PER_UNIVERSE 170
#define MAX_4_CH_LEDS_PER_UNIVERSE 128
#define MAX_CHANNELS_PER_UNIVERSE 512

/*
 * E1.31 handler
 */

//DDP protocol support, called by handleE131Packet
//handles RGB data only
void handleDDPPacket(e131_packet_t* p) {
  int lastPushSeq = e131LastSequenceNumber[0];
  
  //reject late packets belonging to previous frame (assuming 4 packets max. before push)
  if (e131SkipOutOfSequence && lastPushSeq) {
    int sn = p->sequenceNum & 0xF;
    if (sn) {
      if (lastPushSeq > 5) {
        if (sn > (lastPushSeq -5) && sn < lastPushSeq) return;
      } else {
        if (sn > (10 + lastPushSeq) || sn < lastPushSeq) return;
      }
    }
  }

  uint8_t ddpChannelsPerLed = (p->dataType == DDP_TYPE_RGBW32) ? 4 : 3; // data type 0x1A is RGBW (type 3, 8 bit/channel)

  uint32_t start =  htonl(p->channelOffset) / ddpChannelsPerLed;
  start += DMXAddress / ddpChannelsPerLed;
  uint16_t stop = start + htons(p->dataLen) / ddpChannelsPerLed;
  uint8_t* data = p->data;
  uint16_t c = 0;
  if (p->flags & DDP_TIMECODE_FLAG) c = 4; //packet has timecode flag, we do not support it, but data starts 4 bytes later

  realtimeLock(realtimeTimeoutMs, REALTIME_MODE_DDP);
  
  if (!realtimeOverride || (realtimeMode && useMainSegmentOnly)) {
    for (uint16_t i = start; i < stop; i++) {
      setRealtimePixel(i, data[c], data[c+1], data[c+2], ddpChannelsPerLed >3 ? data[c+3] : 0);
      c += ddpChannelsPerLed;
    }
  }

  bool push = p->flags & DDP_PUSH_FLAG;
  if (push) {
    e131NewData = true;
    byte sn = p->sequenceNum & 0xF;
    if (sn) e131LastSequenceNumber[0] = sn;
  }
}

//E1.31 and Art-Net protocol support
void handleE131Packet(e131_packet_t* p, IPAddress clientIP, byte protocol){

  DEBUG_PRINTLN("receiving dmx packages! ");
  
  uint16_t uni = 0, dmxChannels = 0;
  uint8_t* e131_data = nullptr;
  uint8_t seq = 0, mde = REALTIME_MODE_E131;

  if (protocol == P_ARTNET)
  {
    if (p->art_opcode == ARTNET_OPCODE_OPPOLL) {
      handleArtnetPollReply(clientIP);
      return;
    }
    uni = p->art_universe;
    dmxChannels = htons(p->art_length);
    e131_data = p->art_data;
    seq = p->art_sequence_number;
    mde = REALTIME_MODE_ARTNET;
  } else if (protocol == P_E131) {
    uni = htons(p->universe);
    dmxChannels = htons(p->property_value_count) -1;
    e131_data = p->property_values;
    seq = p->sequence_number;
  } else { //DDP
    realtimeIP = clientIP;
    handleDDPPacket(p);
    return;
  }

  #ifdef WLED_ENABLE_DMX
  // does not act on out-of-order packets yet
  if (e131ProxyUniverse > 0 && uni == e131ProxyUniverse) {
    for (uint16_t i = 1; i <= dmxChannels; i++)
      dmx.write(i, e131_data[i]);
    dmx.update();
  }
  #endif

  // only listen for universes we're handling & allocated memory
  if (uni < e131Universe || uni >= (e131Universe + E131_MAX_UNIVERSE_COUNT)) return;

  uint8_t previousUniverses = uni - e131Universe;

  if (e131SkipOutOfSequence)
    if (seq < e131LastSequenceNumber[previousUniverses] && seq > 20 && e131LastSequenceNumber[previousUniverses] < 250){
      DEBUG_PRINT("skipping E1.31 frame (last seq=");
      DEBUG_PRINT(e131LastSequenceNumber[previousUniverses]);
      DEBUG_PRINT(", current seq=");
      DEBUG_PRINT(seq);
      DEBUG_PRINT(", universe=");
      DEBUG_PRINT(uni);
      DEBUG_PRINTLN(")");
      return;
    }
  e131LastSequenceNumber[previousUniverses] = seq;

  // update status info
  realtimeIP = clientIP;
  byte wChannel = 0;
  uint16_t totalLen = strip.getLengthTotal();
  uint16_t availDMXLen = 0;
  uint16_t dataOffset = DMXAddress;

  // For legacy DMX start address 0 the available DMX length offset is 0
  const uint16_t dmxLenOffset = (DMXAddress == 0) ? 0 : 1;

  // Check if DMX start address fits in available channels
  if (dmxChannels >= DMXAddress) {
    availDMXLen = (dmxChannels - DMXAddress) + dmxLenOffset;
  }

  // DMX data in Art-Net packet starts at index 0, for E1.31 at index 1
  if (protocol == P_ARTNET && dataOffset > 0) {
    dataOffset--;
  }

  uint16_t dmxPointer = dataOffset;

  switch (DMXMode) {
    case DMX_MODE_DISABLED:
      return;  // nothing to do
      break;

    case DMX_MODE_SINGLE_RGB: // RGB only
      if (uni != e131Universe) return;
      if (availDMXLen < 3) return;

      realtimeLock(realtimeTimeoutMs, mde);

      if (realtimeOverride && !(realtimeMode && useMainSegmentOnly)) return;

      wChannel = (availDMXLen > 3) ? e131_data[dataOffset+3] : 0;
      for (uint16_t i = 0; i < totalLen; i++)
        setRealtimePixel(i, e131_data[dataOffset+0], e131_data[dataOffset+1], e131_data[dataOffset+2], wChannel);
      break;
    
    case DMX_MODE_SINGLE_DRGB: // Dimmer + RGB
      if (uni != e131Universe) return;
      if (availDMXLen < 4) return;

      realtimeLock(realtimeTimeoutMs, mde);
      if (realtimeOverride && !(realtimeMode && useMainSegmentOnly)) return;
      wChannel = (availDMXLen > 4) ? e131_data[dataOffset+4] : 0;

      if (bri != e131_data[dataOffset+0]) {
        bri = e131_data[dataOffset+0];
        strip.setBrightness(bri, true);
      }

      for (uint16_t i = 0; i < totalLen; i++)
        setRealtimePixel(i, e131_data[dataOffset+1], e131_data[dataOffset+2], e131_data[dataOffset+3], wChannel);
      break;

    case DMX_MODE_EFFECT: // Length 1: Apply Preset ID, length 11-13: apply effect config
      if (uni != e131Universe) return;
      if (availDMXLen < 11) {
        if (availDMXLen > 1) return;
        applyPreset(e131_data[dataOffset+0], CALL_MODE_NOTIFICATION);
        return;
      }

      if (bri != e131_data[dataOffset+0]) {
        bri = e131_data[dataOffset+0];
      }
      if (e131_data[dataOffset+1] < strip.getModeCount())
      effectCurrent   = e131_data[dataOffset+ 1];
      effectSpeed     = e131_data[dataOffset+ 2];  // flickers
      effectIntensity = e131_data[dataOffset+ 3];
      effectPalette   = e131_data[dataOffset+ 4];
      col[0]          = e131_data[dataOffset+ 5];
      col[1]          = e131_data[dataOffset+ 6];
      col[2]          = e131_data[dataOffset+ 7];
      colSec[0]       = e131_data[dataOffset+ 8];
      colSec[1]       = e131_data[dataOffset+ 9];
      colSec[2]       = e131_data[dataOffset+10];
      if (availDMXLen > 11)
      {
        col[3]        = e131_data[dataOffset+11]; //white
        colSec[3]     = e131_data[dataOffset+12];
      }
      transitionDelayTemp = 0;               // act fast
      colorUpdated(CALL_MODE_NOTIFICATION);  // don't send UDP
      return;                                // don't activate realtime live mode
      break;

    case DMX_MODE_MULTIPLE_DRGB:
    case DMX_MODE_MULTIPLE_RGB:
    case DMX_MODE_MULTIPLE_RGBW:
      {
        bool is4Chan = (DMXMode == DMX_MODE_MULTIPLE_RGBW);
        const uint16_t dmxChannelsPerLed = is4Chan ? 4 : 3;
        const uint16_t ledsPerUniverse = is4Chan ? MAX_4_CH_LEDS_PER_UNIVERSE : MAX_3_CH_LEDS_PER_UNIVERSE;
        uint8_t stripBrightness = bri;
        uint16_t previousLeds, dmxOffset, ledsTotal;

        if (previousUniverses == 0) {
          if (availDMXLen < 1) return;
          dmxOffset = dataOffset;
          previousLeds = 0;
          // First DMX address is dimmer in DMX_MODE_MULTIPLE_DRGB mode.
          if (DMXMode == DMX_MODE_MULTIPLE_DRGB) {
            stripBrightness = e131_data[dmxOffset++];
            ledsTotal = (availDMXLen - 1) / dmxChannelsPerLed;
          } else {
            ledsTotal = availDMXLen / dmxChannelsPerLed;
          }
        } else {
          // All subsequent universes start at the first channel.
          dmxOffset = (protocol == P_ARTNET) ? 0 : 1;
          const uint16_t dimmerOffset = (DMXMode == DMX_MODE_MULTIPLE_DRGB) ? 1 : 0;
          uint16_t ledsInFirstUniverse = (((MAX_CHANNELS_PER_UNIVERSE - DMXAddress) + dmxLenOffset) - dimmerOffset) / dmxChannelsPerLed;
          previousLeds = ledsInFirstUniverse + (previousUniverses - 1) * ledsPerUniverse;
          ledsTotal = previousLeds + (dmxChannels / dmxChannelsPerLed);
        }

        // All LEDs already have values
        if (previousLeds >= totalLen) {
          return;
        }

        realtimeLock(realtimeTimeoutMs, mde);
        if (realtimeOverride && !(realtimeMode && useMainSegmentOnly)) return;

        if (ledsTotal > totalLen) {
          ledsTotal = totalLen;
        }

        if (DMXMode == DMX_MODE_MULTIPLE_DRGB && previousUniverses == 0) {
          if (bri != stripBrightness) {
            bri = stripBrightness;
            strip.setBrightness(bri, true);
          }
        }
        
        if (!is4Chan) {
          for (uint16_t i = previousLeds; i < ledsTotal; i++) {
            setRealtimePixel(i, e131_data[dmxOffset], e131_data[dmxOffset+1], e131_data[dmxOffset+2], 0);
            dmxOffset+=3;
          }
        } else {
          for (uint16_t i = previousLeds; i < ledsTotal; i++) {
            setRealtimePixel(i, e131_data[dmxOffset], e131_data[dmxOffset+1], e131_data[dmxOffset+2], e131_data[dmxOffset+3]);
            dmxOffset+=4;
          }
        }
        break;
      }
    
    case DMX_MODE_SEGMENTS_RGBW:
    case DMX_MODE_SEGMENTS_DRGBW:
    case DMX_MODE_SEGMENTS_RGB:
    case DMX_MODE_SEGMENTS_DRGB:   

      if (uni != e131Universe) return;  
            
      realtimeLock(realtimeTimeoutMs, mde);
      if (realtimeOverride && !(realtimeMode && useMainSegmentOnly)) return;

      if ((DMXMode == DMX_MODE_SEGMENTS_DRGB) |  (DMXMode == DMX_MODE_SEGMENTS_DRGBW)){
          if (bri != e131_data[dataOffset+0]) {
            bri = e131_data[dataOffset+0];
            strip.setBrightness(bri, true);
          }
          dmxPointer++;
      }
      for(segment & seg : strip._segments){
        if(seg.isActive()){   
          seg.setOption(SEG_OPTION_SELECTED,true);  
          if ((DMXMode == DMX_MODE_SEGMENTS_RGBW) |  (DMXMode == DMX_MODE_SEGMENTS_DRGBW)){
            seg.fill(RGBW32(e131_data[dmxPointer+0], e131_data[dmxPointer+1], e131_data[dmxPointer+2],0));
          }else{
            seg.fill(RGBW32(e131_data[dmxPointer+0], e131_data[dmxPointer+1], e131_data[dmxPointer+2],e131_data[dmxPointer+3]));
          }
          seg.setOption(SEG_OPTION_SELECTED,false); 
        }
        if ((DMXMode == DMX_MODE_SEGMENTS_RGBW) |  (DMXMode == DMX_MODE_SEGMENTS_DRGBW)){
          dmxPointer += 4; 
        }else{
          dmxPointer += 3; 
        }
      }
      break;
    
    case DMX_MODE_SEGMENTS_EFFECT:  //TODO maybe by mx
      DEBUG_PRINTLN(F("DMX_MODE_SEGMENTS_EFFECT TODO"));
    default:
      DEBUG_PRINTLN(F("unknown E1.31 DMX mode"));
      return;  // nothing to do
      break;
  }

  e131NewData = true;
}

void handleArtnetPollReply(IPAddress ipAddress) {
  ArtPollReply artnetPollReply;
  prepareArtnetPollReply(&artnetPollReply);

  uint16_t startUniverse = e131Universe;
  uint16_t endUniverse = e131Universe;

  switch (DMXMode) {
    case DMX_MODE_DISABLED:
      return;  // nothing to do
      break;

    case DMX_MODE_SINGLE_RGB:
    case DMX_MODE_SINGLE_DRGB:
    case DMX_MODE_EFFECT:
      break;  // 1 universe is enough

    case DMX_MODE_MULTIPLE_DRGB:
    case DMX_MODE_MULTIPLE_RGB:
    case DMX_MODE_MULTIPLE_RGBW:
      {
        bool is4Chan = (DMXMode == DMX_MODE_MULTIPLE_RGBW);
        const uint16_t dmxChannelsPerLed = is4Chan ? 4 : 3;
        const uint16_t dimmerOffset = (DMXMode == DMX_MODE_MULTIPLE_DRGB) ? 1 : 0;
        const uint16_t dmxLenOffset = (DMXAddress == 0) ? 0 : 1; // For legacy DMX start address 0
        const uint16_t ledsInFirstUniverse = (((MAX_CHANNELS_PER_UNIVERSE - DMXAddress) + dmxLenOffset) - dimmerOffset) / dmxChannelsPerLed;
        const uint16_t totalLen = strip.getLengthTotal();

        if (totalLen > ledsInFirstUniverse) {
          const uint16_t ledsPerUniverse = is4Chan ? MAX_4_CH_LEDS_PER_UNIVERSE : MAX_3_CH_LEDS_PER_UNIVERSE;
          const uint16_t remainLED = totalLen - ledsInFirstUniverse;

          endUniverse += (remainLED / ledsPerUniverse);

          if ((remainLED % ledsPerUniverse) > 0) {
            endUniverse++;
          }

          if ((endUniverse - startUniverse) > E131_MAX_UNIVERSE_COUNT) {
            endUniverse = startUniverse + E131_MAX_UNIVERSE_COUNT - 1;
          }
        }
        break;
      }
    default:
      DEBUG_PRINTLN(F("unknown E1.31 DMX mode"));
      return;  // nothing to do
      break;
  }

  for (uint16_t i = startUniverse; i <= endUniverse; ++i) {
    sendArtnetPollReply(&artnetPollReply, ipAddress, i);
  }
}

void prepareArtnetPollReply(ArtPollReply *reply) {
  // Art-Net
  reply->reply_id[0] = 0x41;
  reply->reply_id[1] = 0x72;
  reply->reply_id[2] = 0x74;
  reply->reply_id[3] = 0x2d;
  reply->reply_id[4] = 0x4e;
  reply->reply_id[5] = 0x65;
  reply->reply_id[6] = 0x74;
  reply->reply_id[7] = 0x00;

  reply->reply_opcode = ARTNET_OPCODE_OPPOLLREPLY;

  IPAddress localIP = Network.localIP();
  for (uint8_t i = 0; i < 4; i++) {
    reply->reply_ip[i] = localIP[i];
  }

  reply->reply_port = ARTNET_DEFAULT_PORT;

  char * numberEnd = versionString;
  reply->reply_version_h = (uint8_t)strtol(numberEnd, &numberEnd, 10);
  numberEnd++;
  reply->reply_version_l = (uint8_t)strtol(numberEnd, &numberEnd, 10);

  // Switch values depend on universe, set before sending
  reply->reply_net_sw = 0x00;
  reply->reply_sub_sw = 0x00;

  reply->reply_oem_h = 0x00; // TODO add assigned oem code
  reply->reply_oem_l = 0x00;

  reply->reply_ubea_ver = 0x00;

  // Indicators in Normal Mode
  // All or part of Port-Address programmed by network or Web browser
  reply->reply_status_1 = 0xE0;

  reply->reply_esta_man = 0x0000;

  strlcpy((char *)(reply->reply_short_name), serverDescription, 18);
  strlcpy((char *)(reply->reply_long_name), serverDescription, 64);

  reply->reply_node_report[0] = '\0';

  reply->reply_num_ports_h = 0x00;
  reply->reply_num_ports_l = 0x01; // One output port

  reply->reply_port_types[0] = 0x80; // Output DMX data
  reply->reply_port_types[1] = 0x00;
  reply->reply_port_types[2] = 0x00;
  reply->reply_port_types[3] = 0x00;

  // No inputs
  reply->reply_good_input[0] = 0x00;
  reply->reply_good_input[1] = 0x00;
  reply->reply_good_input[2] = 0x00;
  reply->reply_good_input[3] = 0x00;

  // One output
  reply->reply_good_output_a[0] = 0x80; // Data is being transmitted
  reply->reply_good_output_a[1] = 0x00;
  reply->reply_good_output_a[2] = 0x00;
  reply->reply_good_output_a[3] = 0x00;

  // Values depend on universe, set before sending
  reply->reply_sw_in[0] = 0x00;
  reply->reply_sw_in[1] = 0x00;
  reply->reply_sw_in[2] = 0x00;
  reply->reply_sw_in[3] = 0x00;

  // Values depend on universe, set before sending
  reply->reply_sw_out[0] = 0x00;
  reply->reply_sw_out[1] = 0x00;
  reply->reply_sw_out[2] = 0x00;
  reply->reply_sw_out[3] = 0x00;

  reply->reply_sw_video = 0x00;
  reply->reply_sw_macro = 0x00;
  reply->reply_sw_remote = 0x00;

  reply->reply_spare[0] = 0x00;
  reply->reply_spare[1] = 0x00;
  reply->reply_spare[2] = 0x00;

  // A DMX to / from Art-Net device
  reply->reply_style = 0x00;

  Network.localMAC(reply->reply_mac);

  for (uint8_t i = 0; i < 4; i++) {
    reply->reply_bind_ip[i] = localIP[i];
  }

  reply->reply_bind_index = 1;

  // Product supports web browser configuration
  // Node’s IP is DHCP or manually configured
  // Node is DHCP capable
  // Node supports 15 bit Port-Address (Art-Net 3 or 4)
  // Node is able to switch between ArtNet and sACN
  reply->reply_status_2 = (staticIP[0] == 0) ? 0x1F : 0x1D;

  // RDM is disabled
  // Output style is continuous
  reply->reply_good_output_b[0] = 0xC0;
  reply->reply_good_output_b[1] = 0xC0;
  reply->reply_good_output_b[2] = 0xC0;
  reply->reply_good_output_b[3] = 0xC0;

  // Fail-over state: Hold last state
  // Node does not support fail-over
  reply->reply_status_3 = 0x00;

  for (uint8_t i = 0; i < 21; i++) {
    reply->reply_filler[i] = 0x00;
  }
}

void sendArtnetPollReply(ArtPollReply *reply, IPAddress ipAddress, uint16_t portAddress) {
  reply->reply_net_sw = (uint8_t)((portAddress >> 8) & 0x007F);
  reply->reply_sub_sw = (uint8_t)((portAddress >> 4) & 0x000F);
  reply->reply_sw_out[0] = (uint8_t)(portAddress & 0x000F);

  sprintf((char *)reply->reply_node_report, "#0001 [%04u] OK - WLED v" TOSTRING(WLED_VERSION), pollReplyCount);
  
  if (pollReplyCount < 9999) {
    pollReplyCount++;
  } else {
    pollReplyCount = 0;
  }

  notifierUdp.beginPacket(ipAddress, ARTNET_DEFAULT_PORT);
  notifierUdp.write(reply->raw, sizeof(ArtPollReply));
  notifierUdp.endPacket();

  reply->reply_bind_index++;
}