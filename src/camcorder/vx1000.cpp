#include "vx1000.h"

namespace dingosdk::camcorder {
namespace {
constexpr float widest_ray = 85.0f * degrees; // a flat render cannot reach 90 degrees off-axis
constexpr float widest_render = 150.0f;       // degrees: past this a flat render falls apart
constexpr float widest_approximate = 100.0f;  // degrees: the most a flat stand-in is given
} // namespace

const Adapter &century_fisheye() {
    static const Adapter century{"century", "Century Optics 0.3x fisheye", true, 125.0f, Projection::equidistant, 1, 1.08f, 0.35f, {}};
    return century;
}

float focal_length(float zoom, const LensSpec &lens) noexcept {
    const float z = std::isfinite(zoom) ? std::clamp(zoom, 0.0f, 1.0f) : 0.0f;
    return lens.wide_mm * std::pow(lens.tele_mm / lens.wide_mm, z);
}

float rectilinear_fov(float extent_mm, float focal_mm) noexcept {
    if (!(extent_mm > 0) || !(focal_mm > 0)) return 0;
    return 2 * std::atan(extent_mm * 0.5f / focal_mm) / degrees;
}

float fisheye_angle(Projection projection, float rho, float half) noexcept {
    switch (projection) {
    case Projection::equisolid: return 2 * std::asin(std::min(rho * std::sin(half * 0.5f), 1.0f));
    case Projection::stereographic: return 2 * std::atan(rho * std::tan(half * 0.5f));
    case Projection::orthographic: return std::asin(std::min(rho * std::sin(std::min(half, pi * 0.5f)), 1.0f));
    case Projection::equidistant: break;
    }
    return rho * half;
}

Tangent fisheye_source(const Optics &o, float x, float y) noexcept {
    const float half = o.horizontal_fov * 0.5f * degrees;
    if (!o.fisheye) {
        const float t = std::tan(std::min(half, widest_ray));
        return {x * t, y * t};
    }
    const float rho = std::sqrt(x * x + y * y);
    if (!(rho > 1e-6f)) return {};
    const float t = std::tan(std::min(fisheye_angle(o.projection, rho, half), widest_ray)) / rho;
    return {x * t, y * t};
}

TextureUv warp_uv(const Optics &o, float x, float y, float render_aspect) noexcept {
    const float aspect = std::isfinite(render_aspect) && render_aspect > 0.2f ? render_aspect : 16.0f / 9.0f;
    const float tall = std::tan(std::clamp(o.render_vertical_fov, 1.0f, 179.0f) * 0.5f * degrees);
    const auto t = fisheye_source(o, x, y);
    return {0.5f + 0.5f * t.x / (tall * aspect), 0.5f - 0.5f * t.y / tall};
}

Optics optics(const Adapter *adapter, float zoom, FisheyeRender render, float render_aspect, const LensSpec &lens) noexcept {
    Optics o;
    o.aspect = std::clamp(lens.aspect(), 0.25f, 4.0f);
    // The picture's top edge and its corner, in half picture widths.
    const float picture_half_height = 1 / o.aspect;
    const float picture_corner = std::sqrt(1 + picture_half_height * picture_half_height);
    o.focal_mm = focal_length(zoom, lens);
    o.zoom_ratio = o.focal_mm / lens.wide_mm;
    const auto shadow = [&](float radius, float growth) {
        const float vignette = radius > 0 ? radius + (o.zoom_ratio - 1) * growth : 0;
        return vignette > 0 && vignette < picture_corner ? vignette : 0.0f;
    };
    if (!adapter || !adapter->fisheye) {
        // A flat converter scales the focal length (a 0.5x wide halves it).
        const float magnification = adapter && adapter->magnification > 0 ? adapter->magnification : 1.0f;
        o.horizontal_fov = rectilinear_fov(lens.sensor_width_mm, o.focal_mm * magnification);
        o.vertical_fov = rectilinear_fov(lens.sensor_height_mm, o.focal_mm * magnification);
        if (adapter) o.vignette_radius = shadow(adapter->vignette_radius, adapter->vignette_growth);
        // The picture is cut from the middle of the game's, top to bottom when the game's is wider:
        // the game's vertical FOV is the picture's. A picture wider than the game's is cut from its
        // whole width, so the game's camera opens until its width covers the picture's.
        const float aspect = std::isfinite(render_aspect) && render_aspect > 0.2f ? render_aspect : 16.0f / 9.0f;
        const float across = std::tan(o.horizontal_fov * 0.5f * degrees) / aspect;
        o.render_vertical_fov = std::max(o.vertical_fov, 2 * std::atan(across) / degrees);
        return o;
    }
    // Equidistant: the angle shrinks in step with zoom, and is linear in the distance from centre.
    o.fisheye = true;
    o.projection = adapter->projection;
    o.horizontal_fov = adapter->fisheye_fov / o.zoom_ratio;
    o.vertical_fov = o.horizontal_fov * picture_half_height;
    o.vignette_radius = shadow(adapter->vignette_radius, adapter->vignette_growth);
    if (render == FisheyeRender::approximate) {
        o.render_vertical_fov = std::min(o.vertical_fov, widest_approximate);
        return o;
    }
    o.warped = true;
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
