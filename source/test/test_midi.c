/*
   MIT License
   Copyright (c) 2026 YDigiKat

   Permission to use, copy, modify, and/or distribute this code for any purpose
   with or without fee is hereby granted, provided the above copyright notice
   and this permission notice appear in all copies.
   -----------------------------------------------------------------------------
   A.I. Disclosure:  These unit tests written by Anthropic Claude.
   -----------------------------------------------------------------------------
*/

#include "midi_parser.h"
#include "unity.h"

static struct midi_inst inst;
static uint16_t now;

/*
 * Wraps midi_parse() with an auto-advancing tick, standing in for the
 * caller-supplied microsecond timestamp.  Each byte is far closer together
 * than MIDI_SYSEX_TIMEOUT_US so this has no effect on non-timeout tests -
 * tests that exercise the SysEx timeout call midi_parse() directly so they
 * can control the tick precisely.
 */
static struct midi_msg *parse_byte(uint8_t byte) {
  now += 10;
  return midi_parse(&inst, byte, now);
}

/*
 * Feeds a stream of bytes to the midi parser returning the last
 * msg parsed or NULL if no messages found.
 */
static struct midi_msg *feed(const uint8_t *bytes, size_t n) {
  struct midi_msg *msg = NULL;
  for (size_t i = 0; i < n; i++) {
    struct midi_msg *m = parse_byte(bytes[i]);
    if (m)
      msg = m;
  }

  return msg;
}

#define FEED(...)                                                              \
  ({                                                                           \
    uint8_t _bytes[] = {__VA_ARGS__};                                          \
    feed(_bytes, sizeof(_bytes));                                              \
  })

void setUp() {
  inst = (struct midi_inst){.channel = MIDI_OMNI};
  now = 0;
}

void tearDown() {}

/* -------------------------------------------------------------------------
 * 3-byte channel tests
 * ---------------------------------------------------------------------- */

void note_on_returns_msg_at_third_byte(void) {
  TEST_ASSERT_NULL(parse_byte(0x90));      /* Status */
  TEST_ASSERT_NULL(parse_byte(0x3C));      /* note */
  struct midi_msg *msg = parse_byte(0x7F); /* velocity */
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x3C, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, msg->data[2]);
  TEST_ASSERT_EQUAL_size_t(3, msg->len);
}

void note_off_parsed_correctly(void) {
  struct midi_msg *msg = FEED(0x80, 0x3C, 0x40);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_OFF, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x3C, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x40, msg->data[2]);
}

void control_change_parsed_correctly(void) {
  struct midi_msg *msg = FEED(0xB0, MIDI_CC_VOLUME, 100);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_CONTROL_CHANGE, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(MIDI_CC_VOLUME, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(100, msg->data[2]);
}

void pitch_bend_parsed_correctly(void) {
  struct midi_msg *msg = FEED(0xE0, 0x00, 0x40); /* centre position */
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_PITCH_BEND, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x40, msg->data[2]);
  TEST_ASSERT_EQUAL_size_t(3, msg->len);
}

void poly_pressure_parsed_correctly(void) {
  struct midi_msg *msg = FEED(0xA0, 0x3C, 0x60);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_POLY_PRESSURE, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x3C, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x60, msg->data[2]);
}

/* -------------------------------------------------------------------------
 * 2-byte channel tests
 * ---------------------------------------------------------------------- */

void program_change_returns_on_second_byte(void) {
  TEST_ASSERT_NULL(parse_byte(0xC0));      /* status — no msg yet   */
  struct midi_msg *msg = parse_byte(0x05); /* program — complete    */
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_PROGRAM_CHANGE, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x05, msg->data[1]);
  TEST_ASSERT_EQUAL_size_t(2, msg->len);
}

void channel_pressure_returns_on_second_byte(void) {
  struct midi_msg *msg = FEED(0xD0, 0x7F);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_CHANNEL_PRESSURE, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, msg->data[1]);
  TEST_ASSERT_EQUAL_size_t(2, msg->len);
}

/* -------------------------------------------------------------------------
 * Running status tests
 * ---------------------------------------------------------------------- */
void running_status_second_note_on(void) {
  FEED(0x90, 0x3C, 0x7F);                  /* first note — consume  */
  struct midi_msg *msg = FEED(0x48, 0x64); /* second note, no status*/
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x48, msg->data[1]); /* E4                    */
  TEST_ASSERT_EQUAL_UINT8(0x64, msg->data[2]); /* velocity 100          */
}

void running_status_multiple_cc(void) {
  FEED(0xB0, MIDI_CC_VOLUME, 100);
  struct midi_msg *msg = FEED(MIDI_CC_PAN, 64);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_CONTROL_CHANGE, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(MIDI_CC_PAN, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(64, msg->data[2]);
}

void running_status_cleared_by_new_status(void) {
  FEED(0x90, 0x3C, 0x7F);             /* Note On running status */
  FEED(0x80);                         /* New status: Note Off  */
  TEST_ASSERT_NULL(parse_byte(0x48)); /* first data byte only  */
}

/* -------------------------------------------------------------------------
 * Intermidate state tests
 * ---------------------------------------------------------------------- */
void no_message_after_status_only(void) { TEST_ASSERT_NULL(parse_byte(0x90)); }

void no_message_after_first_data_byte(void) {
  parse_byte(0x90);
  TEST_ASSERT_NULL(parse_byte(0x3C));
}

/* -------------------------------------------------------------------------
 * Sysex draining
 * ---------------------------------------------------------------------- */
void sysex_bytes_are_swallowed(void) {
  /* SysEx start + arbitrary manufacturer data — nothing returned */
  TEST_ASSERT_NULL(parse_byte(MIDI_STATUS_SYS_EX_START));
  TEST_ASSERT_NULL(parse_byte(0x41)); /* Roland ID               */
  TEST_ASSERT_NULL(parse_byte(0x10));
  TEST_ASSERT_NULL(parse_byte(0x42));
  TEST_ASSERT_NULL(parse_byte(MIDI_STATUS_SYS_EX_END));
}

void message_received_after_sysex_ends(void) {
  FEED(MIDI_STATUS_SYS_EX_START, 0x41, 0x10, MIDI_STATUS_SYS_EX_END);
  struct midi_msg *msg = FEED(0x90, 0x3C, 0x7F);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0]);
}

/*
 * MIDI 1.0: a SysEx ends at EOX (F7) or at any other status byte except
 * realtime.  A Note On arriving without a preceding F7 ends the SysEx and is
 * parsed as a normal message.
 */
void note_on_status_ends_an_unterminated_sysex(void) {
  FEED(MIDI_STATUS_SYS_EX_START, 0x7E, 0x01); /* no F7 */
  struct midi_msg *msg = FEED(0x90, 0x3C, 0x64);

  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(0x90, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x3C, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x64, msg->data[2]);
}

/* ...and running status from that Note On carries on working afterwards */
void running_status_works_after_an_unterminated_sysex(void) {
  FEED(MIDI_STATUS_SYS_EX_START, 0x7E, 0x01);
  FEED(0x90, 0x3C, 0x64);
  struct midi_msg *msg = FEED(0x3E, 0x64); /* second note, no status */

  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(0x90, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x3E, msg->data[1]);
}

/* A system common status also ends the SysEx */
void system_common_status_ends_an_unterminated_sysex(void) {
  FEED(MIDI_STATUS_SYS_EX_START, 0x41, 0x10);
  struct midi_msg *msg = parse_byte(MIDI_STATUS_TUNE_REQUEST);

  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_TUNE_REQUEST, msg->data[0]);
}

/* Realtime bytes pass through mid-SysEx and do NOT end it */
void realtime_byte_inside_sysex_is_returned_and_sysex_continues(void) {
  parse_byte(MIDI_STATUS_SYS_EX_START);
  parse_byte(0x41);

  struct midi_msg *rt = parse_byte(MIDI_STATUS_CLOCK);
  TEST_ASSERT_NOT_NULL(rt);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_CLOCK, rt->data[0]);

  /* Still draining: a data byte after the clock is not a message */
  TEST_ASSERT_NULL(parse_byte(0x10));
  TEST_ASSERT_NULL(parse_byte(MIDI_STATUS_SYS_EX_END));
}

/* SysEx cancels running status: bare data bytes after it are ignored */
void sysex_cancels_running_status(void) {
  FEED(0x90, 0x3C, 0x64); /* establishes Note On running status */
  FEED(MIDI_STATUS_SYS_EX_START, 0x41, MIDI_STATUS_SYS_EX_END);

  TEST_ASSERT_NULL(parse_byte(0x3E));
  TEST_ASSERT_NULL(parse_byte(0x64));
}

void sysex_survives_gaps_under_the_timeout(void) {
  /* Bytes trickling in, each just under the timeout apart, should not
     trip the recovery - this is still one (slow but valid) SysEx drain. */
  uint16_t t = 0;
  midi_parse(&inst, MIDI_STATUS_SYS_EX_START, t);
  for (int i = 0; i < 10; i++) {
    t += (MIDI_SYSEX_TIMEOUT_US - 1);
    TEST_ASSERT_NULL(midi_parse(&inst, 0x41, t));
  }
  TEST_ASSERT_NULL(
      midi_parse(&inst, MIDI_STATUS_SYS_EX_END, (uint16_t)(t + 1)));
}

void sysex_recovers_after_inter_byte_timeout_without_end_flag(void) {
  /* No terminating F7 ever arrives.  Once the gap since the last SysEx
     byte exceeds MIDI_SYSEX_TIMEOUT_US, the drain must be abandoned so
     the next status byte is parsed as a normal message. */
  midi_parse(&inst, MIDI_STATUS_SYS_EX_START, 0);
  midi_parse(&inst, 0x41, 100);

  uint16_t stalled = (uint16_t)(100 + MIDI_SYSEX_TIMEOUT_US + 1);
  TEST_ASSERT_NULL(midi_parse(&inst, 0x90, stalled)); /* status - no msg yet */
  TEST_ASSERT_NULL(midi_parse(&inst, 0x3C, (uint16_t)(stalled + 1)));
  struct midi_msg *msg = midi_parse(&inst, 0x7F, (uint16_t)(stalled + 2));
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0]);
}

/* -------------------------------------------------------------------------
 * Realtime Messages
 * ---------------------------------------------------------------------- */
void clock_returned_mid_message(void) {
  parse_byte(0x90);
  parse_byte(0x3C);
  /* Clock arrives before velocity byte */
  struct midi_msg *rt = parse_byte(MIDI_STATUS_CLOCK);
  TEST_ASSERT_NOT_NULL(rt);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_CLOCK, rt->data[0]);
  TEST_ASSERT_EQUAL_size_t(1, rt->len);
}

void note_completes_after_interleaved_clock(void) {
  parse_byte(0x90);
  parse_byte(0x3C);
  parse_byte(MIDI_STATUS_CLOCK);           /* interleaved RT        */
  struct midi_msg *msg = parse_byte(0x7F); /* velocity — completes  */
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x7F, msg->data[2]);
}

void active_sense_is_single_byte(void) {
  struct midi_msg *msg = parse_byte(MIDI_STATUS_ACTIVE_SENSE);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_ACTIVE_SENSE, msg->data[0]);
  TEST_ASSERT_EQUAL_size_t(1, msg->len);
}

void start_stop_continue_are_single_byte(void) {
  struct midi_msg *msg;

  msg = parse_byte(MIDI_STATUS_START);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_START, msg->data[0]);

  msg = parse_byte(MIDI_STATUS_STOP);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_STOP, msg->data[0]);

  msg = parse_byte(MIDI_STATUS_CONTINUE);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_CONTINUE, msg->data[0]);
}

/* -------------------------------------------------------------------------
 * System tests
 * ---------------------------------------------------------------------- */
void song_position_pointer(void) {
  /* Song Pos is 3 bytes but clears running status afterwards */
  struct midi_msg *msg = FEED(MIDI_STATUS_SONG_POS, 0x00, 0x00);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_SONG_POS, msg->data[0]);
  TEST_ASSERT_EQUAL_size_t(3, msg->len);
}

void song_select(void) {
  struct midi_msg *msg = FEED(MIDI_STATUS_SONG_SELECT, 0x03);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_SONG_SELECT, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03, msg->data[1]);
  TEST_ASSERT_EQUAL_size_t(2, msg->len);
}

/*
 * Song Select and MTC Quarter Frame are 2-byte messages.  A stray data byte
 * after one must not be taken as a third byte and emitted as a bogus
 * 3-byte message (status 0x00).
 */
void song_select_does_not_take_a_stray_byte_as_a_third_byte(void) {
  FEED(MIDI_STATUS_SONG_SELECT, 0x03);
  TEST_ASSERT_NULL(parse_byte(0x40));
}

void time_code_is_a_two_byte_message(void) {
  struct midi_msg *msg = FEED(MIDI_STATUS_TIME_CODE, 0x21);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_TIME_CODE, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x21, msg->data[1]);
  TEST_ASSERT_EQUAL_size_t(2, msg->len);

  TEST_ASSERT_NULL(parse_byte(0x40)); /* stray data byte */
}

/* System common messages cancel running status (MIDI 1.0) */
void system_common_cancels_running_status(void) {
  FEED(0x90, 0x3C, 0x64);              /* Note On running status */
  FEED(MIDI_STATUS_SONG_SELECT, 0x03); /* cancels it */

  TEST_ASSERT_NULL(parse_byte(0x3E));
  TEST_ASSERT_NULL(parse_byte(0x64));
}

/* ...and normal messages parse after them */
void note_on_after_song_select_parses(void) {
  FEED(MIDI_STATUS_SONG_SELECT, 0x03);
  struct midi_msg *msg = FEED(0x90, 0x3C, 0x64);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(0x90, msg->data[0]);
  TEST_ASSERT_EQUAL_size_t(3, msg->len);
}

/*
 * System messages have no channel, so the channel filter must not apply to
 * them.  (Previously the low nibble of the status - e.g. 2 for F2 - was
 * compared with the listening channel.)
 */
void song_position_passes_the_channel_filter(void) {
  inst.channel = 5;
  struct midi_msg *msg = FEED(MIDI_STATUS_SONG_POS, 0x10, 0x20);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_SONG_POS, msg->data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x10, msg->data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x20, msg->data[2]);
  TEST_ASSERT_EQUAL_size_t(3, msg->len);
}

void song_select_passes_the_channel_filter(void) {
  inst.channel = 5;
  struct midi_msg *msg = FEED(MIDI_STATUS_SONG_SELECT, 0x03);
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_SONG_SELECT, msg->data[0]);
}

/* The filter still rejects channel messages on other channels afterwards */
void channel_filter_still_applies_after_a_system_message(void) {
  inst.channel = 1;
  FEED(MIDI_STATUS_SONG_POS, 0x10, 0x20);
  TEST_ASSERT_NULL(FEED(0x91, 0x3C, 0x64)); /* ch 2 */
  TEST_ASSERT_NOT_NULL(FEED(0x90, 0x3C, 0x64)); /* ch 1 */
}

/* -------------------------------------------------------------------------
 * Channel filtering
 * ---------------------------------------------------------------------- */
void channel_filter_accepts_matching_channel(void) {
  inst.channel = 1;                              /* listen on ch 1*/
  struct midi_msg *msg = FEED(0x90, 0x3C, 0x7F); /* ch 1 Note On*/
  TEST_ASSERT_NOT_NULL(msg);
}

void channel_filter_rejects_other_channel(void) {
  inst.channel = 1;                              /* listen on ch 1        */
  struct midi_msg *msg = FEED(0x92, 0x3C, 0x7F); /* ch 3 Note On          */
  TEST_ASSERT_NULL(msg);
}

void omni_accepts_ch2(void) {
  inst.channel = MIDI_OMNI;
  struct midi_msg *msg = FEED(0x91, 0x3C, 0x7F); /* ch 2 Note On          */
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0] & 0xF0);
}

void omni_accepts_ch6(void) {
  inst.channel = MIDI_OMNI;
  struct midi_msg *msg = FEED(0x95, 0x48, 0x40); /* ch 6 Note On          */
  TEST_ASSERT_NOT_NULL(msg);
  TEST_ASSERT_EQUAL_UINT8(MIDI_STATUS_NOTE_ON, msg->data[0] & 0xF0);
}

void channel_filter_accepts_ch1(void) {
  inst.channel = 1;
  struct midi_msg *msg =
      FEED(0x90, 0x3C, 0x7F); /* ch 1 Note On, status = 0x90 */
  TEST_ASSERT_NOT_NULL(msg);
}

void channel_filter_accepts_ch16(void) {
  inst.channel = 16;
  struct midi_msg *msg =
      FEED(0x9F, 0x3C, 0x7F); /* ch 16 Note On, status = 0x9F */
  TEST_ASSERT_NOT_NULL(msg);
}

int main(void) {
  UNITY_BEGIN();

  /* 3-byte channel messages*/
  RUN_TEST(note_on_returns_msg_at_third_byte);
  RUN_TEST(note_off_parsed_correctly);
  RUN_TEST(control_change_parsed_correctly);
  /* 2-byte channel messages*/
  RUN_TEST(pitch_bend_parsed_correctly);
  RUN_TEST(poly_pressure_parsed_correctly);
  RUN_TEST(program_change_returns_on_second_byte);
  RUN_TEST(channel_pressure_returns_on_second_byte);
  /* Running status */
  RUN_TEST(running_status_cleared_by_new_status);
  RUN_TEST(running_status_second_note_on);
  RUN_TEST(running_status_multiple_cc);
  /* Intermediate states*/
  RUN_TEST(no_message_after_first_data_byte);
  RUN_TEST(no_message_after_status_only);
  /* Sysex draining */
  RUN_TEST(sysex_bytes_are_swallowed);
  RUN_TEST(note_on_status_ends_an_unterminated_sysex);
  RUN_TEST(running_status_works_after_an_unterminated_sysex);
  RUN_TEST(system_common_status_ends_an_unterminated_sysex);
  RUN_TEST(realtime_byte_inside_sysex_is_returned_and_sysex_continues);
  RUN_TEST(sysex_cancels_running_status);
  RUN_TEST(message_received_after_sysex_ends);
  RUN_TEST(sysex_survives_gaps_under_the_timeout);
  RUN_TEST(sysex_recovers_after_inter_byte_timeout_without_end_flag);
  /* Reatime*/
  RUN_TEST(clock_returned_mid_message);
  RUN_TEST(note_completes_after_interleaved_clock);
  RUN_TEST(active_sense_is_single_byte);
  RUN_TEST(start_stop_continue_are_single_byte);
  /* System */
  RUN_TEST(song_position_pointer);
  RUN_TEST(song_select);
  RUN_TEST(song_select_does_not_take_a_stray_byte_as_a_third_byte);
  RUN_TEST(time_code_is_a_two_byte_message);
  RUN_TEST(system_common_cancels_running_status);
  RUN_TEST(note_on_after_song_select_parses);
  RUN_TEST(song_position_passes_the_channel_filter);
  RUN_TEST(song_select_passes_the_channel_filter);
  RUN_TEST(channel_filter_still_applies_after_a_system_message);
  /* Channel filter*/
  RUN_TEST(channel_filter_accepts_matching_channel);
  RUN_TEST(channel_filter_rejects_other_channel);
  RUN_TEST(channel_filter_accepts_ch16);
  RUN_TEST(channel_filter_accepts_ch1);
  RUN_TEST(omni_accepts_ch2);
  RUN_TEST(omni_accepts_ch6);

  return UNITY_END();
}