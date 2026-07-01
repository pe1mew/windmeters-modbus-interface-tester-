/**
 * @file include_lib.cpp
 * @brief Pulls mb_frame.cpp and mb_core.cpp into the native test build.
 *
 * Same workaround greenhouse-Controller's drivers/modBus uses (see its own
 * test/test_modbus_rtu/include_lib.cpp): PlatformIO 6's Library Dependency
 * Finder has a self-reference lock-file bug on Windows, so lib_ldf_mode is
 * set to `off` for the native env (platformio.ini) and the library's own
 * .cpp files are included directly here instead of being linked normally.
 *
 * Never compiled for the target board — the native env's build_src_filter
 * only pulls in files from test/test_mb_core/, and this file's contents are
 * gated on NATIVE_TEST regardless.
 */
#ifdef NATIVE_TEST
  #include "../../lib/mb_core/mb_frame.cpp"
  #include "../../lib/mb_core/mb_core.cpp"
#endif
