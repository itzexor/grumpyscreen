#include "status_bar.h"
#include "theme.h"

using namespace Theme;

namespace {

// the first line of a Klipper state_message; the rest goes to the console
std::string first_line(const std::string &s) {
  const size_t nl = s.find('\n');
  return nl == std::string::npos ? s : s.substr(0, nl);
}

void show(lv_obj_t *obj, bool visible) {
  if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *bar_label(lv_obj_t *parent) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, "");
  return l;
}

}  // namespace

StatusBar::StatusBar(lv_obj_t *screen)
  : bar(lv_obj_create(screen))
  , row(lv_obj_create(bar))
  , dot(lv_obj_create(row))
  , spinner(lv_spinner_create(row, 1000, 90))
  , alert(bar_label(row))
  , state_label(bar_label(row))
  , message_label(bar_label(row))
  , pill(lv_obj_create(row))
  , pill_text(bar_label(pill))
  , pill_value(bar_label(pill))
  , pill_aux(bar_label(pill))
  , pill_chevron(bar_label(pill))
  , progress(lv_obj_create(bar))
  , klipper(Klipper::OFFLINE)
  , print_progress(0)
  , print_shown(false)
{
  const int h = status_bar_h();

  // The screen and the top layer (where dialogs open) give up their top strip
  // to the bar, so every panel sized in percent sits under it and no dialog's
  // dimmed backdrop reaches it. The bar lives in that padding, above the
  // content area, so the screen must not take it for content to scroll to.
  lv_obj_set_style_pad_top(screen, h, 0);
  lv_obj_set_style_pad_top(lv_layer_top(), h, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_pos(bar, 0, -h);
  lv_obj_set_size(bar, LV_PCT(100), h);
  lv_obj_add_style(bar, &styles().row, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(bar, 0, 0);
  // The one place the UI goes under the 12px floor of the type scale: at
  // 800x480 scale_font(12) is 20px, which would make a bar this slim cramped.
  // At 480x272 this still lands on 12.
  lv_obj_set_style_text_font(bar, scale_font(10), 0);
  lv_obj_set_style_text_color(bar, col(TEXT), 0);

  // the content, ruled off from the page below it
  lv_obj_add_style(row, &styles().row, 0);
  lv_obj_set_size(row, LV_PCT(100), LV_PCT(100));
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_hor(row, gap(), 0);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, border_w(), 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // the state light: a dot when settled, a spinner while waiting, a warning
  // sign when Klipper has stopped. One shows at a time.
  const int light = lv_font_get_line_height(scale_font(10)) * 2 / 3;
  lv_obj_add_style(dot, &styles().row, 0);
  lv_obj_set_size(dot, light * 3 / 4, light * 3 / 4);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);

  lv_obj_set_size(spinner, light, light);
  lv_obj_set_style_arc_width(spinner, scale_r(2), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner, scale_r(2), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spinner, col(WARNING), LV_PART_INDICATOR);

  lv_label_set_text(alert, LV_SYMBOL_WARNING);

  // the gap between the state and the message is wider than the one inside
  // the state, so the two read as separate
  lv_obj_set_style_pad_right(state_label, gap(), 0);

  lv_obj_set_flex_grow(message_label, 1);
  lv_label_set_long_mode(message_label, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_color(message_label, col(TEXT_DIM), 0);

  // the pill: a rounded button across the rest of the bar
  lv_obj_add_style(pill, &styles().row, 0);
  lv_obj_set_flex_grow(pill, 1);
  lv_obj_set_height(pill, h - 2 * scale_r(2));
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_left(pill, gap() * 3 / 2, 0);
  lv_obj_set_style_pad_right(pill, gap() / 2, 0);
  lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_style(pill, &styles().card_pressed, LV_STATE_PRESSED);
  // the whole height of the bar takes the tap, not just the pill drawn in it
  lv_obj_set_ext_click_area(pill, scale_r(2));
  lv_obj_add_event_cb(pill, &StatusBar::_handle_click, LV_EVENT_CLICKED, this);

  lv_obj_set_flex_grow(pill_text, 1);
  lv_label_set_long_mode(pill_text, LV_LABEL_LONG_DOT);
  lv_label_set_text(pill_chevron, LV_SYMBOL_RIGHT);

  // the print's progress, drawn along the bottom edge over the rule
  lv_obj_add_style(progress, &styles().row, 0);
  lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(progress, theme_primary(), 0);
  lv_obj_set_style_radius(progress, 0, 0);
  lv_obj_set_height(progress, scale_r(2));
  lv_obj_align(progress, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  refresh();
}

StatusBar::~StatusBar() {
  if (bar != NULL) {
    lv_obj_del(bar);
    bar = NULL;
  }
}

void StatusBar::set_klipper(Klipper k, const std::string &msg) {
  klipper = k;
  message = msg;
  refresh();
}

void StatusBar::set_print_state(const std::string &state) {
  print_state = state;
  refresh();
}

void StatusBar::set_print_file(const std::string &filename) {
  // the file's own name; the folders it is in do not fit
  const size_t slash = filename.rfind('/');
  print_file = slash == std::string::npos ? filename : filename.substr(slash + 1);
  refresh();
}

void StatusBar::set_print_progress(int percent) {
  print_progress = percent;
  refresh();
}

void StatusBar::set_print_eta(const std::string &eta) {
  print_eta = eta;
  refresh();
}

void StatusBar::set_print_shown(bool shown) {
  print_shown = shown;
  refresh();
}

void StatusBar::reset_print() {
  print_file.clear();
  print_progress = 0;
  print_eta.clear();
  refresh();
}

StatusBar::Pill StatusBar::pill_mode() const {
  if (klipper == Klipper::ERROR || klipper == Klipper::SHUTDOWN) return Pill::DETAILS;
  if (klipper == Klipper::READY && (print_state == "printing" || print_state == "paused")) return Pill::PRINT;
  return Pill::NONE;
}

void StatusBar::refresh() {
  const bool stopped = klipper == Klipper::ERROR || klipper == Klipper::SHUTDOWN;
  const bool waiting = klipper == Klipper::STARTING || klipper == Klipper::DISCONNECTED;

  // a stopped Klipper turns the whole bar red, so it is seen from across the room
  lv_obj_set_style_bg_color(bar, stopped ? lv_color_mix(col(DANGER), col(CHROME), LV_OPA_20) : col(CHROME), 0);
  lv_obj_set_style_border_color(row, stopped ? col(DANGER) : col(BORDER), 0);

  // the danger red is too dark to read on the red bar; a lighter tint of it is not
  const lv_color_t stop_red = lv_color_mix(lv_color_white(), col(DANGER), LV_OPA_40);
  lv_obj_set_style_text_color(alert, stop_red, 0);

  show(dot, !stopped && !waiting);
  show(spinner, waiting);
  show(alert, stopped);
  lv_obj_set_style_bg_color(dot, klipper == Klipper::READY ? col(GOOD) : col(DISABLED), 0);

  const char *state_text = "";
  const char *wait_text = "";
  switch (klipper) {
  case Klipper::OFFLINE:
    state_text = "Offline";
    wait_text = "Connecting to Moonraker...";
    break;
  case Klipper::DISCONNECTED:
    state_text = "Disconnected";
    wait_text = "Waiting for Klipper to connect...";
    break;
  case Klipper::STARTING:
    state_text = "Starting";
    wait_text = "Waiting for Klipper to start...";
    break;
  case Klipper::READY:
    state_text = "Ready";
    break;
  case Klipper::ERROR:
    state_text = "Error";
    break;
  case Klipper::SHUTDOWN:
    state_text = "Shutdown";
    break;
  }
  lv_label_set_text(state_label, state_text);
  lv_obj_set_style_text_color(state_label,
                              stopped ? stop_red : waiting ? col(WARNING) : col(TEXT_DIM), 0);

  const Pill mode = pill_mode();
  show(message_label, mode == Pill::NONE);
  show(pill, mode != Pill::NONE);
  show(progress, mode == Pill::PRINT);
  lv_label_set_text(message_label, wait_text);

  if (mode == Pill::DETAILS) {
    const std::string line = first_line(message);
    lv_label_set_text(pill_text, line.empty() ? "Klipper has stopped" : line.c_str());
    lv_obj_set_style_bg_color(pill, lv_color_mix(col(DANGER), col(CHROME), LV_OPA_40), 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    show(pill_value, false);
    show(pill_aux, true);
    lv_label_set_text(pill_aux, "Details");
    lv_obj_set_style_text_color(pill_aux, col(TEXT_DIM), 0);
    show(pill_chevron, true);
  } else if (mode == Pill::PRINT) {
    lv_label_set_text(pill_text, print_file.c_str());
    lv_obj_set_style_bg_color(pill, col(RAISED), 0);
    lv_obj_set_style_border_width(pill, border_w(), 0);
    lv_obj_set_style_border_color(pill, theme_primary(), 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_50, 0);
    show(pill_value, true);
    lv_label_set_text_fmt(pill_value, "%d%%", print_progress);
    lv_obj_set_style_text_color(pill_value, theme_primary(), 0);
    const bool paused = print_state == "paused";
    show(pill_aux, paused || !print_eta.empty());
    lv_label_set_text(pill_aux, paused ? "Paused" : print_eta.c_str());
    lv_obj_set_style_text_color(pill_aux, paused ? col(WARNING) : col(TEXT_DIM), 0);
    show(pill_chevron, !print_shown);
    lv_obj_set_width(progress, lv_pct(LV_CLAMP(0, print_progress, 100)));
  }
}

void StatusBar::_handle_click(lv_event_t *event) {
  StatusBar *self = static_cast<StatusBar *>(lv_event_get_user_data(event));
  switch (self->pill_mode()) {
  case Pill::PRINT:
    if (self->print_cb) self->print_cb();
    break;
  case Pill::DETAILS:
    if (self->details_cb) self->details_cb();
    break;
  case Pill::NONE:
    break;
  }
}
