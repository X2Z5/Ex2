cmake_minimum_required(VERSION 3.22)
project(EX2Voice VERSION 1.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# ---- JUCE -------------------------------------------------------------------
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ---- ONNX Runtime (headers only; the shared lib is dlopen'd at runtime) -----
set(ORT_ROOT "" CACHE PATH "ONNX Runtime root containing include/ and lib/")
if(NOT EXISTS "${ORT_ROOT}/include")
    message(FATAL_ERROR "Set -DORT_ROOT to an extracted onnxruntime package (needs include/ + lib/)")
endif()

# ---- llama.cpp (static, CPU-only, portable) ----------------------------------
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(LLAMA_CURL           OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE          OFF CACHE BOOL "" FORCE)
set(GGML_METAL           OFF CACHE BOOL "" FORCE)
set(GGML_BLAS            OFF CACHE BOOL "" FORCE)
FetchContent_Declare(llama
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG        b9596
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(llama)

# ---- the plugin ---------------------------------------------------------------
juce_add_plugin(EX2Voice
    PRODUCT_NAME "EX2 Voice"
    COMPANY_NAME "EX2SEE"
    BUNDLE_ID com.ex2see.ex2voice
    PLUGIN_MANUFACTURER_CODE Ex2s
    PLUGIN_CODE Exvo
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(EX2Voice PRIVATE
    Source/Plugin/PluginMain.cpp
    Source/AI/LlamaCppService.cpp)

target_include_directories(EX2Voice PRIVATE
    Source
    "${ORT_ROOT}/include")

target_compile_definitions(EX2Voice PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EX2Voice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_data_structures
        llama
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
