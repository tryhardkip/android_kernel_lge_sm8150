# SPDX-License-Identifier: GPL-2.0

GCOV_PROFILE	:= n
# The runtime itself must never be instrumented for profiling, otherwise the
# instrumentation calls into a runtime that is still profiling itself.
PGO_PROFILE	:= n
KCOV_INSTRUMENT	:= n
KASAN_SANITIZE	:= n
UBSAN_SANITIZE	:= n

# Disable LTO for the runtime so the section symbols resolve as expected.
CFLAGS_REMOVE_fs.o		= $(DISABLE_LTO)
CFLAGS_REMOVE_instrument.o	= $(DISABLE_LTO)

obj-y	+= fs.o instrument.o
