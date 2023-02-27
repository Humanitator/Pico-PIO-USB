// This example runs both host and device concurrently. The USB host receive
// reports from HID device and print it out over USB Device CDC interface.
// For TinyUSB roothub port0 is native usb controller, roothub port1 is
// pico-pio-usb.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"

#include "bsp/board.h"
#include "pio_usb.h"
#include "tusb.h"

#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

// uncomment if you are using colemak layout
// #define KEYBOARD_COLEMAK

#ifdef KEYBOARD_COLEMAK
const uint8_t colemak[128] = {
  0  ,  0,  0,  0,  0,  0,  0, 22,
  9  , 23,  7,  0, 24, 17,  8, 12,
  0  , 14, 28, 51,  0, 19, 21, 10,
  15 ,  0,  0,  0, 13,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0, 18,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0,
  0  ,  0,  0,  0,  0,  0,  0,  0
};
#endif

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };
static uint8_t const ascii2keycode[128][2] =  { HID_ASCII_TO_KEYCODE };

/*------------- MAIN -------------*/

// core1: handle host events
void core1_main() {
  sleep_ms(10);

  // Use tuh_configure() to pass pio configuration to the host stack
  // Note: tuh_configure() must be called before
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

  // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
  // port1) on core1
  tuh_init(1);

  while (true) {
    tuh_task(); // tinyusb host task
  }
}

// core0: handle device events
int main(void) {
  // default 125MHz is not appropreate. Sysclock should be multiple of 12MHz.
  set_sys_clock_khz(120000, true);

  sleep_ms(10);

  multicore_reset_core1();
  // all USB task run in core1
  multicore_launch_core1(core1_main);

  // init device stack on native usb (roothub port0)
  tud_init(0);

  while (true) {
    tud_task(); // tinyusb device task
    // tud_cdc_write_flush();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device CDC
//--------------------------------------------------------------------+

// Invoked when CDC interface received data from host
// void tud_cdc_rx_cb(uint8_t itf)
// {
//   (void) itf;

//   char buf[64];
//   uint32_t count = tud_cdc_read(buf, sizeof(buf));

//   // TODO control LED on keyboard of host stack
//   (void) count;
// }

//--------------------------------------------------------------------+
// USB DEVICE HID
//--------------------------------------------------------------------+

static void send_hid_report(uint8_t report_id, uint8_t modifier, uint8_t keycode[6])
{
  // skip if hid is not ready yet
  if ( !tud_hid_ready() ) return;

  switch(report_id)
  {
    case REPORT_ID_KEYBOARD:
    {
      // Send key report
      tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keycode);
    }
    break;

    case REPORT_ID_MOUSE:
    {
      int8_t const delta = 5;

      // no button, right + down, no scroll, no pan
      tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, delta, delta, 0, 0);
    }
    break;

    default: break;
  }
}

// Every 10ms, we will sent 1 report for each HID profile (keyboard, mouse etc ..)
// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
// void hid_task(void)
// {
//   // Poll every 10ms
//   const uint32_t interval_ms = 10;
//   static uint32_t start_ms = 0;

//   if ( board_millis() - start_ms < interval_ms) return; // not enough time
//   start_ms += interval_ms;

//   uint32_t const btn = board_button_read();

//   // Remote wakeup
//   if ( tud_suspended() && btn )
//   {
//     // Wake up host if we are in suspend mode
//     // and REMOTE_WAKEUP feature is enabled by host
//     tud_remote_wakeup();
//   }else
//   {
//     // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
//     send_hid_report(REPORT_ID_KEYBOARD, );
//   }
// }


// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) len;

  uint8_t next_report_id = report[0] + 1u;

  // if (next_report_id < REPORT_ID_COUNT)
  // {
  //   send_hid_report(next_report_id, );
  // }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      // if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      // {
      //   // Capslock On: disable blink, turn led on
      //   blink_interval_ms = 0;
      //   board_led_write(true);
      // }else
      // {
      //   // Caplocks Off: back to normal blink
      //   board_led_write(false);
      //   blink_interval_ms = BLINK_MOUNTED;
      // }
    }
  }
}
\

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);

  char tempbuf[256];
  int count = sprintf(tempbuf, "[%04x:%04x][%u] HID Interface%u, Protocol = %s\r\n", vid, pid, dev_addr, instance, protocol_str[itf_protocol]);

  // tud_cdc_write(tempbuf, count);
  // tud_cdc_write_flush();

  // Receive report from boot keyboard & mouse only
  // tuh_hid_report_received_cb() will be invoked when report is available
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || itf_protocol == HID_ITF_PROTOCOL_MOUSE)
  {
    if ( !tuh_hid_receive_report(dev_addr, instance) )
    {
      // tud_cdc_write_str("Error: cannot request report\r\n");
    }
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  char tempbuf[256];
  int count = sprintf(tempbuf, "[%u] HID Interface%u is unmounted\r\n", dev_addr, instance);
  // tud_cdc_write(tempbuf, count);
  // tud_cdc_write_flush();
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for(uint8_t i=0; i<6; i++)
  {
    if (report->keycode[i] == keycode)  return true;
  }

  return false;
}

// "print" text with keyboard
static void KeyText(uint8_t *text)
{
  for (uint8_t i = 0; i < sizeof(text); i++)
  {
    uint8_t ch = text[i];

    uint8_t modifier = 0;
    uint8_t keycode[6] = { 0 };

    if ( ascii2keycode[ch][0] ) modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
    keycode[0] = ascii2keycode[ch][1];
    send_hid_report(REPORT_ID_KEYBOARD, modifier, keycode);
    sleep_ms(50);
  }
}

// Text logging data
static uint8_t loggedText[128];
static int lastLog = -1;

// convert hid keycode to ascii and print via usb device CDC (ignore non-printable)
static void process_kbd_report(uint8_t dev_addr, uint8_t instance, hid_keyboard_report_t const *report)
{
  (void) dev_addr;
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released
  static uint8_t leds = 0;

  uint8_t keycode[6] = { 0 };
  uint8_t modifier = report->modifier;

  uint8_t charachters[6] = {0};
  uint8_t casedChars[6] = {0};

  // Set keys
  for (uint8_t i = 0; i < 6; i++)
  {
    uint8_t kc = report->keycode[i];
    keycode[i] = kc;

    bool const is_shift = modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
    bool const is_alt = modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT);
    casedChars[i] = keycode2ascii[kc][is_shift ? 1 : 0];
    charachters[i] = keycode2ascii[kc][0];

    // Print saved buffer
    if (charachters[0] == 'p' && charachters[1] == 'b' && is_shift && is_alt)
    {
      modifier = 0;
      KeyText(loggedText);

      // Empty cased chars
      casedChars[0] = 0;
      casedChars[1] = 0;
      casedChars[2] = 0;
      casedChars[3] = 0;
      casedChars[4] = 0;
      casedChars[5] = 0;

      // Empty keycode
      keycode[0] = 0;
      keycode[1] = 0;
      keycode[2] = 0;
      keycode[3] = 0;
      keycode[4] = 0;
      keycode[5] = 0;
    }
    //   else if (kc == HID_KEY_CAPS_LOCK) {
    //     leds ^= KEYBOARD_LED_CAPSLOCK;
    //     tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT,
    //                         &leds, sizeof(leds));
    //   } else if (kc == HID_KEY_NUM_LOCK) {
    //     leds ^= KEYBOARD_LED_NUMLOCK;
    //     tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT,
    //                         &leds, sizeof(leds));
    //   } else if (kc == HID_KEY_SCROLL_LOCK) {
    //     leds ^= KEYBOARD_LED_SCROLLLOCK;
    //     tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT,
    //                         &leds, sizeof(leds));
    //   }
  }

  if (casedChars[0])
  {
    lastLog++;
    loggedText[lastLog] = casedChars[0];
  }

  // Send report
  send_hid_report(REPORT_ID_KEYBOARD, modifier, keycode);
}

// send mouse report to usb device CDC
static void process_mouse_report(uint8_t dev_addr, hid_mouse_report_t const * report)
{
  //------------- button state  -------------//
  //uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  char l = report->buttons & MOUSE_BUTTON_LEFT   ? 'L' : '-';
  char m = report->buttons & MOUSE_BUTTON_MIDDLE ? 'M' : '-';
  char r = report->buttons & MOUSE_BUTTON_RIGHT  ? 'R' : '-';

  char tempbuf[32];
  int count = sprintf(tempbuf, "[%u] %c%c%c %d %d %d\r\n", dev_addr, l, m, r, report->x, report->y, report->wheel);

  // tud_cdc_write(tempbuf, count);
  // tud_cdc_write_flush();
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) len;
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  switch(itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      process_kbd_report(dev_addr, instance, (hid_keyboard_report_t const*) report );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      process_mouse_report(dev_addr, (hid_mouse_report_t const*) report );
    break;

    default: break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    // tud_cdc_write_str("Error: cannot request report\r\n");
  }
}
