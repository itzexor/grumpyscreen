#include "init_panel.h"
#include "utils.h"
#include "state.h"
#include "config.h"
#include "mmu_panel.h"
#include "logger.h"
#include "theme.h"

using namespace Theme;

#include <algorithm>
#include <cstdio>

InitPanel::InitPanel(MainPanel &mp, StatusBar &sb, std::mutex& l)
  : main_panel(mp)
  , status_bar(sb)
  , lv_lock(l)
  , poll_timer(INVALID_TIMER_ID)
  , ready(false)
{
}

InitPanel::~InitPanel() {
}

void InitPanel::connected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel connected");
  show_state(StatusBar::Klipper::STARTING, "");
  start_polling(ws);

  State *state = State::get_instance();
  state->reset();

  ws.send_jsonrpc("printer.objects.list", [this, &ws](json& d) {
    State *state = State::get_instance();
	  state->set_data("printer_objs", d, "/result");

	  ws.send_jsonrpc("server.files.roots",
			[](json& j) { State::get_instance()->set_data("roots", j, "/result"); });

	  ws.send_jsonrpc("printer.info",
			[](json& j) { State::get_instance()->set_data("printer_info", j, "/result"); });

    this->main_panel.subscribe();

    // spoolman
    ws.send_jsonrpc("server.info", [this](json &j) {
      if (get_log_level() <= LogLevel::DEBUG) {
        LOG_DEBUG("server_info {}", j.dump());
      }
      State::get_instance()->set_data("server_info", j, "/result");

      const auto components_ptr = "/result/components"_json_pointer;
      if (j.contains(components_ptr) && !j[components_ptr].is_null()) {
        auto &components = j[components_ptr];
        const auto &has_spoolman = components.template get<std::vector<std::string>>();
        if (std::find(has_spoolman.begin(), has_spoolman.end(), "spoolman") != has_spoolman.end()) {
          this->main_panel.enable_spoolman();
        }
      }
    });

    auto display_sensors = state->get_display_sensors();
    this->main_panel.create_sensors(display_sensors);

    auto display_fans = state->get_display_fans();
    this->main_panel.create_fans(display_fans);

    auto display_leds = state->get_display_leds();
    this->main_panel.create_leds(display_leds);

    // subscribe to all objects except gcode_macro
    const auto objects_ptr = "/result/objects"_json_pointer;
    if (d.contains(objects_ptr) && !d[objects_ptr].is_null()) {
      auto objs = d[objects_ptr];
      json sub_objs;
      for (auto &obj : objs) {
        std::string obj_name = obj.template get<std::string>();
        if (obj_name.rfind("gcode_macro ", 0 ) != 0) {
          sub_objs[obj_name] = nullptr;
        }
      }

      // runs again on every klipper reconnect, so the tab has to follow the
      // backend back down as well as up
      MmuPanel *mmu = this->main_panel.mmu();
      if (mmu != NULL && mmu->select_backend() != NULL) {
        this->main_panel.enable_mmu();
      } else {
        this->main_panel.disable_mmu();
      }

      json subs = {{ "objects", sub_objs }};
      ws.send_jsonrpc("printer.info", [this, &ws, subs](json& info) {
        State::get_instance()->set_data("printer_info", info, "/result");
        const std::string klippy_state = info.value("/result/state"_json_pointer, std::string());
        if (klippy_state != "ready") {
          LOG_DEBUG("Klipper is not ready (state: {})", klippy_state.empty() ? "unknown" : klippy_state);
          return;
        }

        if (get_log_level() <= LogLevel::DEBUG) {
          LOG_DEBUG("subscribing to {}", subs.dump());
        }
        ws.send_jsonrpc("printer.objects.subscribe", subs, [this, &ws](json &data) {
          State::get_instance()->set_data("printer_state", data, "/result/status");
          this->main_panel.init(data);
          LOG_DEBUG("done init");
          stop_polling(ws);
          std::lock_guard<std::mutex> lock(this->lv_lock);
          ready = true;
          reported.clear();
          status_bar.set_klipper(StatusBar::Klipper::READY);
          main_panel.set_ready(true);
        });
      });
    }
  });
}

void InitPanel::disconnected(KWebSocketClient &ws) {
  LOG_DEBUG("init panel disconnected");
  const bool moonraker_up = ws.is_open();
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    ready = false;
    main_panel.set_ready(false);
    status_bar.set_klipper(moonraker_up ? StatusBar::Klipper::STARTING : StatusBar::Klipper::OFFLINE);
  }

  if (moonraker_up) {
    // Klipper went away or stopped: ask now, so a shutdown's reason is on the
    // bar straight away rather than after the first tick
    start_polling(ws);
    poll(ws);
  } else {
    stop_polling(ws);
  }
}

void InitPanel::start_polling(KWebSocketClient &ws) {
  if (poll_timer != INVALID_TIMER_ID) return;
  poll_timer = ws.loop()->setInterval(2000, [this, &ws](hv::TimerID) { poll(ws); });
}

void InitPanel::stop_polling(KWebSocketClient &ws) {
  if (poll_timer == INVALID_TIMER_ID) return;
  ws.loop()->killTimer(poll_timer);
  poll_timer = INVALID_TIMER_ID;
}

void InitPanel::poll(KWebSocketClient &ws) {
  ws.send_jsonrpc("server.info", [this, &ws](json &info) {
    if (!info.value("/result/klippy_connected"_json_pointer, false)) {
      show_state(StatusBar::Klipper::DISCONNECTED, "");
      return;
    }

    // Klipper's own word on its state, and why, from its info endpoint
    ws.send_jsonrpc("printer.info", [this](json &j) {
      const std::string state = j.value("/result/state"_json_pointer, std::string());
      const std::string message = j.value("/result/state_message"_json_pointer, std::string());
      if (state == "error") {
        show_state(StatusBar::Klipper::ERROR, message);
      } else if (state == "shutdown") {
        show_state(StatusBar::Klipper::SHUTDOWN, message);
      } else if (state == "startup") {
        show_state(StatusBar::Klipper::STARTING, "");
      }
      // ready: notify_klippy_ready brings the UI up, and the bar with it
    });
  });
}

void InitPanel::show_state(StatusBar::Klipper k, const std::string &message) {
  std::lock_guard<std::mutex> lock(lv_lock);
  if (ready) return;

  status_bar.set_klipper(k, message);

  // the bar has room for a line; the whole message goes to the console, once
  const bool stopped = k == StatusBar::Klipper::ERROR || k == StatusBar::Klipper::SHUTDOWN;
  if (!stopped) {
    reported.clear();
  } else if (!message.empty() && message != reported) {
    reported = message;
    main_panel.report(message);
  }
}
