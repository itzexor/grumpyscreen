#ifndef __STATUS_BAR_H__
#define __STATUS_BAR_H__

#include "lvgl/lvgl.h"

#include <functional>
#include <string>

// The strip along the top of the display: what state Klipper is in, a short
// line about it when something is wrong, and the running print. Nothing on the
// screen covers it; Theme::status_bar_h() is the room it takes.
//
// When there is something to go to, the middle of the bar is a pill that goes
// there on a tap: the print status screen while printing, the console (where
// the full message is) when Klipper has stopped.
//
// Every setter is called with the lvgl lock held.
class StatusBar {
 public:
  // Moonraker's view of Klipper, plus the state of Moonraker itself
  enum class Klipper {
    OFFLINE,       // no connection to Moonraker
    DISCONNECTED,  // Moonraker is up but Klipper is not connected to it
    STARTING,      // Klipper is starting, or the UI is still subscribing
    READY,
    ERROR,         // Klipper failed to start (usually its config)
    SHUTDOWN,      // Klipper stopped itself
  };

  StatusBar(lv_obj_t *screen);
  ~StatusBar();

  // message is Klipper's state_message; the bar shows its first line
  void set_klipper(Klipper k, const std::string &message = "");
  // print_stats.state and .filename, the print's progress (percent) and time left
  void set_print_state(const std::string &state);
  void set_print_file(const std::string &filename);
  void set_print_progress(int percent);
  void set_print_eta(const std::string &eta);
  // while the print status screen is up, the print pill has nowhere to go
  void set_print_shown(bool shown);
  void reset_print();

  void on_print(std::function<void()> cb) { print_cb = cb; }
  void on_details(std::function<void()> cb) { details_cb = cb; }

 private:
  enum class Pill { NONE, PRINT, DETAILS };

  void refresh();
  Pill pill_mode() const;
  static void _handle_click(lv_event_t *event);

  lv_obj_t *bar;
  lv_obj_t *row;
  lv_obj_t *dot;
  lv_obj_t *spinner;
  lv_obj_t *alert;
  lv_obj_t *state_label;
  lv_obj_t *message_label;
  lv_obj_t *pill;
  lv_obj_t *pill_text;
  lv_obj_t *pill_value;
  lv_obj_t *pill_aux;
  lv_obj_t *pill_chevron;
  lv_obj_t *progress;

  Klipper klipper;
  std::string message;
  std::string print_state;
  std::string print_file;
  int print_progress;
  std::string print_eta;
  bool print_shown;

  std::function<void()> print_cb;
  std::function<void()> details_cb;
};

#endif // __STATUS_BAR_H__
