#include "library.hpp"

#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/core/lv2.h>
#include <lv2/patch/patch.h>
#include <lv2/ui/ui.h>
#include <lv2/urid/urid.h>
#include <dialogs/xfile-dialog.h>
#include <xwidgets.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define TL_URI "urn:neville:toneloader"
#define TL_UI_URI TL_URI "#ui"
#define TL_PEDAL_MODEL TL_URI "#pedalModel"
#define TL_AMP_MODEL TL_URI "#ampModel"
#define TL_IR_MODEL TL_URI "#irModel"
#define TL_PEDAL_AUDITION TL_URI "#pedalAudition"
#define TL_AMP_AUDITION TL_URI "#ampAudition"
#define TL_IR_AUDITION TL_URI "#irAudition"
#define TL_PEDAL_CANCEL TL_URI "#pedalCancelAudition"
#define TL_AMP_CANCEL TL_URI "#ampCancelAudition"
#define TL_IR_CANCEL TL_URI "#irCancelAudition"

namespace {

struct Knob {
  uint32_t port;
  const char* label;
  int x;
  int y;
  float minimum;
  float maximum;
};

constexpr std::array<Knob, 8> knobs{{
    {5, "INPUT", 560, 118, -20.0f, 20.0f},
    {6, "OUTPUT", 680, 118, -20.0f, 20.0f},
    {7, "QUALITY", 800, 118, 0.0f, 1.0f},
    {9, "INPUT", 560, 236, -20.0f, 20.0f},
    {10, "OUTPUT", 680, 236, -20.0f, 20.0f},
    {11, "QUALITY", 800, 236, 0.0f, 1.0f},
    {13, "INPUT", 620, 354, -20.0f, 20.0f},
    {14, "WET", 780, 354, 0.0f, 100.0f},
}};

struct Ui {
  Xputty app{};
  Widget_t* window{};
  vfunc events{};
  LV2UI_Resize* resize{};
  LV2UI_Write_Function write{};
  LV2UI_Controller controller{};
  LV2_URID_Map* map{};
  LV2_Atom_Forge forge{};
  LV2_URID atom_event_transfer{};
  LV2_URID atom_object{};
  LV2_URID atom_path{};
  LV2_URID atom_urid{};
  LV2_URID patch_get{};
  LV2_URID patch_set{};
  LV2_URID patch_property{};
  LV2_URID patch_value{};
  std::array<LV2_URID, 3> model_uri{};
  std::array<LV2_URID, 3> audition_uri{};
  std::array<LV2_URID, 3> cancel_uri{};
  std::array<float, 15> values{};
  std::array<std::string, 3> model_paths{};
  std::filesystem::path factory_model;
  std::filesystem::path library;
  std::vector<toneloader::Pack> packs;
  bool settings{};
  bool directory_dialog_open{};
  bool library_save_error{};
  int chooser{-1};
  int selected_pack{-1};
  int selected_model{-1};
  int pack_offset{};
  int model_offset{};
  int audition_pack{-1};
  int audition_model{-1};
  std::string audition_path;
  bool audition_loading{};
  bool audition_error{};
  bool commit_pending{};
  std::string commit_path;
  float saved_bypass{};
  Time last_click{};
  int last_pack{-1};
  int last_model{-1};
  Time last_knob_click{};
  int last_knob{-1};
  double pulse{};
  int active_knob{-1};
  int active_scroll{-1};
  int editing_knob{-1};
  std::string edit_value;
  int drag_y{};
  float drag_value{};
};

void color(cairo_t* cr, double red, double green, double blue, double alpha = 1.0) {
  cairo_set_source_rgba(cr, red, green, blue, alpha);
}

void text(cairo_t* cr, const char* value, double x, double y, double size,
          bool centered = false, bool italic = false) {
  cairo_select_font_face(cr, "Sans", italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, size);
  if (centered) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, value, &extents);
    x -= extents.x_bearing + extents.width / 2.0;
  }
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, value);
}

void rounded_rectangle(cairo_t* cr, double x, double y, double width, double height,
                       double radius) {
  constexpr double pi = 3.14159265358979323846;
  cairo_new_sub_path(cr);
  cairo_arc(cr, x + width - radius, y + radius, radius, -pi / 2.0, 0);
  cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, pi / 2.0);
  cairo_arc(cr, x + radius, y + height - radius, radius, pi / 2.0, pi);
  cairo_arc(cr, x + radius, y + radius, radius, pi, 3.0 * pi / 2.0);
  cairo_close_path(cr);
}

void section_icon(cairo_t* cr, int section, double x, double y) {
  cairo_save(cr);
  color(cr, 0.72, 0.74, 0.77);
  cairo_set_line_width(cr, 1.8);
  if (section == 0) {
    rounded_rectangle(cr, x + 5, y + 2, 22, 26, 3);
    cairo_stroke(cr);
    cairo_arc(cr, x + 12, y + 9, 2, 0, 6.283185307179586);
    cairo_arc(cr, x + 20, y + 9, 2, 0, 6.283185307179586);
    cairo_stroke(cr);
    cairo_arc(cr, x + 16, y + 20, 4, 0, 6.283185307179586);
    cairo_stroke(cr);
  } else if (section == 1) {
    rounded_rectangle(cr, x + 1, y + 7, 30, 19, 2);
    cairo_stroke(cr);
    cairo_move_to(cr, x + 9, y + 7);
    cairo_line_to(cr, x + 12, y + 3);
    cairo_line_to(cr, x + 20, y + 3);
    cairo_line_to(cr, x + 23, y + 7);
    cairo_stroke(cr);
    for (int i = 0; i < 4; ++i) {
      cairo_arc(cr, x + 8 + i * 5, y + 18, 1.2, 0, 6.283185307179586);
      cairo_fill(cr);
    }
  } else {
    rounded_rectangle(cr, x + 3, y + 1, 26, 28, 2);
    cairo_stroke(cr);
    for (int row = 0; row < 2; ++row)
      for (int column = 0; column < 2; ++column) {
        cairo_arc(cr, x + 10 + column * 12, y + 9 + row * 12, 4, 0,
                  6.283185307179586);
        cairo_stroke(cr);
      }
  }
  cairo_restore(cr);
}

void bypass_icon(cairo_t* cr, double x, double y) {
  cairo_save(cr);
  color(cr, 0.906, 0.914, 0.925);
  cairo_set_line_width(cr, 1.8);
  cairo_rectangle(cr, x - 6, y - 5, 12, 12);
  cairo_stroke(cr);
  cairo_move_to(cr, x - 14, y + 1);
  cairo_line_to(cr, x + 14, y + 1);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void model_icon(cairo_t* cr, double x, double y) {
  cairo_save(cr);
  color(cr, 0.906, 0.914, 0.925);
  cairo_set_line_width(cr, 2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
  cairo_move_to(cr, x - 7, y - 4);
  cairo_line_to(cr, x, y - 10);
  cairo_line_to(cr, x + 7, y - 4);
  cairo_move_to(cr, x - 7, y + 4);
  cairo_line_to(cr, x, y + 10);
  cairo_line_to(cr, x + 7, y + 4);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void folder_icon(cairo_t* cr, double x, double y) {
  cairo_save(cr);
  color(cr, 0.906, 0.914, 0.925);
  cairo_set_line_width(cr, 1.8);
  cairo_move_to(cr, x - 10, y - 7);
  cairo_line_to(cr, x - 2, y - 7);
  cairo_line_to(cr, x + 2, y - 3);
  cairo_line_to(cr, x + 11, y - 3);
  cairo_line_to(cr, x + 9, y + 8);
  cairo_line_to(cr, x - 10, y + 8);
  cairo_close_path(cr);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void gear_icon(cairo_t* cr, double x, double y) {
  cairo_save(cr);
  color(cr, 0.906, 0.914, 0.925);
  cairo_set_line_width(cr, 1.6);
  for (int i = 0; i < 8; ++i) {
    const double angle = i * 0.7853981633974483;
    cairo_move_to(cr, x + cos(angle) * 7, y + sin(angle) * 7);
    cairo_line_to(cr, x + cos(angle) * 10, y + sin(angle) * 10);
  }
  cairo_stroke(cr);
  cairo_arc(cr, x, y, 7, 0, 6.283185307179586);
  cairo_stroke(cr);
  cairo_arc(cr, x, y, 2.5, 0, 6.283185307179586);
  cairo_stroke(cr);
  cairo_restore(cr);
}

void header_button(cairo_t* cr) {
  color(cr, 0.188, 0.204, 0.227);
  rounded_rectangle(cr, 846, 14, 30, 30, 4);
  cairo_fill(cr);
}

void close_icon(cairo_t* cr) {
  color(cr, 0.906, 0.914, 0.925);
  cairo_set_line_width(cr, 1.8);
  cairo_move_to(cr, 856, 24);
  cairo_line_to(cr, 866, 34);
  cairo_move_to(cr, 866, 24);
  cairo_line_to(cr, 856, 34);
  cairo_stroke(cr);
}

bool header_button_hit(const XButtonEvent& button) {
  return button.x >= 840 && button.x <= 882 && button.y >= 8 && button.y <= 50;
}

std::string display_name(const std::filesystem::path& path) {
  return path.stem().string();
}

void draw_knob(cairo_t* cr, const Knob& knob, float value, const char* edited = nullptr) {
  const double fraction = (value - knob.minimum) / (knob.maximum - knob.minimum);
  color(cr, 0.72, 0.74, 0.77);
  text(cr, knob.label, knob.x, knob.y - 29, 10, true);
  char display[32];
  if (edited)
    std::snprintf(display, sizeof(display), "%s_", edited);
  else if (knob.port == 7 || knob.port == 11)
    std::snprintf(display, sizeof(display), "%d%%", static_cast<int>(value * 100.0f + 0.5f));
  else if (knob.port == 14)
    std::snprintf(display, sizeof(display), "%d%%", static_cast<int>(value + 0.5f));
  else
    std::snprintf(display, sizeof(display), "%+.1f dB", value);
  if (edited) {
    color(cr, 0.188, 0.204, 0.227);
    rounded_rectangle(cr, knob.x - 31, knob.y + 22, 62, 18, 3);
    cairo_fill(cr);
    color(cr, 0.906, 0.914, 0.925);
  }
  text(cr, display, knob.x, knob.y + 35, 10, true);

  color(cr, 0.090, 0.098, 0.110);
  cairo_arc(cr, knob.x, knob.y, 18, 0, 6.283185307179586);
  cairo_fill(cr);
  cairo_set_line_width(cr, 3);
  color(cr, 0.25, 0.27, 0.30);
  cairo_arc(cr, knob.x, knob.y, 22, -3.9269908169872414, 0.7853981633974483);
  cairo_stroke(cr);
  color(cr, 0.322, 0.780, 0.647);
  cairo_arc(cr, knob.x, knob.y, 22, -3.9269908169872414,
            -3.9269908169872414 + fraction * 4.71238898038469);
  cairo_stroke(cr);
  const double angle = -3.9269908169872414 + fraction * 4.71238898038469;
  color(cr, 0.906, 0.914, 0.925);
  cairo_set_line_width(cr, 2);
  cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
  cairo_move_to(cr, knob.x + cos(angle) * 6, knob.y + sin(angle) * 6);
  cairo_line_to(cr, knob.x + cos(angle) * 14, knob.y + sin(angle) * 14);
  cairo_stroke(cr);
}

void send_path(Ui& ui, LV2_URID property, const std::string& path) {
  std::array<uint8_t, 4096> buffer{};
  lv2_atom_forge_set_buffer(&ui.forge, buffer.data(), buffer.size());
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_object(&ui.forge, &frame, 0, ui.patch_set);
  auto* object = reinterpret_cast<LV2_Atom*>(buffer.data());
  lv2_atom_forge_key(&ui.forge, ui.patch_property);
  lv2_atom_forge_urid(&ui.forge, property);
  lv2_atom_forge_key(&ui.forge, ui.patch_value);
  lv2_atom_forge_path(&ui.forge, path.c_str(), static_cast<uint32_t>(path.size() + 1));
  lv2_atom_forge_pop(&ui.forge, &frame);
  ui.write(ui.controller, 0, lv2_atom_total_size(object), ui.atom_event_transfer, object);
}

void send_cancel(Ui& ui, int module) { send_path(ui, ui.cancel_uri[module], "cancel"); }

void send_get(Ui& ui) {
  std::array<uint8_t, 256> buffer{};
  lv2_atom_forge_set_buffer(&ui.forge, buffer.data(), buffer.size());
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_object(&ui.forge, &frame, 0, ui.patch_get);
  auto* object = reinterpret_cast<LV2_Atom*>(buffer.data());
  lv2_atom_forge_pop(&ui.forge, &frame);
  ui.write(ui.controller, 0, lv2_atom_total_size(object), ui.atom_event_transfer, object);
}

void draw_scrollbar(cairo_t* cr, int x, int count, int offset) {
  constexpr int visible = 11;
  constexpr double top = 130;
  constexpr double height = 275;
  if (count <= visible) return;
  color(cr, 0.090, 0.098, 0.110);
  rounded_rectangle(cr, x, top, 6, height, 3);
  cairo_fill(cr);
  const double thumb_height = height * visible / count;
  const double thumb_y = top + (height - thumb_height) * offset / (count - visible);
  color(cr, 0.35, 0.37, 0.40);
  rounded_rectangle(cr, x, thumb_y, 6, thumb_height, 3);
  cairo_fill(cr);
}

void draw_chooser(cairo_t* cr, Ui& ui) {
  color(cr, 0.090, 0.098, 0.110);
  cairo_rectangle(cr, 0, 0, 900, 82);
  cairo_fill(cr);
  section_icon(cr, ui.chooser, 22, 25);
  color(cr, 0.906, 0.914, 0.925);
  std::string current = ui.model_paths[ui.chooser].empty()
                            ? "No model"
                            : display_name(ui.model_paths[ui.chooser]);
  text(cr, current.c_str(), 70, 47, 18);

  const int first = ui.chooser == 0 ? 0 : ui.chooser == 1 ? 3 : 6;
  Knob left = knobs[first];
  Knob right = knobs[first + 1];
  left.x = 650;
  left.y = 40;
  right.x = 755;
  right.y = 40;
  draw_knob(cr, left, ui.values[left.port],
            ui.editing_knob == first ? ui.edit_value.c_str() : nullptr);
  draw_knob(cr, right, ui.values[right.port],
            ui.editing_knob == first + 1 ? ui.edit_value.c_str() : nullptr);

  header_button(cr);
  close_icon(cr);

  color(cr, 0.188, 0.204, 0.227);
  rounded_rectangle(cr, 18, 94, 270, 318, 4);
  cairo_stroke(cr);
  rounded_rectangle(cr, 300, 94, 582, 318, 4);
  cairo_stroke(cr);
  color(cr, 0.72, 0.74, 0.77);
  text(cr, "PACK", 30, 116, 10);
  text(cr, "MODEL", 312, 116, 10);
  color(cr, 0.188, 0.204, 0.227);
  cairo_rectangle(cr, 28, 123, 248, 1);
  cairo_fill(cr);
  cairo_rectangle(cr, 310, 123, 560, 1);
  cairo_fill(cr);

  constexpr int list_y = 130;
  constexpr int row_height = 25;
  constexpr int visible_rows = 11;
  for (int row = 0; row < visible_rows; ++row) {
    const int pack_index = ui.pack_offset + row;
    if (pack_index >= static_cast<int>(ui.packs.size())) break;
    if (pack_index == ui.selected_pack) {
      color(cr, 0.188, 0.204, 0.227);
      rounded_rectangle(cr, 26, list_y + row * row_height, 254, 23, 3);
      cairo_fill(cr);
    }
    color(cr, pack_index == ui.selected_pack ? 0.906 : 0.72,
          pack_index == ui.selected_pack ? 0.914 : 0.74,
          pack_index == ui.selected_pack ? 0.925 : 0.77);
    const auto name = display_name(ui.packs[pack_index].path);
    cairo_save(cr);
    cairo_rectangle(cr, 30, list_y + row * row_height, 238, row_height);
    cairo_clip(cr);
    text(cr, name.c_str(), 34, list_y + 17 + row * row_height, 11);
    cairo_restore(cr);
  }
  draw_scrollbar(cr, 276, static_cast<int>(ui.packs.size()), ui.pack_offset);

  if (ui.selected_pack >= 0 && ui.selected_pack < static_cast<int>(ui.packs.size())) {
    const auto& models = ui.packs[ui.selected_pack].models;
    for (int row = 0; row < visible_rows; ++row) {
      const int model_index = ui.model_offset + row;
      if (model_index >= static_cast<int>(models.size())) break;
      const bool selected = ui.selected_model == model_index;
      const bool audition = ui.audition_pack == ui.selected_pack &&
                            ui.audition_model == model_index;
      const bool current = display_name(models[model_index]) ==
                           display_name(ui.model_paths[ui.chooser]);
      if (selected || audition || current) {
        color(cr, audition ? 0.16 : 0.188, audition ? 0.32 : 0.204,
              audition ? 0.29 : 0.227);
        rounded_rectangle(cr, 308, list_y + row * row_height, 566, 23, 3);
        cairo_fill(cr);
      }
      if (audition && ui.audition_error)
        color(cr, 0.90, 0.28, 0.28);
      else if (audition)
        color(cr, 0.322, 0.780, 0.647, 0.55 + 0.35 * sin(ui.pulse));
      else
        color(cr, current ? 0.906 : 0.72, current ? 0.914 : 0.74,
              current ? 0.925 : 0.77);
      const auto name = display_name(models[model_index]);
      cairo_save(cr);
      cairo_rectangle(cr, 312, list_y + row * row_height, 548, row_height);
      cairo_clip(cr);
      text(cr, name.c_str(), 316, list_y + 17 + row * row_height, 11, false,
           audition);
      cairo_restore(cr);
    }
    draw_scrollbar(cr, 870, static_cast<int>(models.size()), ui.model_offset);
  }
  if (ui.packs.empty()) {
    color(cr, 0.55, 0.57, 0.60);
    text(cr, "No packs found", 34, 153, 11);
  }
}

void draw_settings(cairo_t* cr, Ui& ui) {
  color(cr, 0.906, 0.914, 0.925);
  text(cr, "Settings", 24, 38, 22);

  header_button(cr);
  close_icon(cr);

  color(cr, 0.322, 0.780, 0.647);
  cairo_rectangle(cr, 24, 48, 852, 2);
  cairo_fill(cr);

  color(cr, 0.188, 0.204, 0.227);
  rounded_rectangle(cr, 18, 70, 864, 104, 5);
  cairo_stroke(cr);
  color(cr, 0.72, 0.74, 0.77);
  text(cr, "MODEL LIBRARY", 36, 96, 10);

  if (ui.library_save_error) color(cr, 0.28, 0.10, 0.11);
  else color(cr, 0.090, 0.098, 0.110);
  rounded_rectangle(cr, 36, 112, 680, 42, 4);
  cairo_fill(cr);
  color(cr, 0.906, 0.914, 0.925);
  cairo_save(cr);
  cairo_rectangle(cr, 48, 112, 656, 42);
  cairo_clip(cr);
  text(cr, ui.library.c_str(), 50, 138, 12);
  cairo_restore(cr);

  color(cr, 0.188, 0.204, 0.227);
  rounded_rectangle(cr, 734, 112, 130, 42, 4);
  cairo_fill(cr);
  folder_icon(cr, 756, 133);
  color(cr, 0.906, 0.914, 0.925);
  text(cr, "Browse", 779, 138, 12);

}

void draw_canvas(void* widget, void*) {
  auto* window = static_cast<Widget_t*>(widget);
  auto* ui = static_cast<Ui*>(window->parent_struct);
  cairo_t* cr = window->crb;

  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  color(cr, 0.133, 0.145, 0.165);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

  if (ui->chooser >= 0) {
    draw_chooser(cr, *ui);
    cairo_restore(cr);
    return;
  }

  if (ui->settings) {
    draw_settings(cr, *ui);
    cairo_restore(cr);
    return;
  }

  color(cr, 0.906, 0.914, 0.925);
  text(cr, "ToneLoader", 24, 38, 22);
  header_button(cr);
  gear_icon(cr, 861, 29);
  color(cr, 0.322, 0.780, 0.647);
  cairo_rectangle(cr, 24, 48, 852, 2);
  cairo_fill(cr);

  constexpr int rows[] = {62, 180, 298};
  constexpr uint32_t bypass_ports[] = {4, 8, 12};
  for (int i = 0; i < 3; ++i) {
    color(cr, 0.188, 0.204, 0.227);
    rounded_rectangle(cr, 18, rows[i], 864, 104, 5);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    color(cr, 0.133, 0.145, 0.165);
    cairo_rectangle(cr, 30, rows[i] - 10, 42, 30);
    cairo_fill(cr);
    section_icon(cr, i, 35, rows[i] - 9);

    const bool bypassed = ui->values[bypass_ports[i]] >= 0.5f;
    color(cr, bypassed ? 0.25 : 0.322, bypassed ? 0.27 : 0.780,
          bypassed ? 0.30 : 0.647);
    rounded_rectangle(cr, 28, rows[i] + 34, 44, 38, 4);
    cairo_fill(cr);
    bypass_icon(cr, 50, rows[i] + 53);

    color(cr, 0.188, 0.204, 0.227);
    rounded_rectangle(cr, 84, rows[i] + 34, 44, 38, 4);
    cairo_fill(cr);
    model_icon(cr, 106, rows[i] + 53);
    color(cr, 0.72, 0.74, 0.77);
    const std::string model = ui->model_paths[i].empty()
                                  ? (i == 1 ? "Crate Vintage Club 20" : "No model")
                                  : display_name(ui->model_paths[i]);
    text(cr, model.c_str(), 148, rows[i] + 58, 12);
  }

  for (std::size_t i = 0; i < knobs.size(); ++i)
    draw_knob(cr, knobs[i], ui->values[knobs[i].port],
              ui->editing_knob == static_cast<int>(i) ? ui->edit_value.c_str() : nullptr);
  cairo_restore(cr);
}

void redraw(Ui& ui) {
  widget_draw(ui.window, nullptr);
  XFlush(ui.app.dpy);
}

void library_selected(void* widget, void* selection) {
  auto* window = static_cast<Widget_t*>(widget);
  auto& ui = *static_cast<Ui*>(window->parent_struct);
  ui.directory_dialog_open = false;
  if (!selection) return;
  const char* path = *static_cast<char**>(selection);
  if (!path) return;
  ui.library_save_error = !toneloader::save_library(path);
  if (!ui.library_save_error) ui.library = path;
  redraw(ui);
}

void set_knob(Ui& ui, int y) {
  const auto& knob = knobs[ui.active_knob];
  float value = ui.drag_value + static_cast<float>(ui.drag_y - y) / 120.0f *
                                    (knob.maximum - knob.minimum);
  if (value < knob.minimum) value = knob.minimum;
  if (value > knob.maximum) value = knob.maximum;
  ui.values[knob.port] = value;
  ui.write(ui.controller, knob.port, sizeof(value), 0, &value);
  redraw(ui);
}

void write_control(Ui& ui, uint32_t port, float value) {
  ui.values[port] = value;
  ui.write(ui.controller, port, sizeof(value), 0, &ui.values[port]);
}

void press_knob(Ui& ui, int index, Time time, int y) {
  if (ui.last_knob == index && time - ui.last_knob_click < 300) {
    const auto port = knobs[index].port;
    write_control(ui, port, port == 7 || port == 11 ? 1.0f : port == 14 ? 100.0f : 0.0f);
    ui.active_knob = -1;
    ui.editing_knob = -1;
    ui.last_knob = -1;
    return;
  }
  ui.last_knob_click = time;
  ui.last_knob = index;
  ui.active_knob = index;
  ui.editing_knob = -1;
  ui.drag_y = y;
  ui.drag_value = ui.values[knobs[index].port];
}

uint32_t bypass_port(int module) { return module == 0 ? 4 : module == 1 ? 8 : 12; }

void close_chooser(Ui& ui, bool cancel) {
  const int module = ui.chooser;
  if (module < 0) return;
  if (cancel) send_cancel(ui, module);
  write_control(ui, bypass_port(module), ui.saved_bypass);
  ui.chooser = -1;
  ui.packs.clear();
  ui.audition_path.clear();
  ui.audition_loading = false;
  ui.audition_error = false;
  ui.commit_pending = false;
  ui.commit_path.clear();
  ui.active_knob = -1;
  ui.active_scroll = -1;
  ui.editing_knob = -1;
  ui.edit_value.clear();
  redraw(ui);
}

void open_chooser(Ui& ui, int module) {
  ui.chooser = module;
  ui.selected_pack = -1;
  ui.selected_model = -1;
  ui.pack_offset = 0;
  ui.model_offset = 0;
  ui.audition_pack = -1;
  ui.audition_model = -1;
  ui.audition_path.clear();
  ui.audition_loading = false;
  ui.audition_error = false;
  ui.commit_pending = false;
  ui.commit_path.clear();
  ui.editing_knob = -1;
  ui.edit_value.clear();
  ui.last_pack = -1;
  ui.last_model = -1;
  ui.packs = toneloader::scan_packs(toneloader::configured_library(),
      module == 0 ? toneloader::Module::pedal
                  : module == 1 ? toneloader::Module::amp : toneloader::Module::ir);
  if (module == 1)
    ui.packs.insert(ui.packs.begin(),
                    toneloader::Pack{ui.factory_model.parent_path() / "Factory", false,
                                     {ui.factory_model}});
  if (!ui.packs.empty()) ui.selected_pack = 0;
  ui.saved_bypass = ui.values[bypass_port(module)];
  if (ui.saved_bypass >= 0.5f) write_control(ui, bypass_port(module), 0.0f);
  redraw(ui);
}

std::filesystem::path selected_model(Ui& ui, int model_index) {
  if (ui.selected_pack < 0 || ui.selected_pack >= static_cast<int>(ui.packs.size())) return {};
  const auto& pack = ui.packs[ui.selected_pack];
  if (model_index < 0 || model_index >= static_cast<int>(pack.models.size())) return {};
  return pack.archive ? toneloader::extract_model(pack.path, pack.models[model_index])
                      : pack.models[model_index];
}

void select_pack(Ui& ui, int pack) {
  if (pack < 0 || pack >= static_cast<int>(ui.packs.size())) return;
  ui.selected_pack = pack;
  ui.selected_model = -1;
  ui.model_offset = 0;
  if (pack < ui.pack_offset) ui.pack_offset = pack;
  if (pack >= ui.pack_offset + 11) ui.pack_offset = pack - 10;
}

void audition_model(Ui& ui, int model) {
  if (ui.selected_pack < 0 || ui.selected_pack >= static_cast<int>(ui.packs.size()) ||
      model < 0 || model >= static_cast<int>(ui.packs[ui.selected_pack].models.size()))
    return;
  ui.selected_model = model;
  if (model < ui.model_offset) ui.model_offset = model;
  if (model >= ui.model_offset + 11) ui.model_offset = model - 10;
  const auto path = selected_model(ui, model);
  ui.audition_pack = ui.selected_pack;
  ui.audition_model = model;
  if (path.empty()) {
    ui.audition_loading = false;
    ui.audition_error = true;
  } else if (ui.audition_path != path.string()) {
    ui.audition_path = path.string();
    ui.audition_loading = true;
    ui.audition_error = false;
    send_path(ui, ui.audition_uri[ui.chooser], ui.audition_path);
  }
  redraw(ui);
}

void commit_model(Ui& ui) {
  if (ui.chooser < 0 || ui.selected_pack < 0 ||
      ui.selected_pack >= static_cast<int>(ui.packs.size()) || ui.selected_model < 0 ||
      ui.selected_model >= static_cast<int>(ui.packs[ui.selected_pack].models.size()))
    return;
  const auto path = selected_model(ui, ui.selected_model);
  if (path.empty()) {
    ui.audition_pack = ui.selected_pack;
    ui.audition_model = ui.selected_model;
    ui.audition_loading = false;
    ui.audition_error = true;
  } else {
    ui.commit_pending = true;
    ui.commit_path = path.string();
    ui.audition_loading = true;
    ui.audition_error = false;
    send_path(ui, ui.model_uri[ui.chooser], ui.commit_path);
  }
  redraw(ui);
}

void set_scroll(Ui& ui, int column, int y) {
  int count = static_cast<int>(ui.packs.size());
  if (column == 1) {
    if (ui.selected_pack < 0 || ui.selected_pack >= count) return;
    count = static_cast<int>(ui.packs[ui.selected_pack].models.size());
  }
  int maximum = count - 11;
  if (maximum < 0) maximum = 0;
  int offset = static_cast<int>((y - 130) / 275.0 * maximum + 0.5);
  if (offset < 0) offset = 0;
  if (offset > maximum) offset = maximum;
  if (column == 0) ui.pack_offset = offset;
  else ui.model_offset = offset;
  redraw(ui);
}

void button_pressed(void* widget, void* event, void*) {
  auto* window = static_cast<Widget_t*>(widget);
  auto* ui = static_cast<Ui*>(window->parent_struct);
  auto* button = static_cast<XButtonEvent*>(event);

  if (ui->settings) {
    if (button->button != Button1) return;
    if (header_button_hit(*button)) {
      ui->settings = false;
      redraw(*ui);
      return;
    }
    if (!ui->directory_dialog_open && button->x >= 734 && button->x <= 864 &&
        button->y >= 112 && button->y <= 154) {
      ui->directory_dialog_open = true;
      open_directory_dialog(ui->window, ui->library.c_str(), nullptr);
    }
    return;
  }

  if (ui->chooser >= 0) {
    if (button->button == Button4 || button->button == Button5) {
      const int direction = button->button == Button4 ? -1 : 1;
      if (button->x < 294) {
        int maximum = static_cast<int>(ui->packs.size()) - 11;
        if (maximum < 0) maximum = 0;
        ui->pack_offset += direction;
        if (ui->pack_offset < 0) ui->pack_offset = 0;
        if (ui->pack_offset > maximum) ui->pack_offset = maximum;
      } else if (ui->selected_pack >= 0 &&
                 ui->selected_pack < static_cast<int>(ui->packs.size())) {
        int maximum = static_cast<int>(ui->packs[ui->selected_pack].models.size()) - 11;
        if (maximum < 0) maximum = 0;
        ui->model_offset += direction;
        if (ui->model_offset < 0) ui->model_offset = 0;
        if (ui->model_offset > maximum) ui->model_offset = maximum;
      }
      redraw(*ui);
      return;
    }
    if (button->button != Button1) return;
    if (header_button_hit(*button)) {
      close_chooser(*ui, true);
      return;
    }
    const int first = ui->chooser == 0 ? 0 : ui->chooser == 1 ? 3 : 6;
    constexpr int header_x[] = {650, 755};
    for (int i = 0; i < 2; ++i) {
      if (button->x >= header_x[i] - 31 && button->x <= header_x[i] + 31 &&
          button->y >= 64 && button->y <= 82) {
        ui->editing_knob = first + i;
        ui->edit_value.clear();
        redraw(*ui);
        return;
      }
      if (button->x >= header_x[i] - 25 && button->x <= header_x[i] + 25 &&
          button->y >= 15 && button->y <= 65) {
        press_knob(*ui, first + i, button->time, button->y);
        return;
      }
    }
    if (button->y >= 125 && button->y <= 410 && button->x >= 270 &&
        button->x <= 288 && ui->packs.size() > 11) {
      ui->active_scroll = 0;
      set_scroll(*ui, 0, button->y);
      return;
    }
    if (button->y >= 125 && button->y <= 410 && button->x >= 864 &&
        button->x <= 882 && ui->selected_pack >= 0 &&
        ui->packs[ui->selected_pack].models.size() > 11) {
      ui->active_scroll = 1;
      set_scroll(*ui, 1, button->y);
      return;
    }
    if (button->y >= 130 && button->y < 405) {
      const int row = (button->y - 130) / 25;
      if (button->x >= 18 && button->x < 294) {
        const int pack = ui->pack_offset + row;
        if (pack >= 0 && pack < static_cast<int>(ui->packs.size())) {
          select_pack(*ui, pack);
          redraw(*ui);
        }
        return;
      }
      if (button->x >= 300 && button->x < 882 && ui->selected_pack >= 0) {
        const int model = ui->model_offset + row;
        if (ui->selected_pack >= static_cast<int>(ui->packs.size()) ||
            model >= static_cast<int>(ui->packs[ui->selected_pack].models.size()))
          return;
        const auto path = selected_model(*ui, model);
        ui->selected_model = model;
        if (path.empty()) {
          ui->audition_pack = ui->selected_pack;
          ui->audition_model = model;
          ui->audition_loading = false;
          ui->audition_error = true;
          redraw(*ui);
          return;
        }
        const bool double_click = ui->last_pack == ui->selected_pack &&
                                  ui->last_model == model &&
                                  button->time - ui->last_click < 300;
        ui->last_click = button->time;
        ui->last_pack = ui->selected_pack;
        ui->last_model = model;
        if (double_click) commit_model(*ui);
        else audition_model(*ui, model);
        return;
      }
    }
    return;
  }

  if (button->button != Button1) return;

  if (header_button_hit(*button)) {
    ui->settings = true;
    ui->library_save_error = false;
    redraw(*ui);
    return;
  }

  constexpr int rows[] = {62, 180, 298};
  constexpr uint32_t bypass_ports[] = {4, 8, 12};
  for (int i = 0; i < 3; ++i) {
    if (button->x >= 28 && button->x <= 72 && button->y >= rows[i] + 34 &&
        button->y <= rows[i] + 72) {
      const uint32_t port = bypass_ports[i];
      ui->values[port] = ui->values[port] >= 0.5f ? 0.0f : 1.0f;
      ui->write(ui->controller, port, sizeof(float), 0, &ui->values[port]);
      redraw(*ui);
      return;
    }
    if (button->x >= 84 && button->x <= 128 && button->y >= rows[i] + 34 &&
        button->y <= rows[i] + 72) {
      open_chooser(*ui, i);
      return;
    }
  }
  for (std::size_t i = 0; i < knobs.size(); ++i) {
    const auto& knob = knobs[i];
    if (button->x >= knob.x - 31 && button->x <= knob.x + 31 &&
        button->y >= knob.y + 22 && button->y <= knob.y + 42) {
      ui->editing_knob = static_cast<int>(i);
      ui->edit_value.clear();
      redraw(*ui);
      return;
    }
    if (button->x >= knob.x - 25 && button->x <= knob.x + 25 &&
        button->y >= knob.y - 25 && button->y <= knob.y + 25) {
      press_knob(*ui, static_cast<int>(i), button->time, button->y);
      return;
    }
  }
}

void pointer_moved(void* widget, void* event, void*) {
  auto* window = static_cast<Widget_t*>(widget);
  auto* ui = static_cast<Ui*>(window->parent_struct);
  auto* motion = static_cast<XMotionEvent*>(event);
  if (ui->active_scroll >= 0 && (motion->state & Button1Mask)) {
    set_scroll(*ui, ui->active_scroll, motion->y);
    return;
  }
  if (ui->active_knob >= 0 && (motion->state & Button1Mask)) set_knob(*ui, motion->y);
}

void button_released(void* widget, void*, void*) {
  auto* window = static_cast<Widget_t*>(widget);
  auto* ui = static_cast<Ui*>(window->parent_struct);
  ui->active_knob = -1;
  ui->active_scroll = -1;
}

void key_pressed(void* widget, void* event, void*) {
  auto* window = static_cast<Widget_t*>(widget);
  auto* ui = static_cast<Ui*>(window->parent_struct);
  auto* key_event = static_cast<XKeyEvent*>(event);
  const KeySym key = XLookupKeysym(key_event, 0);

  if (ui->editing_knob >= 0) {
    if (key == XK_Escape) {
      ui->editing_knob = -1;
      ui->edit_value.clear();
      redraw(*ui);
      return;
    }
    if (key == XK_BackSpace) {
      if (!ui->edit_value.empty()) ui->edit_value.pop_back();
      redraw(*ui);
      return;
    }
    if (key == XK_Return || key == XK_KP_Enter) {
      char* end = nullptr;
      const float entered = std::strtof(ui->edit_value.c_str(), &end);
      if (end != ui->edit_value.c_str() && *end == '\0') {
        const auto& knob = knobs[ui->editing_knob];
        float value = (knob.port == 7 || knob.port == 11) ? entered / 100.0f : entered;
        if (value < knob.minimum) value = knob.minimum;
        if (value > knob.maximum) value = knob.maximum;
        write_control(*ui, knob.port, value);
        ui->editing_knob = -1;
        ui->edit_value.clear();
      }
      redraw(*ui);
      return;
    }
    char characters[8]{};
    KeySym translated{};
    const int count = XLookupString(key_event, characters, sizeof(characters), &translated, nullptr);
    if (count == 1 && ((characters[0] >= '0' && characters[0] <= '9') ||
                       characters[0] == '.' || characters[0] == '+' ||
                       characters[0] == '-') && ui->edit_value.size() < 12) {
      ui->edit_value.push_back(characters[0]);
      redraw(*ui);
    }
    return;
  }

}

void canvas_events(void* widget, void* event, Xputty* app, void* user_data) {
  auto* window = static_cast<Widget_t*>(widget);
  auto* xevent = static_cast<XEvent*>(event);
  if (xevent->type == ButtonPress &&
      (xevent->xbutton.button == Button4 || xevent->xbutton.button == Button5)) {
    button_pressed(widget, &xevent->xbutton, user_data);
    return;
  }
  static_cast<Ui*>(window->parent_struct)->events(widget, event, app, user_data);
}

void configured(void* widget, void*) {
  auto* window = static_cast<Widget_t*>(widget);
  redraw(*static_cast<Ui*>(window->parent_struct));
}

LV2UI_Handle instantiate(const LV2UI_Descriptor*, const char* plugin_uri, const char* bundle,
                         LV2UI_Write_Function write, LV2UI_Controller controller,
                         LV2UI_Widget* result, const LV2_Feature* const* features) {
  if (std::strcmp(plugin_uri, TL_URI)) return nullptr;
  Window parent = 0;
  LV2UI_Resize* resize = nullptr;
  LV2_URID_Map* map = nullptr;
  for (std::size_t i = 0; features && features[i]; ++i) {
    if (!std::strcmp(features[i]->URI, LV2_UI__parent))
      parent = reinterpret_cast<Window>(features[i]->data);
    else if (!std::strcmp(features[i]->URI, LV2_UI__resize))
      resize = static_cast<LV2UI_Resize*>(features[i]->data);
    else if (!std::strcmp(features[i]->URI, LV2_URID__map))
      map = static_cast<LV2_URID_Map*>(features[i]->data);
  }
  if (!parent || !map) return nullptr;

  auto* ui = new Ui;
  ui->resize = resize;
  ui->write = write;
  ui->controller = controller;
  ui->map = map;
  const auto map_uri = [map](const char* uri) { return map->map(map->handle, uri); };
  ui->atom_event_transfer = map_uri(LV2_ATOM__eventTransfer);
  ui->atom_object = map_uri(LV2_ATOM__Object);
  ui->atom_path = map_uri(LV2_ATOM__Path);
  ui->atom_urid = map_uri(LV2_ATOM__URID);
  ui->patch_get = map_uri(LV2_PATCH__Get);
  ui->patch_set = map_uri(LV2_PATCH__Set);
  ui->patch_property = map_uri(LV2_PATCH__property);
  ui->patch_value = map_uri(LV2_PATCH__value);
  ui->model_uri = {map_uri(TL_PEDAL_MODEL), map_uri(TL_AMP_MODEL), map_uri(TL_IR_MODEL)};
  ui->audition_uri = {map_uri(TL_PEDAL_AUDITION), map_uri(TL_AMP_AUDITION),
                      map_uri(TL_IR_AUDITION)};
  ui->cancel_uri = {map_uri(TL_PEDAL_CANCEL), map_uri(TL_AMP_CANCEL), map_uri(TL_IR_CANCEL)};
  lv2_atom_forge_init(&ui->forge, map);
  ui->factory_model = std::filesystem::path(bundle) / "Crate_Vintage_Club_20.nam";
  ui->library = toneloader::configured_library();
  ui->values[4] = 1.0f;
  ui->values[7] = 1.0f;
  ui->values[11] = 1.0f;
  ui->values[12] = 1.0f;
  ui->values[14] = 100.0f;
  main_init(&ui->app);
  ui->window = create_window(&ui->app, parent, 0, 0, 900, 430);
  ui->window->parent_struct = ui;
  ui->events = ui->window->event_callback;
  ui->window->event_callback = canvas_events;
  ui->window->func.expose_callback = draw_canvas;
  ui->window->func.configure_notify_callback = configured;
  ui->window->func.button_press_callback = button_pressed;
  ui->window->func.button_release_callback = button_released;
  ui->window->func.motion_callback = pointer_moved;
  ui->window->func.key_press_callback = key_pressed;
  ui->window->func.dialog_callback = library_selected;
  XColor background{0, 0x2222, 0x2525, 0x2a2a, DoRed | DoGreen | DoBlue, 0};
  XAllocColor(ui->app.dpy,
              DefaultColormap(ui->app.dpy, DefaultScreen(ui->app.dpy)), &background);
  XSetWindowBackground(ui->app.dpy, ui->window->widget, background.pixel);
  widget_show_all(ui->window);
  redraw(*ui);
  *result = reinterpret_cast<void*>(ui->window->widget);
  if (ui->resize)
    ui->resize->ui_resize(ui->resize->handle, ui->window->width, ui->window->height);
  send_get(*ui);
  return ui;
}

void cleanup(LV2UI_Handle handle) {
  auto* ui = static_cast<Ui*>(handle);
  for (int module = 0; module < 3; ++module) send_cancel(*ui, module);
  main_quit(&ui->app);
  delete ui;
}

void port_event(LV2UI_Handle handle, uint32_t port, uint32_t size, uint32_t format,
                const void* buffer) {
  auto* ui = static_cast<Ui*>(handle);
  if (port == 1 && format == ui->atom_event_transfer && size >= sizeof(LV2_Atom_Object)) {
    const auto* object = static_cast<const LV2_Atom_Object*>(buffer);
    if (object->atom.type != ui->atom_object || object->body.otype != ui->patch_set) return;
    const LV2_Atom* property = nullptr;
    const LV2_Atom* value = nullptr;
    lv2_atom_object_get(object, ui->patch_property, &property, ui->patch_value, &value, 0);
    if (!property || property->type != ui->atom_urid || !value || value->type != ui->atom_path)
      return;
    const auto property_uri = reinterpret_cast<const LV2_Atom_URID*>(property)->body;
    const std::string path(reinterpret_cast<const char*>(value + 1));
    for (int module = 0; module < 3; ++module) {
      if (property_uri == ui->model_uri[module]) {
        ui->model_paths[module] = path;
        if (module == ui->chooser && ui->commit_pending) {
          if (path == ui->commit_path) {
            ui->saved_bypass = 0.0f;
            close_chooser(*ui, false);
            return;
          }
          ui->commit_pending = false;
          ui->audition_loading = false;
          ui->audition_error = true;
        }
      }
      if (property_uri == ui->audition_uri[module] && module == ui->chooser) {
        ui->audition_loading = false;
        ui->audition_error = path.empty();
      }
    }
    redraw(*ui);
    return;
  }
  if (format || size != sizeof(float) || port >= ui->values.size()) return;
  ui->values[port] = *static_cast<const float*>(buffer);
  redraw(*ui);
}

int idle(LV2UI_Handle handle) {
  auto* ui = static_cast<Ui*>(handle);
  run_embedded(&ui->app);
  if (ui->chooser >= 0 && ui->audition_pack >= 0 && !ui->audition_error) {
    ui->pulse += 0.12;
    redraw(*ui);
  }
  return 0;
}

const void* extension(const char* uri) {
  static const LV2UI_Idle_Interface idle_interface{idle};
  if (!std::strcmp(uri, LV2_UI__idleInterface)) return &idle_interface;
  return nullptr;
}

const LV2UI_Descriptor descriptor{TL_UI_URI, instantiate, cleanup, port_event, extension};

}  // namespace

extern "C" __attribute__((visibility("default")))
const LV2UI_Descriptor* lv2ui_descriptor(uint32_t index) { return index ? nullptr : &descriptor; }
