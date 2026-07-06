# Regenerates Version.h from the current git state. Invoked with `cmake -P` both
# once at configure (bootstrap) and on every build (the opna_version target), so
# the editor's build-identity string tracks the live commit / -dirty marker
# WITHOUT needing a reconfigure. Writes via copy_if_different, so Version.h's
# timestamp -- and therefore the unity-TU rebuild it triggers -- only changes
# when the hash actually changes.
#
# Required -D args: SRC_DIR, IN_FILE, OUT_FILE, OPNA_VERSION.

execute_process(
  COMMAND git -C ${SRC_DIR} describe --always --dirty --abbrev=8
  OUTPUT_VARIABLE OPNA_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT OPNA_GIT_COMMIT)
  set(OPNA_GIT_COMMIT "unknown")
endif()

configure_file(${IN_FILE} ${OUT_FILE}.tmp @ONLY)
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT_FILE}.tmp ${OUT_FILE})
execute_process(COMMAND ${CMAKE_COMMAND} -E remove ${OUT_FILE}.tmp)
