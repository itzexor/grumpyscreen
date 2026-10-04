#ifndef __INIT_PANEL_H__
#define __INIT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "main_panel.h"
#include "status_bar.h"
#include "print_status_panel.h"

#include <mutex>
#include <string>

// Brings the UI up on a Klipper connection, and reports Klipper's state on the
// status bar until it is ready.
class InitPanel {
 public:
  InitPanel(MainPanel &mp, StatusBar &sb, std::mutex &l);
  ~InitPanel();

  void connected(KWebSocketClient &ws);
  void disconnected(KWebSocketClient &ws);

 private:
  // While Klipper is not ready, Moonraker is asked for its state every couple
  // of seconds: notify_klippy_ready says when it comes up, but nothing says
  // when a starting Klipper fails instead. Runs on the websocket's loop.
  void start_polling(KWebSocketClient &ws);
  void stop_polling(KWebSocketClient &ws);
  void poll(KWebSocketClient &ws);
  // takes the lvgl lock
  void show_state(StatusBar::Klipper k, const std::string &message);

  MainPanel &main_panel;
  StatusBar &status_bar;
  std::mutex &lv_lock;
  hv::TimerID poll_timer;
  // the subscription is up; a poll answer still in flight must not undo it.
  // Guarded by lv_lock.
  bool ready;
  // the last stop message copied to the console, so a repeat is not
  std::string reported;
};

#endif // __INIT_PANEL_H__
