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
        {"rig", "Hold the camera in your hand, or fix it to the board (the board cam)", "handheld|board|toggle"},
        {"operate", "Full camera control (the skater waits; roll too), or filming while skating", "[on|off|toggle]"},
        {"lockon", "Keep the subject framed by yourself", "[on|off|toggle]"},
        {"subject", "Who to film: a name (or its start, any case), an id, or", "<player>|next|closest|none"},
        {"frame", "Where the camera points on who it films", "board|body|head|next"},
        {"tripod", "Set up, choose and take down tripods (place again while setting up: done)", "place|done|next|setup [on|off]|remove [1-4]|<1-4>"},
        {"head", "The tripod's head: locked off, or following who it films", "locked|follow|next"},
        {"height", "Hold the camera higher or lower (or the active tripod's height)", "up|down|reset|<metres>"},
        {"tape", "List the clips, join them into one file, or put in a new tape", "clips|capture|new"},
        {"still", "Save a still of the camcorder's picture (Videos\\ReSkate Camcorder\\Stills)", nullptr},
        {"clean", "Hide the viewfinder readouts (keep the frame and lens)", "[on|off|toggle]"},
        {"sound", "Clips without sound, or with the game's as the camera hears it", "off|game"},
        {"camera", "The camera: the VX1000, or one a mod adds", "<id>|next|list"},
        {"mods", "The cameras and lenses mods add, and what is wrong with any", "[reload]"},
        {"effects", "The camera's effects, in order, or every effect a camera can use", "[list]"},
        {"export", "Start your own camera mod: the camera, lens and feel in use, written to Mods/My Camcorder", nullptr},
        {"quick", "The quick menu over the viewfinder", "[toggle|open|close]"},
        {"review", "Play the last clip in the viewfinder", "[stop]"},
        {"open", "Open the clips (or stills) folder", "[clips|stills]"},
        {"guide", "Show the controls guide over the viewfinder again", "[off]"},
        {"sensitivity", "How fast the mouse and stick aim (1 as built)", "<0.25 to 4>"},
        {"invert", "Push up to look down", "[on|off|toggle]"},
        {"look", "The DV look on clips and stills, or a clean picture", "dv|clean|toggle"},
        {"dv", "One part of the DV look", "fields|highlights|balance|sharpen|chroma|grain [on|off|toggle]"},
        {"date", "The camcorder's date stamp, the year it says, or its clock set to a date and time", "[on|off|toggle] | year <yyyy>|now | set <YYYY-MM-DD> [HH:MM]|now"},
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
