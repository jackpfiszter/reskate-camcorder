// The `camcorder ...` console commands, registered like the trainer's (trainer_commands.cpp):
// each verb takes the rest of the line as words and runs on the game thread. Add
// register_camcorder_commands to Extension/Console/commands.h and call it where the others are.
#include "camcorder_runtime.h"
#include "Extension/Console/commands.h"
#include "camcorder/commands.h"

namespace dingosdk::console {
void register_camcorder_commands(Commands &registry) {
    struct Verb {
        const char *name, *description, *usage;
    };
    const Verb verbs[]{
        {"status", "What the camcorder is doing", nullptr},
        {"power", "Turn the camcorder on or off", "[on|off|toggle]"},
        {"rec", "REC / STBY", nullptr},
        {"stop", "Stop recording", nullptr},
        {"mode", "Film handheld or from a tripod", "handheld|tripod|toggle"},
        {"lens", "A lens on the front: the Century fisheye, one a mod adds, or none", "<id>|fisheye|stock|next|list"},
        {"rig", "In your hands, or a mod's rig (a pole cam, a board cam)", "<id>|next|list"},
        {"grip", "The same as rig (older settings say grip)", "<id>|next|list"},
        {"lockon", "Keep the subject framed by yourself", "[on|off|toggle]"},
        {"subject", "Who to film", "<player>|next|closest|none"},
        {"operate", "Give the pad to the camera (aim, zoom) instead of the skater", "[on|off|toggle]"},
        {"aim", "Aim while filming: the mouse or right stick aims while you skate", "[on|off|toggle]"},
        {"tripod", "Set up, choose and take down tripods (place again while setting up: done)", "place|done|next|setup [on|off]|remove [1-4]|<1-4>"},
        {"head", "The tripod's fluid head", "locked|manual|follow|next"},
        {"height", "Hold the camera higher or lower (or the active tripod's height)", "up|down|low|hip|eye|next|reset|<metres>"},
        {"tape", "List the clips, join them into one file, or put in a new tape", "clips|capture|new"},
        {"still", "Save a still of the camcorder's picture (Pictures\\ReSkate Camcorder)", nullptr},
        {"clean", "Hide the viewfinder readouts (keep the frame and lens)", "[on|off|toggle]"},
        {"quality", "Record in HD (1440 x 1080) or at the VX1000's own 640 x 480", "hd|dv"},
        {"sound", "Clips without sound, with the game's, or through the camera's own mic", "off|game|mic"},
        {"camera", "The camera: the VX1000, or one a mod adds", "<id>|next|list"},
        {"mods", "The cameras and lenses mods add, and what is wrong with any", "[reload]"},
        {"export", "Start your own camera mod: the camera, lens, rig and feel in use, written to Mods/My Camcorder", nullptr},
        {"warp", "The true fisheye (lines bend) or the flat stand-in", "[on|off|toggle]"},
        {"quick", "The quick menu over the viewfinder", "[toggle|open|close]"},
        {"review", "Play the last clip in the viewfinder", "[stop]"},
        {"open", "Open the clips (or stills) folder", "[clips|stills]"},
        {"guide", "Show the controls guide over the viewfinder again", "[off]"},
        {"sensitivity", "How fast the mouse and stick aim (1 as built)", "<0.25 to 4>"},
        {"invert", "Push up to look down", "[on|off|toggle]"},
        {"light", "A light on the camera for filming at night", "[on|off|toggle]"},
        {"look", "The DV look on clips and stills, or a clean picture", "dv|clean|toggle"},
        {"dv", "One part of the DV look", "fields|highlights|balance|sharpen|chroma|grain [on|off|toggle]"},
        {"date", "The camcorder's date stamp on clips and stills, and the year it says", "[on|off|toggle] | year <yyyy>|now"},
    };
    for (const auto &verb : verbs) {
        std::vector<Argument> arguments;
        if (verb.usage) {
            auto rest = argument(verb.usage, Type::text, true);
            rest.rest = true;
            arguments.push_back(std::move(rest));
        }
        auto entry = action(std::string("camcorder ") + verb.name, verb.description, Group::gameplay, std::move(arguments));
        entry.run = [name = std::string(verb.name)](const Model &, const Values &values, const Output &out) {
            std::vector<std::string> words;
            if (!values.empty())
                if (const auto *text = std::get_if<std::string>(&values[0])) {
                    std::size_t start = 0;
                    while (start < text->size()) {
                        auto end = text->find(' ', start);
                        if (end == std::string::npos) end = text->size();
                        if (end > start) words.push_back(text->substr(start, end - start));
                        start = end + 1;
                    }
                }
            out(camcorder_runtime::command(name, words));
        };
        registry.add(std::move(entry));
    }
}
} // namespace dingosdk::console
