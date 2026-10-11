#pragma once
// The `camcorder ...` console commands. As with ReSkate's trainer, every change goes through
// these: the menu sends them, and a player binds them to buttons (Controls > bindings), so one
// parser serves all three. Runs on the game thread, where the camcorder lives.
//
//   camcorder status
//   camcorder power [on|off|toggle]
//   camcorder rec                       REC / STBY
//   camcorder stop
//   camcorder mode handheld|tripod|toggle
//   camcorder lens fisheye|stock|toggle
//   camcorder grip long|hip|eye|next
//   camcorder lockon [on|off|toggle]
//   camcorder tripod place|next|setup [on|off|toggle]|remove [1-4]|<1-4>
//   camcorder head locked|manual|follow|next
//   camcorder height <metres>           the active tripod's
//   camcorder tape new|clips
//
// `camcorder subject ...` needs the players in the session and is answered by the ReSkate side.
#include "camcorder.h"
#include <string>
#include <string_view>
#include <vector>

namespace dingosdk::camcorder {
struct CommandContext {
    Filmer filmer; // where a new tripod goes
};
// The verbs this parser knows, for the console's completion and help.
const std::vector<std::string_view> &command_verbs();
// Runs `verb` with its words and returns the line to print.
std::string run_command(Camcorder &, std::string_view verb, const std::vector<std::string> &words,
                        const CommandContext & = {});
// A one-line summary of the camcorder, as `camcorder status` prints it.
std::string describe(const Status &);
} // namespace dingosdk::camcorder
