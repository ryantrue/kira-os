idf_component_register(SRCS "app.cpp" INCLUDE_DIRS "include" REQUIRES kira_platform brookesia_system_core brookesia_gui_lvgl)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
