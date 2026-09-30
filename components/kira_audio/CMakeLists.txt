idf_component_register(
    SRCS "playback_device.cpp" "voice_device.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
# The HAL device registers itself through a static plugin registrar; keep it linked.
target_link_libraries(${COMPONENT_LIB} PRIVATE "-u kira_playback_device_symbol")
target_link_libraries(${COMPONENT_LIB} PRIVATE "-u kira_voice_device_symbol")
