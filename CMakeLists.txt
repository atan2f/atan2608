# Mandatory line, sets the minimum version of CMake that should be used with this repository.
# 3.22 is the minimum CMake version required by JUCE.
# 3.25 is required for the MSVC_DEBUG_INFORMATION_FORMAT feature to work.
# To verify your version run
# $ cmake --version
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

set(CMAKE_POLICY_DEFAULT_CMP0141
    NEW
    CACHE STRING "" FORCE
)

# macOS 10.11 is the minimum required by JUCE
set(CMAKE_OSX_DEPLOYMENT_TARGET
    "10.11"
    CACHE STRING "" FORCE
)
set(CMAKE_OSX_ARCHITECTURES
    "x86_64;arm64"
    CACHE STRING "" FORCE
)

# Read the version from version.txt so a bump lives in one place and CI can key
# off it. Read before project() so it flows into PROJECT_VERSION.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" ATAN_VERSION_RAW LIMIT_COUNT 1)
string(STRIP "${ATAN_VERSION_RAW}" ATAN_VERSION_RAW)
if(NOT ATAN_VERSION_RAW MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "version.txt must contain a single MAJOR.MINOR.PATCH line, got: '${ATAN_VERSION_RAW}'")
endif()
# Re-run CMake when version.txt changes so a bump triggers reconfiguration.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt")

# Sets a few variables, like PROJECT_NAME. VERSION is needed by JUCE.
project(AudioPlugin VERSION ${ATAN_VERSION_RAW})

# Set the C++ standard only if this project is standalone;
# otherwise, let clients decide.
if(PROJECT_IS_TOP_LEVEL)
  if(NOT CMAKE_CXX_STANDARD)
    # Always use the newest C++ standard on green-field projects if possible.
    set(CMAKE_CXX_STANDARD 23)
  endif()

  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
      "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
      CACHE STRING "" FORCE
  )
endif()

# I like to download the dependencies to the same folder as the project.
# If you want to install them system wide, set CPM_SOURCE_CACHE with the path to the dependencies
# either as an environment variable or pass it to the cmake script with -DCPM_SOURCE_CACHE=<path>.
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libs)

# Downloads CPM if not already downloaded. CPM is an easy-to-use package manager nicely integrated with CMake.
include(cmake/cpm.cmake)

# This commands downloads AND configures JUCE. It sets up some variables, like JUCE_SOURCE_DIR.
cpmaddpackage(
  NAME
  JUCE
  GIT_TAG
  8.0.12
  GITHUB_REPOSITORY
  juce-framework/JUCE
  SOURCE_DIR
  ${LIB_DIR}/juce
)

# Adds googletest.
cpmaddpackage(
  NAME
  GOOGLETEST
  GITHUB_REPOSITORY
  google/googletest
  VERSION
  1.17.0
  SOURCE_DIR
  ${LIB_DIR}/googletest
  OPTIONS
  "INSTALL_GTEST OFF"
  "gtest_force_shared_crt ON"
)

# Add compiler warning utilities
include(cmake/CompilerWarnings.cmake)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Sets the Windows VST3 installation location to one
  # that doesn't require elevated permissions
  set_directory_properties(PROPERTIES JUCE_VST3_COPY_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")
endif()

# Off-Windows, hand JUCE the icon PNGs (macOS .icns / iOS asset catalog). On
# Windows we instead use a custom .rc + native multi-size icon.ico (see the
# JUCE_USER_DEFINED_RC_FILE wiring after juce_add_plugin).
set(plugin_icon_args)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(plugin_icon_args
      ICON_BIG   audio_plugin/icon_big.png
      ICON_SMALL audio_plugin/icon_small.png)
endif()

# Adds a plugin target (that's basically what the Projucer does).
juce_add_plugin(
  AudioPlugin
  COMPANY_NAME
  "atan2"
  IS_SYNTH
  TRUE
  NEEDS_MIDI_INPUT
  TRUE
  NEEDS_MIDI_OUTPUT
  FALSE
  PLUGIN_MANUFACTURER_CODE
  Atn2 # first letter uppercase, rest lowercase, for max compatibility
  PLUGIN_CODE
  Opna # first letter uppercase, rest lowercase, for max compatibility
  FORMATS
  Standalone
  VST3
  AU
  PRODUCT_NAME
  "atan2608" # dev name "atan2" + the YM2608 chip
  # Windows icon is supplied via a custom .rc (see below) pointing at a
  # hand-built multi-size icon.ico; passing ICON_BIG/SMALL here too would make
  # juceaide emit its own (blurry, rescaled) icon + duplicate IDI_ICON1. So the
  # PNGs are only handed to JUCE off-Windows (macOS .icns / iOS assets).
  ${plugin_icon_args}
  VERSION
  ${PROJECT_VERSION} # from version.txt (read above)
  COPY_PLUGIN_AFTER_BUILD
  TRUE # auto-copies built plugin formats to their install dirs (must already exist)
)

# These definitions are recommended by JUCE
target_compile_definitions(AudioPlugin PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

# Windows icon: bypass juceaide's single-PNG rescale (which bilinear-blurs our
# pixel-art icon at every size except the one that happens to match exactly).
# Instead supply a custom .rc via JUCE_USER_DEFINED_RC_FILE that references a
# hand-built multi-size icon.ico (every size rendered natively by
# scripts/gen_icon.py). The generated <target>_resources.rc #includes our file
# in place of its auto icon+version block. atan2608.rc is generated from
# atan2608.rc.in (below) with the version stamped into VERSIONINFO.
# audio_plugin/ is added to the rc include dirs so the icon.ico the generated .rc
# references resolves (JUCE only forwards include dirs to the rc compile when this
# macro is present).
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  # Generate the .rc from the template, stamping the version (from version.txt,
  # via PROJECT_VERSION). FILEVERSION/PRODUCTVERSION want the comma form
  # (MAJOR,MINOR,PATCH,BUILD); the string fields want the dotted form.
  set(ATAN_VER_COMMA "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
  set(ATAN_VER_DOT   "${PROJECT_VERSION}")
  set(_atan_rc_out "${CMAKE_CURRENT_BINARY_DIR}/generated/atan2608.rc")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/audio_plugin/atan2608.rc.in" "${_atan_rc_out}" @ONLY)

  target_compile_definitions(AudioPlugin PRIVATE
    "JUCE_USER_DEFINED_RC_FILE=\"${_atan_rc_out}\"")
  target_include_directories(AudioPlugin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/audio_plugin")

  # JUCE doesn't know the generated resources.rc transitively depends on our
  # generated atan2608.rc or the icon.ico it references (those are resolved by the
  # rc preprocessor, not CMake). Declare the dependency so a version bump or icon
  # change forces the resource step to recompile and re-embed.
  get_target_property(_juce_gen_dir AudioPlugin JUCE_GENERATED_SOURCES_DIRECTORY)
  set_source_files_properties(
    "${_juce_gen_dir}/AudioPlugin_resources.rc"
    PROPERTIES OBJECT_DEPENDS
    "${_atan_rc_out};${CMAKE_CURRENT_SOURCE_DIR}/audio_plugin/icon.ico")
endif()

# macOS icon: ICON_BIG/SMALL (above) wire up CFBundleIconFile + the asset
# catalog, but juceaide encodes the 32px entry as the legacy `icp5` OSType,
# which modern Finder mis-decodes as raw pixels -> a garbled red thumbnail in
# list/column view (the 512px `ic09` entry is fine, so Get Info / Quick Look
# look correct). Overwrite the bundled Icon.icns with our hand-built one
# (scripts/gen_icon.py -> iconutil, which uses the reliable ic05/ic11 types).
if(APPLE)
  set(_atan_icns "${CMAKE_CURRENT_SOURCE_DIR}/audio_plugin/Icon.icns")
  foreach(_fmt Standalone VST3 AU)
    if(TARGET AudioPlugin_${_fmt})
      add_custom_command(TARGET AudioPlugin_${_fmt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_atan_icns}"
                "$<TARGET_BUNDLE_DIR:AudioPlugin_${_fmt}>/Contents/Resources/Icon.icns"
        COMMENT "Replacing juceaide Icon.icns (icp5 -> ic05/ic11) for AudioPlugin_${_fmt}"
        VERBATIM)
    endif()
  endforeach()
endif()

# We want to have a "library" target with PluginProcessor and PluginEditor to use in plugins and in tests.
# Library targets that use JUCE modules non-PRIVATEly should be created as JUCE module themselves,
# not as static or shared libraries. Otherwise, you risk buggy JUCE build configuration.
# This is unlikely on small projects, but we want to use the best practices from the start
# (and migrating to JUCE modules at a late development stage is a nightmare, trust me).
# Build identity: short git commit (with a -dirty marker) + project version,
# surfaced in the editor's Settings panel so the running build is identifiable.
# Version.h is regenerated on EVERY build by the opna_version target below (via
# cmake/GitVersion.cmake), so the hash tracks the live commit without a
# reconfigure; this configure-time call is just the bootstrap so the header
# exists before the first build (include dirs resolve).
# Version comes from version.txt (read into PROJECT_VERSION above and handed to
# juce_add_plugin); read it back off the plugin target so Version.h stays in sync.
get_target_property(OPNA_VERSION AudioPlugin JUCE_VERSION)
set(OPNA_VERSION_IN  ${CMAKE_CURRENT_SOURCE_DIR}/audio_plugin/include/Version.h.in)
set(OPNA_VERSION_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h)
set(OPNA_VERSION_ARGS
  -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR}
  -DIN_FILE=${OPNA_VERSION_IN}
  -DOUT_FILE=${OPNA_VERSION_OUT}
  -DOPNA_VERSION=${OPNA_VERSION}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GitVersion.cmake
)
execute_process(COMMAND ${CMAKE_COMMAND} ${OPNA_VERSION_ARGS})

add_custom_target(opna_version ALL
  COMMAND ${CMAKE_COMMAND} ${OPNA_VERSION_ARGS}
  BYPRODUCTS ${OPNA_VERSION_OUT}
  COMMENT "Refreshing build identity (Version.h)"
  VERBATIM
)

# ymfm OPNA core + our JUCE-free chip wrapper, compiled as a separate static
# library so they are NOT subject to the plugin's warnings-as-errors flags.
add_library(
  ymfm_opna STATIC
  deps/ymfm-mame/src/ymfm_opn.cpp
  deps/ymfm-mame/src/ymfm_adpcm.cpp
  deps/ymfm-mame/src/ymfm_ssg.cpp
  deps/ymfm-mame/src/ymfm_pcm.cpp
  audio_plugin/source/chip/OpnaChip.cpp
)
target_include_directories(
  ymfm_opna PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/deps/ymfm-mame/src
                   ${CMAKE_CURRENT_SOURCE_DIR}/audio_plugin/include
                   ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_compile_features(ymfm_opna PUBLIC cxx_std_17)
# This static lib is linked into the VST3 shared module. On Linux (ELF) code in a
# shared object must be position-independent, so build it with -fPIC; without it
# the VST3 link fails with "relocation R_X86_64_PC32 ... can not be used when
# making a shared object". No-op on Windows/MSVC.
set_target_properties(ymfm_opna PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Embed binary assets: the PC-9800 typeface used by the GUI and the factory
# preset bank (full-state .opnapreset files authored/committed by
# PresetGeneratorTest; FactoryPresets discovers them from BinaryData at runtime).
# Generates BinaryData.h (BinaryData::pc_9800_ttf, ...).
# CONFIGURE_DEPENDS re-globs on build so a newly generated preset is picked up.
# NOTE: the copyrighted YM2608 rhythm ROM is deliberately NOT embedded; the
# plugin loads it at runtime from %APPDATA%/atan2608/ym2608_adpcm_rom.bin (with a
# manual override in the Settings panel). See PluginProcessor::defaultRhythmRomFile.
file(GLOB OPNA_FACTORY_PRESETS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/factory_presets/*.opnapreset")
juce_add_binary_data(OpnaBinaryData SOURCES deps/pc-9800.ttf
                     ${OPNA_FACTORY_PRESETS})

juce_add_module(audio_plugin ALIAS_NAMESPACE my_company)

# Expose the OPNA library (and its public include dir) to everything that uses
# the audio_plugin module: the plugin targets and the tests.
target_link_libraries(audio_plugin INTERFACE ymfm_opna OpnaBinaryData)

# In the following, we re-export some of the compile-time definitions derived from arguments of
# juce_add_plugin(). This is necessary to maintain consistent behavior in built plugins and in tests.
# Check juce/extras/Build/CMake/JUCEUtils.cmake to see how they are defined for the AudioPlugin target.
# Alternatively, you could use
# target_compile_definitions(AudioPluginTest PRIVATE $<TARGET_PROPERTY:AudioPlugin,COMPILE_DEFINITIONS>)
# in the test/CMakeLists.txt file, but I find it more risky to blindly export all compile
# definitions for all targets using audio_plugin
# (1. you can forget to do it 2. you don't know what you're defining)
# than to re-export the meaningful ones for audio_plugin and just link against the module to use it.
target_compile_definitions(
  audio_plugin
  INTERFACE JUCE_PLUGIN_NAME="$<TARGET_PROPERTY:AudioPlugin,JUCE_PLUGIN_NAME>"
            JUCE_IS_MIDI_EFFECT=$<BOOL:$<TARGET_PROPERTY:AudioPlugin,JUCE_IS_MIDI_EFFECT>>
            JUCE_IS_SYNTH=$<BOOL:$<TARGET_PROPERTY:AudioPlugin,JUCE_IS_SYNTH>>
            JUCE_NEEDS_MIDI_INPUT=$<BOOL:$<TARGET_PROPERTY:AudioPlugin,JUCE_NEEDS_MIDI_INPUT>>
            JUCE_NEEDS_MIDI_OUTPUT=$<BOOL:$<TARGET_PROPERTY:AudioPlugin,JUCE_NEEDS_MIDI_OUTPUT>>
)

# Enables strict C++ warnings and treats warnings as errors.
target_compile_options(audio_plugin INTERFACE "${PROJECT_WARNINGS_CXX}")

# We need to link the plugin targets with the module containing our code.
target_link_libraries(
  AudioPlugin
  PRIVATE my_company::audio_plugin
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags
)

# Build identity (Version.h) must be refreshed before the unity TU that includes
# it (SettingsDialog) compiles, in both the plugin and the test binary.
add_dependencies(AudioPlugin opna_version)
