include(FetchContent)

# Declare the spdlog repository
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    GIT_PROGRESS TRUE
)

# Make spdlog available in your project
# This will download the repository and add its CMake targets to your project.
message("spdlog")
FetchContent_MakeAvailable(spdlog)