#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include <NeuralAudio/NeuralModel.h>
#include <FFTConvolver.h>
#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define TL_URI "urn:neville:toneloader"
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

constexpr std::size_t max_path = 2048;

enum Port : uint32_t {
  control, notify, audio_in, audio_out,
  pedal_bypass, pedal_input, pedal_output, pedal_quality,
  amp_bypass, amp_input, amp_output, amp_quality,
  ir_bypass, ir_input, ir_wet
};

enum class Slot : uint32_t { pedal, amp, ir };
enum class WorkKind : uint32_t { load, install, destroy };
enum class LoadTarget : uint32_t { committed, audition };

struct LoadMessage {
  WorkKind kind{WorkKind::load};
  Slot slot{};
  LoadTarget target{};
  uint32_t generation{};
  char path[max_path]{};
};

struct IrModel {
  fftconvolver::FFTConvolver convolver;
  std::vector<float> impulse;
};

struct InstallMessage {
  WorkKind kind{WorkKind::install};
  Slot slot{};
  LoadTarget target{};
  uint32_t generation{};
  void* processor{};
  bool success{};
  char path[max_path]{};
};

struct DestroyMessage {
  WorkKind kind{WorkKind::destroy};
  Slot slot{};
  void* processor{};
};

struct Uris {
  LV2_URID atom_object{}, atom_path{}, atom_urid{};
  LV2_URID patch_get{}, patch_set{}, patch_property{}, patch_value{};
  std::array<LV2_URID, 3> model{};
  std::array<LV2_URID, 3> audition{};
  std::array<LV2_URID, 3> cancel{};
};

float db_gain(float db) { return std::pow(10.0f, db * 0.05f); }

NeuralAudio::NeuralModelLoader& model_loader() {
  static NeuralAudio::NeuralModelLoader loader = [] {
    NeuralAudio::NeuralModelLoader value;
    value.SetExternalSampleRate(48000);
    value.SetDefaultMaxAudioBufferSize(8192);
    return value;
  }();
  return loader;
}

std::mutex& model_loader_mutex() {
  static std::mutex mutex;
  return mutex;
}

class Plugin {
 public:
  Plugin(double rate, const char* bundle, const LV2_Feature* const* features)
      : sample_rate_(rate),
        wet_buffer_(8192),
        factory_path_((std::filesystem::path(bundle) / "Crate_Vintage_Club_20.nam").string()) {
    for (std::size_t i = 0; features && features[i]; ++i) {
      if (!std::strcmp(features[i]->URI, LV2_URID__map)) map_ = static_cast<LV2_URID_Map*>(features[i]->data);
      if (!std::strcmp(features[i]->URI, LV2_WORKER__schedule)) schedule_ = static_cast<LV2_Worker_Schedule*>(features[i]->data);
      if (!std::strcmp(features[i]->URI, LV2_LOG__log)) log_ = static_cast<LV2_Log_Log*>(features[i]->data);
    }
    if (map_) {
      uris_.atom_object = map(LV2_ATOM__Object);
      uris_.atom_path = map(LV2_ATOM__Path);
      uris_.atom_urid = map(LV2_ATOM__URID);
      uris_.patch_get = map(LV2_PATCH__Get);
      uris_.patch_set = map(LV2_PATCH__Set);
      uris_.patch_property = map(LV2_PATCH__property);
      uris_.patch_value = map(LV2_PATCH__value);
      uris_.model = {map(TL_PEDAL_MODEL), map(TL_AMP_MODEL), map(TL_IR_MODEL)};
      uris_.audition = {map(TL_PEDAL_AUDITION), map(TL_AMP_AUDITION), map(TL_IR_AUDITION)};
      uris_.cancel = {map(TL_PEDAL_CANCEL), map(TL_AMP_CANCEL), map(TL_IR_CANCEL)};
      lv2_atom_forge_init(&forge_, map_);
    }
  }

  ~Plugin() {
    delete pedal_;
    delete amp_;
    delete ir_;
    delete pedal_audition_;
    delete amp_audition_;
    delete ir_audition_;
  }

  bool valid() const { return map_ && schedule_ && sample_rate_ == 48000.0; }

  void connect(uint32_t port, void* data) {
    if (port < ports_.size()) ports_[port] = data;
  }

  void run(uint32_t count) {
    prepare_notify();
    flush_notifications();
    handle_events();
    if (!factory_requested_ && !restored_) {
      factory_requested_ = true;
      request_load(Slot::amp, factory_path_.c_str(), LoadTarget::committed);
    }

    const auto* input = ptr<const float>(audio_in);
    auto* output = ptr<float>(audio_out);
    if (!input || !output) return;
    if (input != output) std::copy_n(input, count, output);

    process_nam(Slot::pedal, pedal_audition_ ? pedal_audition_ : pedal_, count,
                pedal_bypass, pedal_input, pedal_output, pedal_quality);
    process_nam(Slot::amp, amp_audition_ ? amp_audition_ : amp_, count,
                amp_bypass, amp_input, amp_output, amp_quality);
    process_ir(count);
  }

  static LV2_Worker_Status work(LV2_Handle instance, LV2_Worker_Respond_Function respond,
                                LV2_Worker_Respond_Handle handle, uint32_t size, const void* data) {
    if (size < sizeof(WorkKind)) return LV2_WORKER_ERR_UNKNOWN;
    auto& self = *static_cast<Plugin*>(instance);
    const auto kind = *static_cast<const WorkKind*>(data);
    if (kind == WorkKind::destroy) {
      const auto& message = *static_cast<const DestroyMessage*>(data);
      if (message.slot == Slot::ir) delete static_cast<IrModel*>(message.processor);
      else delete static_cast<NeuralAudio::NeuralModel*>(message.processor);
      return LV2_WORKER_SUCCESS;
    }
    if (kind != WorkKind::load || size != sizeof(LoadMessage)) return LV2_WORKER_ERR_UNKNOWN;

    const auto& request = *static_cast<const LoadMessage*>(data);
    InstallMessage result;
    result.slot = request.slot;
    result.target = request.target;
    result.generation = request.generation;
    std::strncpy(result.path, request.path, max_path - 1);
    try {
      if (request.slot == Slot::ir) result.processor = self.load_ir(request.path);
      else {
        const std::lock_guard lock(model_loader_mutex());
        result.processor = model_loader().CreateFromFile(request.path);
      }
      result.success = result.processor != nullptr;
    } catch (...) {
      result.success = false;
    }
    respond(handle, sizeof(result), &result);
    return LV2_WORKER_SUCCESS;
  }

  static LV2_Worker_Status response(LV2_Handle instance, uint32_t size, const void* data) {
    if (size != sizeof(InstallMessage)) return LV2_WORKER_ERR_UNKNOWN;
    auto& self = *static_cast<Plugin*>(instance);
    const auto& message = *static_cast<const InstallMessage*>(data);
    if (message.generation != self.generations_[static_cast<std::size_t>(message.target)][index(message.slot)]) {
      if (message.processor) self.destroy_later(message.slot, message.processor);
      return LV2_WORKER_SUCCESS;
    }
    if (!message.success) {
      if (message.target == LoadTarget::audition)
        self.pending_audition_notifications_[index(message.slot)] = true;
      else {
        if (!self.committed(message.slot)) self.missing_[index(message.slot)] = true;
        self.pending_model_notifications_[index(message.slot)] = true;
      }
      return LV2_WORKER_SUCCESS;
    }

    if (message.target == LoadTarget::audition) {
      void* old = self.audition(message.slot);
      self.set_audition(message.slot, message.processor);
      self.audition_paths_[index(message.slot)] = message.path;
      if (old) self.destroy_later(message.slot, old);
      self.pending_audition_notifications_[index(message.slot)] = true;
      return LV2_WORKER_SUCCESS;
    }

    void* old = self.committed(message.slot);
    self.set_committed(message.slot, message.processor);
    self.paths_[index(message.slot)] = message.path;
    self.missing_[index(message.slot)] = false;
    if (old) self.destroy_later(message.slot, old);
    self.cancel_audition(message.slot);
    self.pending_model_notifications_[index(message.slot)] = true;
    return LV2_WORKER_SUCCESS;
  }

  static LV2_State_Status save(LV2_Handle instance, LV2_State_Store_Function store,
                               LV2_State_Handle handle, uint32_t, const LV2_Feature* const*) {
    auto& self = *static_cast<Plugin*>(instance);
    for (std::size_t i = 0; i < self.paths_.size(); ++i) {
      if (!self.paths_[i].empty())
        store(handle, self.uris_.model[i], self.paths_[i].c_str(), self.paths_[i].size() + 1,
              self.uris_.atom_path, LV2_STATE_IS_POD);
    }
    return LV2_STATE_SUCCESS;
  }

  static LV2_State_Status restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
                                  LV2_State_Handle handle, uint32_t, const LV2_Feature* const*) {
    auto& self = *static_cast<Plugin*>(instance);
    self.restored_ = true;
    for (std::size_t i = 0; i < self.paths_.size(); ++i) {
      size_t size = 0; uint32_t type = 0; uint32_t flags = 0;
      const void* value = retrieve(handle, self.uris_.model[i], &size, &type, &flags);
      if (value && type == self.uris_.atom_path && size > 1 && size <= max_path)
        self.request_load(static_cast<Slot>(i), static_cast<const char*>(value),
                          LoadTarget::committed);
    }
    return LV2_STATE_SUCCESS;
  }

 private:
  static std::size_t index(Slot slot) { return static_cast<std::size_t>(slot); }
  LV2_URID map(const char* uri) const { return map_->map(map_->handle, uri); }

  template <class T> T* ptr(uint32_t port) const { return static_cast<T*>(ports_[port]); }
  float value(uint32_t port, float fallback = 0.0f) const {
    const auto* p = ptr<const float>(port);
    return p ? *p : fallback;
  }

  bool bypassed(Slot slot, uint32_t port) const {
    if (missing_[index(slot)]) return true;
    return value(port, 1.0f) >= 0.5f;
  }

  void* committed(Slot slot) const {
    if (slot == Slot::pedal) return pedal_;
    if (slot == Slot::amp) return amp_;
    return ir_;
  }

  void* audition(Slot slot) const {
    if (slot == Slot::pedal) return pedal_audition_;
    if (slot == Slot::amp) return amp_audition_;
    return ir_audition_;
  }

  void set_committed(Slot slot, void* processor) {
    if (slot == Slot::pedal) pedal_ = static_cast<NeuralAudio::NeuralModel*>(processor);
    else if (slot == Slot::amp) amp_ = static_cast<NeuralAudio::NeuralModel*>(processor);
    else ir_ = static_cast<IrModel*>(processor);
  }

  void set_audition(Slot slot, void* processor) {
    if (slot == Slot::pedal) pedal_audition_ = static_cast<NeuralAudio::NeuralModel*>(processor);
    else if (slot == Slot::amp) amp_audition_ = static_cast<NeuralAudio::NeuralModel*>(processor);
    else ir_audition_ = static_cast<IrModel*>(processor);
  }

  void destroy_later(Slot slot, void* processor) {
    DestroyMessage destroy{WorkKind::destroy, slot, processor};
    schedule_->schedule_work(schedule_->handle, sizeof(destroy), &destroy);
  }

  void cancel_audition(Slot slot) {
    ++generations_[static_cast<std::size_t>(LoadTarget::audition)][index(slot)];
    if (void* processor = audition(slot)) destroy_later(slot, processor);
    set_audition(slot, nullptr);
    audition_paths_[index(slot)].clear();
  }

  void commit(Slot slot, const char* path) {
    if (audition(slot) && audition_paths_[index(slot)] == path) {
      void* old = committed(slot);
      set_committed(slot, audition(slot));
      set_audition(slot, nullptr);
      paths_[index(slot)] = path;
      audition_paths_[index(slot)].clear();
      ++generations_[static_cast<std::size_t>(LoadTarget::audition)][index(slot)];
      missing_[index(slot)] = false;
      if (old) destroy_later(slot, old);
      write_path(slot);
      return;
    }
    cancel_audition(slot);
    request_load(slot, path, LoadTarget::committed);
  }

  void process_nam(Slot slot, NeuralAudio::NeuralModel* model, uint32_t count,
                   uint32_t bypass_port, uint32_t input_port, uint32_t output_port, uint32_t quality_port) {
    if (!model || bypassed(slot, bypass_port)) return;
    auto* buffer = ptr<float>(audio_out);
    model->SetQualityScaleFactor(value(quality_port, 1.0f));
    const float input_gain = db_gain(value(input_port) + model->GetRecommendedInputDBAdjustment());
    for (uint32_t i = 0; i < count; ++i) buffer[i] *= input_gain;
    model->Process(buffer, buffer, count);
    const float output_gain = db_gain(value(output_port) + model->GetRecommendedOutputDBAdjustment());
    for (uint32_t i = 0; i < count; ++i) buffer[i] *= output_gain;
  }

  void process_ir(uint32_t count) {
    IrModel* processor = ir_audition_ ? ir_audition_ : ir_;
    if (!processor || bypassed(Slot::ir, ir_bypass) || count > wet_buffer_.size()) return;
    auto* buffer = ptr<float>(audio_out);
    const float gain = db_gain(value(ir_input));
    for (uint32_t i = 0; i < count; ++i) wet_buffer_[i] = buffer[i] * gain;
    processor->convolver.process(wet_buffer_.data(), wet_buffer_.data(), count);
    const float wet = std::clamp(value(ir_wet, 100.0f) / 100.0f, 0.0f, 1.0f);
    for (uint32_t i = 0; i < count; ++i) buffer[i] = buffer[i] * (1.0f - wet) + wet_buffer_[i] * wet;
  }

  IrModel* load_ir(const char* path) {
    SF_INFO info{};
    SNDFILE* file = sf_open(path, SFM_READ, &info);
    if (!file || info.channels != 1 || info.samplerate != 48000 || info.frames <= 0) {
      if (file) sf_close(file);
      return nullptr;
    }
    auto result = std::make_unique<IrModel>();
    result->impulse.resize(static_cast<std::size_t>(info.frames));
    const auto read = sf_read_float(file, result->impulse.data(), info.frames);
    sf_close(file);
    if (read != info.frames || !result->convolver.init(8192, result->impulse.data(), result->impulse.size())) return nullptr;
    return result.release();
  }

  void request_load(Slot slot, const char* path, LoadTarget target) {
    if (!path || !*path || std::strlen(path) >= max_path) return;
    LoadMessage message;
    message.slot = slot;
    message.target = target;
    message.generation = ++generations_[static_cast<std::size_t>(target)][index(slot)];
    std::strncpy(message.path, path, max_path - 1);
    schedule_->schedule_work(schedule_->handle, sizeof(message), &message);
  }

  void prepare_notify() {
    auto* sequence = ptr<LV2_Atom_Sequence>(notify);
    if (!sequence) return;
    lv2_atom_forge_set_buffer(&forge_, reinterpret_cast<uint8_t*>(sequence), sequence->atom.size);
    lv2_atom_forge_sequence_head(&forge_, &notify_frame_, 0);
  }

  void flush_notifications() {
    for (std::size_t i = 0; i < paths_.size(); ++i) {
      if (pending_model_notifications_[i]) {
        pending_model_notifications_[i] = false;
        write_property_path(uris_.model[i], paths_[i]);
      }
      if (pending_audition_notifications_[i]) {
        pending_audition_notifications_[i] = false;
        write_property_path(uris_.audition[i], audition_paths_[i]);
      }
    }
  }

  void handle_events() {
    const auto* sequence = ptr<const LV2_Atom_Sequence>(control);
    if (!sequence) return;
    LV2_ATOM_SEQUENCE_FOREACH(sequence, event) {
      if (event->body.type != uris_.atom_object) continue;
      const auto* object = reinterpret_cast<const LV2_Atom_Object*>(&event->body);
      if (object->body.otype == uris_.patch_get) {
        for (std::size_t i = 0; i < paths_.size(); ++i) write_path(static_cast<Slot>(i));
        continue;
      }
      if (object->body.otype != uris_.patch_set) continue;
      const LV2_Atom* property = nullptr; const LV2_Atom* value_atom = nullptr;
      lv2_atom_object_get(object, uris_.patch_property, &property, uris_.patch_value, &value_atom, 0);
      if (!property || property->type != uris_.atom_urid) continue;
      const auto property_uri = reinterpret_cast<const LV2_Atom_URID*>(property)->body;
      for (std::size_t i = 0; i < uris_.model.size(); ++i) {
        const auto slot = static_cast<Slot>(i);
        if (property_uri == uris_.cancel[i]) {
          cancel_audition(slot);
          write_property_path(uris_.audition[i], {});
        } else if (value_atom && value_atom->type == uris_.atom_path) {
          const auto* path = reinterpret_cast<const char*>(value_atom + 1);
          if (property_uri == uris_.model[i]) commit(slot, path);
          else if (property_uri == uris_.audition[i])
            request_load(slot, path, LoadTarget::audition);
        }
      }
    }
  }

  void write_path(Slot slot) {
    write_property_path(uris_.model[index(slot)], paths_[index(slot)]);
  }

  void write_property_path(LV2_URID property, const std::string& path) {
    if (!ptr<LV2_Atom_Sequence>(notify)) return;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&forge_, 0);
    lv2_atom_forge_object(&forge_, &frame, 0, uris_.patch_set);
    lv2_atom_forge_key(&forge_, uris_.patch_property);
    lv2_atom_forge_urid(&forge_, property);
    lv2_atom_forge_key(&forge_, uris_.patch_value);
    lv2_atom_forge_path(&forge_, path.c_str(), static_cast<uint32_t>(path.size() + 1));
    lv2_atom_forge_pop(&forge_, &frame);
  }

  double sample_rate_{};
  std::array<void*, 15> ports_{};
  LV2_URID_Map* map_{};
  LV2_Worker_Schedule* schedule_{};
  LV2_Log_Log* log_{};
  Uris uris_{};
  LV2_Atom_Forge forge_{};
  LV2_Atom_Forge_Frame notify_frame_{};
  NeuralAudio::NeuralModel* pedal_{};
  NeuralAudio::NeuralModel* amp_{};
  IrModel* ir_{};
  NeuralAudio::NeuralModel* pedal_audition_{};
  NeuralAudio::NeuralModel* amp_audition_{};
  IrModel* ir_audition_{};
  std::vector<float> wet_buffer_;
  std::array<std::string, 3> paths_{};
  std::array<std::string, 3> audition_paths_{};
  std::array<bool, 3> pending_model_notifications_{};
  std::array<bool, 3> pending_audition_notifications_{};
  std::array<std::array<uint32_t, 3>, 2> generations_{};
  std::array<bool, 3> missing_{true, true, true};
  std::string factory_path_;
  bool factory_requested_{};
  bool restored_{};
};

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char* bundle,
                       const LV2_Feature* const* features) {
  auto plugin = std::make_unique<Plugin>(rate, bundle, features);
  return plugin->valid() ? plugin.release() : nullptr;
}

void connect(LV2_Handle instance, uint32_t port, void* data) { static_cast<Plugin*>(instance)->connect(port, data); }
void run(LV2_Handle instance, uint32_t count) { static_cast<Plugin*>(instance)->run(count); }
void cleanup(LV2_Handle instance) { delete static_cast<Plugin*>(instance); }

const void* extension(const char* uri) {
  static const LV2_Worker_Interface worker{Plugin::work, Plugin::response, nullptr};
  static const LV2_State_Interface state{Plugin::save, Plugin::restore};
  if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
  if (!std::strcmp(uri, LV2_STATE__interface)) return &state;
  return nullptr;
}

const LV2_Descriptor descriptor{TL_URI, instantiate, connect, nullptr, run, nullptr, cleanup, extension};

}  // namespace

extern "C" __attribute__((visibility("default")))
const LV2_Descriptor* lv2_descriptor(uint32_t index) { return index == 0 ? &descriptor : nullptr; }
