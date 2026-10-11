#pragma once
// The overlay's side of the camcorder (camcorder_overlay.cpp): the viewfinder over the game.
namespace dingosdk::overlay {
// Every presented frame: whether there is something to draw (so the overlay renders with the
// menu closed), then the drawing.
bool camcorder_pending();
void draw_camcorder();
} // namespace dingosdk::overlay
