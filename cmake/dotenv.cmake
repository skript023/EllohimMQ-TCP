include(FetchContent)

FetchContent_Declare(
    dotenv
    GIT_REPOSITORY https://github.com/laserpants/dotenv-cpp.git
    GIT_TAG 432def2fb9c2a5e51e6d491f35f2e8f35fa900b0
    GIT_PROGRESS TRUE
)

FetchContent_MakeAvailable(dotenv)
message("dotenv module")
