// REFERENCE ONLY: not yet compiled or run in the game (integration/reskate/README.md).
//
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
        {"lens", "The Century fisheye or the stock lens", "fisheye|stock|toggle"},
        {"grip", "How the handheld camera is held", "long|hip|eye|next"},
        {"lockon", "Keep the subject framed by yourself", "[on|off|toggle]"},
        {"subject", "Who to film", "<player>|next|none"},
        {"operate", "Give the pad to the camera (aim, zoom) instead of the skater", "[on|off|toggle]"},
        {"tripod", "Set up, choose and take down tripods", "place|next|setup [on|off]|remove [1-4]|<1-4>"},
        {"head", "The tripod's fluid head", "locked|manual|follow|next"},
        {"height", "The active tripod's height", "<metres>"},
        {"tape", "List the clips, or put in a new tape", "clips|new"},
        {"clean", "Hide the viewfinder readouts (keep the frame and lens)", "[on|off|toggle]"},
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
