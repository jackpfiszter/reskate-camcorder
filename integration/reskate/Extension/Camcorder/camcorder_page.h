#pragma once
// The CAMCORDER page of the ReSkate menu (Insert). It only sends `camcorder ...` console commands,
// as the trainer's page does, and reads what the camcorder published (camcorder_runtime.h).
#include "Extension/UI/Overlay/skate_menu.h"

namespace dingosdk::overlay::menu {
void camcorder_page(SkateMenu &, const Model &, const CallbacksV3 &);
}
