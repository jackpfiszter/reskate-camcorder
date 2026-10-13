# The camcorder's part of ReSkate's build, kept beside its files so ReSkate's own cmake needs one
# line (cmake/Runtime.cmake includes this once dingosdk_overlay and dingosdk_runtime exist). The
# targets get ReSkate's usual options (/W4 /WX...) with every other target in the project.

# The model (Extension/Camcorder/Model): no game types, shared by the overlay and the runtime.
file(GLOB camcorder_model_sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/Extension/Camcorder/Model/camcorder/*.cpp")
add_library(dingosdk_camcorder_model STATIC ${camcorder_model_sources})
target_include_directories(dingosdk_camcorder_model PUBLIC "${PROJECT_SOURCE_DIR}/Extension/Camcorder/Model")

# What draws: the picture, the viewfinder, the page, clips, stills, the tape and review.
set(camcorder_dir "${PROJECT_SOURCE_DIR}/Extension/Camcorder")
target_sources(dingosdk_overlay PRIVATE
    "${camcorder_dir}/camcorder_overlay.cpp"
    "${camcorder_dir}/camcorder_page.cpp"
    "${camcorder_dir}/camcorder_capture.cpp"
    "${camcorder_dir}/camcorder_video.cpp"
    "${camcorder_dir}/camcorder_audio.cpp"
    "${camcorder_dir}/camcorder_media.cpp"
    "${camcorder_dir}/camcorder_tape.cpp"
    "${camcorder_dir}/camcorder_review.cpp"
    "${camcorder_dir}/camcorder_warp.cpp")
# Media Foundation for the clips, the audio endpoint API for the game's sound.
target_link_libraries(dingosdk_overlay PUBLIC dingosdk_camcorder_model mfplat mfreadwrite mfuuid mmdevapi)

# What runs on the game's tick: the camera itself and its console commands.
target_sources(dingosdk_runtime PRIVATE
    "${camcorder_dir}/camcorder_runtime.cpp"
    "${camcorder_dir}/camcorder_commands.cpp")
target_link_libraries(dingosdk_runtime PRIVATE dingosdk_camcorder_model)
