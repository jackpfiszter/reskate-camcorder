#include "camcorder.h"

namespace dingosdk::camcorder {
namespace {
std::string_view head_name(TripodRig::Head head) {
    switch (head) {
    case TripodRig::Head::locked: return "locked off";
    case TripodRig::Head::manual: return "manual pan";
    case TripodRig::Head::follow: return "follow";
    }
    return "follow";
}
} // namespace

Camcorder::Camcorder(Settings settings)
    : settings_(settings), handheld_(settings.handheld), deck_(settings.deck) {
    deck_.settings().tape_seconds = settings_.camera.tape_minutes * 60;
}

void Camcorder::set_camera(const CameraProfile &camera) {
    settings_.camera = camera;
    deck_.settings().tape_seconds = camera.tape_minutes * 60;
    message_ = camera.name + ".";
}

void Camcorder::power(bool on) {
    if (powered_ == on) return;
    powered_ = on;
    if (!on) {
        deck_.stop();
        tripod_setup_ = false;
    }
    message_ = on ? "Camera on." : "Camera off.";
}

void Camcorder::toggle_record() {
    if (!powered_) power(true);
    if (deck_.recording()) {
        deck_.stop();
        message_ = "STBY";
        return;
    }
    if (tripod_setup_) tripod_setup_ = false; // rolling means the setup is done
    std::string reason;
    message_ = deck_.record(shot_label(), &reason) ? "REC" : reason;
}

void Camcorder::set_mode(Mode mode) {
    if (mode == Mode::tripod && !active()) {
        // Pick any standing tripod; with none, stay handheld.
        for (int i = 0; i < static_cast<int>(tripod_slots); ++i)
            if (tripods_[i]) { active_tripod_ = i; break; }
        if (!active()) {
            message_ = "No tripod set up.";
            return;
        }
    }
    mode_ = mode;
    if (mode == Mode::handheld) tripod_setup_ = false;
}

void Camcorder::set_head(TripodRig::Head head) {
    settings_.tripod.head = head;
    if (auto *t = active()) t->settings().head = head;
}

int Camcorder::place_tripod(const Filmer &filmer) {
    if (!filmer.valid || !finite(filmer.position)) {
        message_ = "Nowhere to stand a tripod.";
        return -1;
    }
    int slot = -1;
    for (int i = 0; i < static_cast<int>(tripod_slots) && slot < 0; ++i)
        if (!tripods_[i]) slot = i;
    if (slot < 0) slot = active_tripod_ >= 0 ? active_tripod_ : 0;
    const auto forward = normalize({filmer.forward[0], 0, filmer.forward[2]}, {0, 0, 1});
    // A step ahead of the filmer, pointing where the handheld camera points (or ahead).
    auto aim = handheld_.aim();
    if (!powered_ || mode_ != Mode::handheld) aim = {std::atan2(forward[0], forward[2]), 0};
    TripodRig tripod(settings_.tripod);
    tripod.place(add(filmer.position, mul(forward, 0.8f)), aim);
    tripods_[slot] = tripod;
    active_tripod_ = slot;
    power(true);
    mode_ = Mode::tripod;
    tripod_setup_ = true;
    message_ = "Tripod " + std::to_string(slot + 1) + " set up.";
    return slot;
}

void Camcorder::remove_tripod(int slot) {
    if (slot < 0 || slot >= static_cast<int>(tripod_slots) || !tripods_[slot]) return;
    tripods_[slot].reset();
    if (slot == active_tripod_) {
        active_tripod_ = -1;
        tripod_setup_ = false;
        if (mode_ == Mode::tripod) {
            set_mode(Mode::tripod); // to another tripod, if one stands
            if (!active()) mode_ = Mode::handheld;
        }
    }
    message_ = "Tripod " + std::to_string(slot + 1) + " taken down.";
}

void Camcorder::next_tripod() {
    for (int step = 1; step <= static_cast<int>(tripod_slots); ++step) {
        const int i = (std::max(active_tripod_, 0) + step) % static_cast<int>(tripod_slots);
        if (tripods_[i]) {
            select_tripod(i);
            return;
        }
    }
    message_ = "No tripod set up.";
}

void Camcorder::select_tripod(int slot) {
    if (slot < 0 || slot >= static_cast<int>(tripod_slots) || !tripods_[slot]) {
        message_ = "No tripod in that slot.";
        return;
    }
    active_tripod_ = slot;
    tripod_setup_ = false;
    mode_ = Mode::tripod;
    message_ = "Tripod " + std::to_string(slot + 1) + ".";
}

void Camcorder::set_tripod_setup(bool on) {
    if (on && !active()) {
        message_ = "No tripod set up.";
        return;
    }
    tripod_setup_ = on;
    if (on) mode_ = Mode::tripod;
}

void Camcorder::set_tripod_height(int slot, float metres) {
    if (slot < 0 || slot >= static_cast<int>(tripod_slots) || !tripods_[slot] || !std::isfinite(metres)) return;
    tripods_[slot]->settings().height = std::clamp(metres, TripodRig::lowest, TripodRig::highest);
}

const TripodRig *Camcorder::tripod(int slot) const {
    return slot >= 0 && slot < static_cast<int>(tripod_slots) && tripods_[slot] ? &*tripods_[slot] : nullptr;
}

std::string Camcorder::shot_label() const {
    const auto &adapter = settings_.adapter;
    const std::string lens = !adapter ? "long lens" : adapter->fisheye ? "fisheye" : adapter->name;
    if (mode_ == Mode::tripod && active_tripod_ >= 0 && tripods_[active_tripod_])
        return "Tripod " + std::to_string(active_tripod_ + 1) + ", " +
               std::string(head_name(tripods_[active_tripod_]->settings().head)) + ", " + lens;
    return "Handheld, " + std::string(grip_name(handheld_.settings().grip)) + ", " + lens;
}

Frame Camcorder::tick(const TickInput &in) {
    const float dt = std::isfinite(in.dt) ? std::clamp(in.dt, 0.0f, 0.25f) : 0.0f;
    Frame frame;
    if (powered_) {
        zoom_.step(in.controls.zoom, dt, settings_.camera.lens);
        frame.optics = optics(settings_.adapter ? &*settings_.adapter : nullptr, zoom_.position, settings_.fisheye_render,
                              in.render_aspect, settings_.camera.lens);
        const Subject *subject = in.subject ? &*in.subject : nullptr;
        if (mode_ == Mode::tripod && !active()) mode_ = Mode::handheld;
        if (mode_ == Mode::tripod) {
            auto &t = *active();
            if (tripod_setup_) {
                t.setup(in.controls, in.ground_under_tripod, dt, frame.optics.zoom_ratio);
                frame.shot.camera = camera_matrix(t.lens(), t.aim().yaw, t.aim().pitch);
                frame.shot.valid = valid_camera_matrix(frame.shot.camera);
            } else {
                // Nobody picked: a tripod films the player who set it up (solo, the only skater).
                Subject self;
                if (!subject && in.filmer.valid) {
                    self = {true, in.filmer.position, in.filmer.velocity};
                    subject = &self;
                }
                frame.shot = t.step(subject, in.controls, dt, frame.optics.zoom_ratio);
            }
        } else {
            frame.shot = handheld_.step(in.filmer, subject, in.controls, dt, frame.optics.zoom_ratio);
        }
    }
    const bool was_recording = deck_.recording();
    deck_.tick(dt, powered_);
    if (was_recording && !deck_.recording() && !deck_.stopped_because().empty()) message_ = deck_.stopped_because();
    frame.status = status();
    return frame;
}

Status Camcorder::status() const {
    Status s;
    s.powered = powered_;
    s.mode = mode_;
    s.tripod_setup = tripod_setup_;
    s.active_tripod = active_tripod_;
    for (std::size_t i = 0; i < tripod_slots; ++i) s.tripods[i] = tripods_[i].has_value();
    s.transport = deck_.transport();
    s.counter_seconds = deck_.tape_seconds();
    s.tape_remaining = deck_.tape_remaining();
    s.battery = deck_.battery_fraction();
    if (const auto *clip = deck_.current_clip()) s.clip = clip->number;
    s.zoom = zoom_.position;
    s.zoom_bar = zoom_.shown_for > 0;
    s.zoom_ratio = focal_length(zoom_.position, settings_.camera.lens) / settings_.camera.lens.wide_mm;
    s.camera_id = settings_.camera.id;
    s.camera_name = settings_.camera.name;
    s.lens_id = settings_.adapter ? settings_.adapter->id : std::string();
    s.lens_name = settings_.adapter ? settings_.adapter->name : "stock lens";
    s.fisheye = settings_.adapter && settings_.adapter->fisheye;
    s.grip = handheld_.settings().grip;
    s.lock_on = handheld_.settings().lock_on;
    s.head = active_tripod_ >= 0 && tripods_[active_tripod_] ? tripods_[active_tripod_]->settings().head
                                                             : settings_.tripod.head;
    s.message = message_;
    return s;
}
} // namespace dingosdk::camcorder
