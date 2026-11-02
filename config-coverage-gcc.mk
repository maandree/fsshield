CONFIGFILE_PROPER = config.mk
include $(CONFIGFILE_PROPER)

CC   = $(CC_PREFIX)gcc -std=c99
GCOV = gcov

COV_CPPFLAGS = -DCOVERAGE_TEST
COV_CFLAGS   = --coverage -g -O0
COV_LDFLAGS  = --coverage -g -O0

G =

coverage: check
	$(GCOV) -pr -- *.gcda 2>&1
