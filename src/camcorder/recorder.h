#pragma once
// Turning the game's frames into the camcorder's video file, the parts that need no Windows: which
// part of the screen is the picture, which frames to keep for 29.97 frames a second, and the pixel
// work (the game's back buffer format to 8-bit, the fisheye housing's shadow, the size of the file,
// and NV12, the layout the H.264 encoder takes). The ReSkate side copies the pixels off the GPU and
// hands them to Windows' own H.264 encoder (Media Foundation).
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace dingosdk::camcorder {
// A rectangle of whole pixels.
struct PixelBox {
    int x{}, y{}, width{}, height{};
    bool operator==(const PixelBox &) const = default;
};
// The 4:3 picture in a `width` x `height` back buffer, centred, with even sizes (H.264 needs them).
PixelBox capture_box(int width, int height) noexcept;

enum class Quality : std::uint8_t {
    hd, // 1440 x 1080 (or the picture's own size when smaller): sharp, for sharing today
    dv, // 640 x 480: the VX1000's own resolution (720 x 480 DV shown at 4:3)
};
struct VideoSize {
    int width{}, height{};
    bool operator==(const VideoSize &) const = default;
};
VideoSize video_size(Quality, const PixelBox &capture) noexcept;
// Bits a second for the encoder: about what the picture needs at 29.97 frames a second.
std::uint32_t video_bitrate(const VideoSize &) noexcept;

// An 8-bit picture, four bytes a pixel in B, G, R, A order, rows packed.
struct Image {
    int width{}, height{};
    std::vector<std::uint8_t> pixels;
    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    }
};

// How the game's back buffer stores a pixel.
enum class SourceFormat : std::uint8_t {
    rgba8,      // DXGI_FORMAT_R8G8B8A8_UNORM
    bgra8,      // DXGI_FORMAT_B8G8R8A8_UNORM
    rgb10a2,    // DXGI_FORMAT_R10G10B10A2_UNORM, SDR
    rgb10a2_pq, // the same, holding HDR10 (PQ, BT.2020) for an HDR display
    rgba16f,    // DXGI_FORMAT_R16G16B16A16_FLOAT, scRGB (linear, 1.0 = 80 nits)
};
// Copies `rows` rows of `width` pixels from `source` (rows `pitch` bytes apart) into `out` as
// 8-bit sRGB-ish BGRA. HDR is brought down by a simple tone curve with `white_nits` as white.
void convert_to_bgra(const std::uint8_t *source, std::size_t pitch, int width, int rows, SourceFormat format,
                     Image &out, float white_nits = 200.0f);
// Bilinear resize of `in` into `out` (already sized).
void scale_bgra(const Image &in, Image &out);
// The fisheye adapter's housing: black outside a circle of `radius` half picture widths
// (vx1000.h, Optics::vignette_radius) with a soft edge; 0 leaves the picture alone.
void apply_vignette(Image &image, float radius);
// NV12 for H.264: a full-size luma plane, then interleaved half-size U, V. BT.709, limited range.
void bgra_to_nv12(const Image &image, std::vector<std::uint8_t> &out);

// Which presented frames become video frames, for a steady 30000/1001 a second whatever the game's
// frame rate: a frame is due when the clock passes the next output frame's time. After a hitch
// the frame shown is repeated (at most `max_repeat` times) so the clip keeps time with the clock.
class FramePacer {
public:
    explicit FramePacer(int max_repeat = 4) : max_repeat_(max_repeat) {}
    void start(double seconds) noexcept;
    // How many video frames the frame shown at `seconds` should become (0: none).
    int due(double seconds) noexcept;
    std::int64_t frames() const noexcept { return emitted_; }

private:
    double start_{};
    std::int64_t emitted_{};
    int max_repeat_;
    bool started_{};
};

// "ReSkate 2026-10-12 14-03-22 clip 03.mp4": sorts by when it was shot, says which clip on the tape.
std::string clip_file_name(std::uint32_t clip, const std::tm &local_time);
} // namespace dingosdk::camcorder
