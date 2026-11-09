# FLAC 1.5.0 exports Threads::Threads without finding it in flac-config.cmake.
# Load this after the consumer's project() has enabled the C/C++ languages.
find_package(Threads REQUIRED)
