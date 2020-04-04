# collect some version information from git
execute_process(
  COMMAND git rev-parse --abbrev-ref HEAD
  OUTPUT_VARIABLE GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git log -1 --format=%h
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND git rev-list --count --first-parent HEAD
  OUTPUT_VARIABLE GIT_REVISION_NUMBER
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
execute_process(
  COMMAND sh -c "git diff --quiet --exit-code || echo +"
  OUTPUT_VARIABLE GIT_DIFF
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

if("${GIT_REVISION_NUMBER}" STREQUAL "")
  set(GIT_REVISION_NUMBER "N/A")
  set(GIT_COMMIT_HASH "N/A")
  set(GIT_BRANCH "N/A")
endif()

string(TIMESTAMP BUILD_TIME_STAMP UTC)

set(VERSION
"
  const char* GIT_BRANCH=\"${GIT_BRANCH}\";
  const char* GIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\";
  const char* GIT_REVISION_NUMBER=\"${GIT_REVISION_NUMBER}${GIT_DIFF}\";
  const char* BUILD_TIME_STAMP=\"${BUILD_TIME_STAMP}\";
"
)
if(EXISTS ${FILE})
    file(READ ${FILE} VERSION_)
else()
    set(VERSION_ "")
endif()

if (NOT "${VERSION}" STREQUAL "${VERSION_}")
    file(WRITE ${FILE} "${VERSION}")
endif()

