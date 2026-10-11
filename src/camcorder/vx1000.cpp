#include "vx1000.h"

namespace dingosdk::camcorder {
namespace {
constexpr float picture_corner = 1.25f; // the 4:3 picture's corner, in half picture widths
constexpr float picture_half_height = 0.75f;
constexpr float widest_ray = 85.0f * degrees; // a flat render cannot reach 90 degrees off-axis
constexpr float widest_render = 150.0f;       // degrees: past this a flat render falls apart
constexpr float widest_approximate = 100.0f;  // degrees: the most a flat stand-in is given
} // namespace

AdapterSpec adapter_spec(Adapter adapter) noexcept {
    switch (adapter) {
    case Adapter::century_fisheye:
        // 125 degrees across is Century's figure. The shadow's size is set by eye from MK1 footage:
        // round black corners at full wide that are gone by about 1.5x.
        return {"Century Optics 0.3x fisheye", true, 125.0f, 1.08f, 0.35f};
    case Adapter::none:
    default:
        return {"Stock lens", false, 0, 0, 0};
    }
}
std::string_view adapter_name(Adapter adapter) noexcept { return adapter_spec(adapter).name; }

float focal_length(float zoom, const LensSpec &lens) noexcept {
    const float z = std::isfinite(zoom) ? std::clamp(zoom, 0.0f, 1.0f) : 0.0f;
    return lens.wide_mm * std::pow(lens.tele_mm / lens.wide_mm, z);
}

float rectilinear_fov(float extent_mm, float focal_mm) noexcept {
    if (!(extent_mm > 0) || !(focal_mm > 0)) return 0;
    return 2 * std::atan(extent_mm * 0.5f / focal_mm) / degrees;
}

Tangent fisheye_source(const Optics &o, float x, float y) noexcept {
    const float half = o.horizontal_fov * 0.5f * degrees;
    if (!o.fisheye) {
        const float t = std::tan(std::min(half, widest_ray));
        return {x * t, y * t};
    }
    const float rho = std::sqrt(x * x + y * y);
    if (!(rho > 1e-6f)) return {};
    const float t = std::tan(std::min(rho * half, widest_ray)) / rho;
    return {x * t, y * t};
}

Optics optics(Adapter adapter, float zoom, FisheyeRender render, float render_aspect, const LensSpec &lens) noexcept {
    Optics o;
    o.focal_mm = focal_length(zoom, lens);
    o.zoom_ratio = o.focal_mm / lens.wide_mm;
    const auto spec = adapter_spec(adapter);
    if (!spec.fisheye) {
        o.horizontal_fov = rectilinear_fov(lens.sensor_width_mm, o.focal_mm);
        o.vertical_fov = rectilinear_fov(lens.sensor_height_mm, o.focal_mm);
        // The 4:3 picture is cut from the middle of the game's wider one, top to bottom: the game's
        // vertical FOV is the picture's.
        o.render_vertical_fov = o.vertical_fov;
        return o;
    }
    // Equidistant: the angle shrinks in step with zoom, and is linear in the distance from centre.
    o.fisheye = true;
    o.horizontal_fov = spec.horizontal_fov_deg / o.zoom_ratio;
    o.vertical_fov = o.horizontal_fov * picture_half_height;
    const float vignette = spec.vignette_radius > 0 ? spec.vignette_radius + (o.zoom_ratio - 1) * spec.vignette_growth : 0;
    o.vignette_radius = vignette > 0 && vignette < picture_corner ? vignette : 0;
    if (render == FisheyeRender::approximate) {
        o.render_vertical_fov = std::min(o.vertical_fov, widest_approximate);
        return o;
    }
    // The warp samples the flat render at fisheye_source(); open the flat camera until every point
    // of the picture left uncovered by the housing's shadow is inside it. The ray's tangent over
    // the distance from centre only grows outward, so the edge of that region is enough to check.
    const float aspect = std::isfinite(render_aspect) && render_aspect > 0.2f ? render_aspect : 16.0f / 9.0f;
    const float radius = o.vignette_radius > 0 ? o.vignette_radius : picture_corner;
    float most_x = 0, most_y = 0;
    const auto visit = [&](float x, float y) {
        if (std::abs(x) > 1.0001f || std::abs(y) > picture_half_height + 1e-4f || x * x + y * y > radius * radius * 1.0001f) return;
        const auto t = fisheye_source(o, x, y);
        most_x = std::max(most_x, std::abs(t.x));
        most_y = std::max(most_y, std::abs(t.y));
    };
    constexpr int steps = 128;
    for (int i = 0; i <= steps; ++i) {
        const float s = static_cast<float>(i) / steps;
        visit(-1 + 2 * s, picture_half_height); // the picture's top and side edges (it is symmetric)
        visit(1, -picture_half_height + 2 * picture_half_height * s);
        const float a = s * 0.5f * pi; // and the shadow's edge, one quarter
        visit(radius * std::cos(a), radius * std::sin(a));
    }
    // The tangents peak where the shadow's edge meets the picture's: visit those exactly.
    if (radius > picture_half_height) visit(std::sqrt(radius * radius - picture_half_height * picture_half_height), picture_half_height);
    if (radius > 1) visit(1, std::sqrt(radius * radius - 1));
    const float vertical = 2 * std::atan(std::max(most_y, most_x / aspect)) / degrees;
    o.render_vertical_fov = std::clamp(vertical, o.vertical_fov, widest_render);
    return o;
}

void ZoomServo::step(float rocker, float dt, const LensSpec &lens) noexcept {
    constexpr float deadzone = 0.08f, motor_lag = 0.08f, bar_seconds = 2.0f;
    if (!std::isfinite(dt) || dt <= 0) return;
    const float r = std::isfinite(rocker) ? std::clamp(rocker, -1.0f, 1.0f) : 0.0f;
    float target = 0;
    if (std::abs(r) > deadzone) {
        const float press = (std::abs(r) - deadzone) / (1 - deadzone);
        const float travel = lens.slowest_zoom_s + (lens.fastest_zoom_s - lens.slowest_zoom_s) * press;
        target = (r > 0 ? 1.0f : -1.0f) / travel;
    }
    speed = damp(speed, target, motor_lag, dt);
    position += speed * dt;
    if (position <= 0 || position >= 1) {
        position = std::clamp(position, 0.0f, 1.0f);
        speed = 0;
    }
    shown_for = std::abs(r) > deadzone || std::abs(speed) > 1e-3f ? bar_seconds : std::max(0.0f, shown_for - dt);
}
} // namespace dingosdk::camcorder
