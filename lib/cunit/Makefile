
include ../../config.mk

CPPFLAGS = $(XML2_CPPFLAGS) $(CUNIT_CPPFLAGS) $(ZLIB_CPPFLAGS) $(GHT_CPPFLAGS) -I..
LDFLAGS = $(XML2_LDFLAGS) $(CUNIT_LDFLAGS) $(ZLIB_LDFLAGS) $(GHT_LDFLAGS) 

EXE = cu_tester

# ADD YOUR NEW TEST FILE HERE (1/1)
OBJS =	\
	cu_tester.o \
	cu_pc_bytes.o \
	cu_pc_schema.o \
	cu_pc_point.o \
	cu_pc_patch.o \
	cu_pc_patch_ght.o

ifeq ($(CUNIT_LDFLAGS),)
# No cunit? Emit message and continue

all:
	@echo "CUnit not found, skipping build"

check:
	@echo "CUnit not found, skipping tests"

else
# Yes cunit? Build tests and run

# Build the unit tester
all: $(EXE)

# Build and run the unit tester
check: $(EXE)
	@./$(EXE)

endif

# Build the main unit test executable
$(EXE): $(OBJS) ../$(LIB_A)
	$(CC) -o $@ $^ $(LDFLAGS) -lm

../$(LIB_A):
	$(MAKE) -C .. $(LIB_A)

# Clean target
clean:
	@rm -f $(OBJS)
	@rm -f $(EXE)

