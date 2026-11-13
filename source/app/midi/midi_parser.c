/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A stateful MIDI parser.  This should be repeatedly called for each MIDI data
   byte received and will spit out complete MIDI messages as they are parsed.

   MIDI messages will always be complete and valid, or NULL and errors.

   This implementation avoids recursive processing (which seems to be a common
   solution for some reason).

   Limitations:
   - Sysex messages are ignored (drained) since Frugi does not use them.

   - MIDI messages are not time-stamped. This makes MIDI slicing impossible 
     however I rarely see a need to use this given the audio block size only
     allows a few bytes to accumulate. I'm comfortable with repeated MIDI events 
     collapsing down into a single event - musically irrelevant to me.
   -----------------------------------------------------------------------------
*/
#include "midi_parser.h"

#define IS_STATUS_BYTE(__BYTE__) ((__BYTE__ >> 7) == 0x1)
#define IS_REAL_TIME(__BYTE__) ((__BYTE__ >> 3) == 0x1f)
#define IS_SINGLE_BYTE_MSG(__BYTE__) ((__BYTE__ >> 2) == 0x3D)
#define CHANNEL_MASK (0x0F)

static struct midi_msg rt_msg;
static struct midi_msg midi_msg;
static struct midi_msg *midi_parse_rt_msg(uint8_t byte);
static bool midi_parse_message(struct midi_inst *midi_in, uint8_t byte,
                               uint16_t now_us);

/*
 * This adds a byte to instance internal state and if this forms a valid
 * MIDI message returns it.  If the message is incomplete a NULL
 * is returned.
 *
 * A microsecond tick value is fed in which is used to detect
 * stalled sysex streams, but could also be used to timestamp messages in
 * future.  Unused for this purpose at present since MIDI slicing is not needed.
 */
struct midi_msg *midi_parse(struct midi_inst *midi_in, uint8_t byte,
                            uint16_t now_us) {
  struct midi_msg *msg = NULL;

  /*
   * This handles out of band, realtime messages.
   */
  msg = midi_parse_rt_msg(byte);

  if (msg == NULL) {
    if (midi_parse_message(midi_in, byte, now_us)) {
      msg = midi_in->msg;
      midi_in->msg = NULL;
    }
  }

  return msg;
}

static void midi_reset_msg(struct midi_msg *msg) {
  msg->len = 0;
  msg->data[0] = 0;
  msg->data[1] = 0;
  msg->data[2] = 0;
}

/*
 * Parses a real-time MIDI message.  These are single byte messages that can
 * arrive at any time.  They are not part of the MIDI stream and can interleave
 * bytes in other messages.  Calling this function does not intefere with the
 * state of any in-flight MIDI message.
 */
static struct midi_msg *midi_parse_rt_msg(uint8_t byte) {
  struct midi_msg *msg = NULL;

  if (IS_STATUS_BYTE(byte) && IS_REAL_TIME(byte)) {
    rt_msg.data[0] = byte;
    rt_msg.len = 1;
    msg = &rt_msg;
  }

  return msg;
}

/*
 * The main MIDI parser process.  Sysex messages are not supported
 * but are drained.  If the terminating F7 is not received, the drain is
 * abandoned once MIDI_SYSEX_TIMEOUT_US elapses.
 */
static bool midi_parse_message(struct midi_inst *midi_in, uint8_t byte,
                               uint16_t now_us) {
  struct midi_msg *msg = NULL;
  uint8_t running_status;

  if (midi_in->msg == NULL) {
    midi_in->msg = &midi_msg;
    midi_reset_msg(midi_in->msg);
  }

  running_status = midi_in->running_status;
  msg = midi_in->msg;

  if (midi_in->sysex_active) {
    if (byte == MIDI_STATUS_SYS_EX_END) {
      midi_in->sysex_active = false;
      return false;
    }

    /*
     * The MIDI spec ends a SysEx at EOX (F7) *or* at any other status byte.    
     */
    if (IS_STATUS_BYTE(byte)) {
      midi_in->sysex_active = false;
    } else if ((uint16_t)(now_us - midi_in->sysex_last_byte_tick) <
               MIDI_SYSEX_TIMEOUT_US) {
      midi_in->sysex_last_byte_tick = now_us;
      return false;
    } else {
      /*
       * Gap since the last SysEx byte exceeds the timeout - abandon the
       * drain and fall through to parse this byte normally.
       */
      midi_in->sysex_active = false;
    }
  }

  if (IS_STATUS_BYTE(byte)) {
    midi_in->running_status = byte;
    midi_in->third_byte_expected = false;

    if (byte == MIDI_STATUS_SYS_EX_START) {
      midi_in->sysex_active = true;
      midi_in->sysex_last_byte_tick = now_us;
      return false;
    }

    if (IS_SINGLE_BYTE_MSG(byte)) {
      midi_in->msg->data[0] = byte;
      midi_in->msg->len = 1;
      return true;
    }

    return false;
  }

  /*
   * The final byte of a 3-byte message.  Checked before the channel filter:
   * a channel message only gets here if its first data byte already passed
   * the filter, and Song Position has cleared running status by now, so its
   * last byte could not be filtered correctly anyway.
   */
  if (midi_in->third_byte_expected) {
    midi_in->third_byte_expected = false;
    msg->data[2] = byte;
    msg->len = 3;
    return true;
  }

  if (running_status == MIDI_STATUS_INVALID) {
    return false;
  }

  /*
   * Channel filter - channel messages (0x80-0xEF) only.  System messages
   * have no channel; the low nibble of their status byte is part of the
   * message type.
   */
  if (running_status < 0xF0 && midi_in->channel != MIDI_OMNI &&
      (running_status & CHANNEL_MASK) != (midi_in->channel - 1)) {
    return false;
  }

  uint8_t switch_status = (running_status < 0xF0)
                              ? (running_status & ~CHANNEL_MASK)
                              : running_status;

  switch (switch_status) {
  case MIDI_STATUS_NOTE_ON:
  case MIDI_STATUS_NOTE_OFF:
  case MIDI_STATUS_CONTROL_CHANGE:
  case MIDI_STATUS_PITCH_BEND:
  case MIDI_STATUS_POLY_PRESSURE:
    midi_in->third_byte_expected = true;
    msg->data[0] = running_status;
    msg->data[1] = byte;
    msg->len = 2;
    return false;

  case MIDI_STATUS_PROGRAM_CHANGE:
  case MIDI_STATUS_CHANNEL_PRESSURE:
    midi_in->third_byte_expected = false;
    msg->data[0] = running_status;
    msg->data[1] = byte;
    msg->len = 2;
    return true;

  case MIDI_STATUS_SONG_POS:
    midi_in->third_byte_expected = true;
    msg->data[0] = running_status;
    msg->data[1] = byte;
    msg->len = 2;
    midi_in->running_status = MIDI_STATUS_INVALID;
    return false;

  /* 2-byte system common messages - complete on their only data byte */
  case MIDI_STATUS_SONG_SELECT:
  case MIDI_STATUS_TIME_CODE:
    midi_in->third_byte_expected = false;
    msg->data[0] = running_status;
    msg->data[1] = byte;
    msg->len = 2;
    midi_in->running_status = MIDI_STATUS_INVALID;
    return true;
  }

  midi_in->running_status = MIDI_STATUS_INVALID;
  return false;
}
/*
 * LUT to convert a MIDI note number to a frequency (FCW)
 */
const float MIDI_FREQ_TABLE[128] = {
    8.1757993698120117f,     8.6619567871093750f,     9.1770238876342773f,
    9.7227182388305664f,     10.3008613586425780f,    10.9133825302124020f,
    11.5623254776000980f,    12.2498569488525390f,    12.9782714843750000f,
    13.7500000000000000f,    14.5676174163818360f,    15.4338531494140630f,
    16.3515987396240230f,    17.3239135742187500f,    18.3540477752685550f,
    19.4454364776611330f,    20.6017227172851560f,    21.8267650604248050f,
    23.1246509552001950f,    24.4997138977050780f,    25.9565429687500000f,
    27.5000000000000000f,    29.1352348327636720f,    30.8677062988281250f,
    32.7031974792480470f,    34.6478271484375000f,    36.7080955505371090f,
    38.8908729553222660f,    41.2034454345703130f,    43.6535301208496090f,
    46.2493019104003910f,    48.9994277954101560f,    51.9130859375000000f,
    55.0000000000000000f,    58.2704696655273440f,    61.7354125976562500f,
    65.4063949584960940f,    69.2956542968750000f,    73.4161911010742190f,
    77.7817459106445310f,    82.4068908691406250f,    87.3070602416992190f,
    92.4986038208007810f,    97.9988555908203130f,    103.8261718750000000f,
    110.0000000000000000f,   116.5409393310546900f,   123.4708251953125000f,
    130.8127899169921900f,   138.5913085937500000f,   146.8323822021484400f,
    155.5634918212890600f,   164.8137817382812500f,   174.6141204833984400f,
    184.9972076416015600f,   195.9977111816406200f,   207.6523437500000000f,
    220.0000000000000000f,   233.0818786621093700f,   246.9416503906250000f,
    261.6255798339843700f,   277.1826171875000000f,   293.6647644042968700f,
    311.1269836425781200f,   329.6275634765625000f,   349.2282409667968700f,
    369.9944152832031200f,   391.9954223632812500f,   415.3046875000000000f,
    440.0000000000000000f,   466.1637573242187500f,   493.8833007812500000f,
    523.2511596679687500f,   554.3652343750000000f,   587.3295288085937500f,
    622.2539672851562500f,   659.2551269531250000f,   698.4564819335937500f,
    739.9888305664062500f,   783.9908447265625000f,   830.6093750000000000f,
    880.0000000000000000f,   932.3275146484375000f,   987.7666015625000000f,
    1046.5023193359375000f,  1108.7304687500000000f,  1174.6590576171875000f,
    1244.5079345703125000f,  1318.5102539062500000f,  1396.9129638671875000f,
    1479.9776611328125000f,  1567.9816894531250000f,  1661.2187500000000000f,
    1760.0000000000000000f,  1864.6550292968750000f,  1975.5332031250000000f,
    2093.0046386718750000f,  2217.4609375000000000f,  2349.3181152343750000f,
    2489.0158691406250000f,  2637.0205078125000000f,  2793.8259277343750000f,
    2959.9553222656250000f,  3135.9633789062500000f,  3322.4375000000000000f,
    3520.0000000000000000f,  3729.3100585937500000f,  3951.0664062500000000f,
    4186.0092773437500000f,  4434.9218750000000000f,  4698.6362304687500000f,
    4978.0317382812500000f,  5274.0410156250000000f,  5587.6518554687500000f,
    5919.9106445312500000f,  6271.9267578125000000f,  6644.8750000000000000f,
    7040.0000000000000000f,  7458.6201171875000000f,  7902.1328125000000000f,
    8372.0185546875000000f,  8869.8437500000000000f,  9397.2724609375000000f,
    9956.0634765625000000f,  10548.0820312500000000f, 11175.3037109375000000f,
    11839.8212890625000000f, 12543.8535156250000000f};