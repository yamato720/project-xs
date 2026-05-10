PYTHON ?= python3
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2

ROOT_DIR := $(abspath .)
BUILD_DIR := $(ROOT_DIR)/build
TEST_BUILD_DIR := $(BUILD_DIR)/test
SRC_DIR := $(ROOT_DIR)/src
INCLUDE_DIR := $(SRC_DIR)/include
BASE_SRC_DIR := $(SRC_DIR)/base
BASE_INCLUDE_DIR := $(INCLUDE_DIR)/base
SCRIPT := $(ROOT_DIR)/script/generate_cg_dataset.py
MAIN_SRC := $(ROOT_DIR)/main.cpp
ABC_TEST_DIR := $(ROOT_DIR)/test/abctest
TEST_ROOT_DIR := $(ROOT_DIR)/test
TEST_MAIN_FILES := $(wildcard $(TEST_ROOT_DIR)/*/main.cpp)
TEST_NAMES := $(sort $(notdir $(patsubst %/,%,$(dir $(TEST_MAIN_FILES)))))
PORT_SRC := $(BASE_SRC_DIR)/Port.cpp
PORT_GROUP_SRC := $(BASE_SRC_DIR)/PortGroup.cpp
AXI_SRC := $(BASE_SRC_DIR)/Axi.cpp
SIM_SRC := $(BASE_SRC_DIR)/CycleSimulator.cpp
KERNEL_SRC := $(BASE_SRC_DIR)/Kernel.cpp
KERNEL_COMPONENT_SRC := $(BASE_SRC_DIR)/KernelComponent.cpp
TARGET := $(BUILD_DIR)/cgsolver_golden

SIZE ?= 512
TAU ?= 1e-10
MAX_ITERS ?= 0
ASPECT_RATIO ?= 1.6
DATASET_DIR ?= $(ROOT_DIR)/data/generated/cgsolver/n$(SIZE)

TEST_GOALS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))

ifneq ($(filter test,$(MAKECMDGOALS)),)
$(foreach goal,$(TEST_GOALS),$(eval .PHONY: $(goal)))
$(foreach goal,$(TEST_GOALS),$(eval $(goal): ; @:))
endif

.PHONY: all build generate run run-cycle-sim test clean

all: run

build: $(TARGET)

generate:
	$(PYTHON) $(SCRIPT) --size $(SIZE) --aspect-ratio $(ASPECT_RATIO) --output-dir $(DATASET_DIR)

run: $(TARGET) generate
	$(TARGET) $(DATASET_DIR) --tau $(TAU) --max-iters $(MAX_ITERS)

test:
	@test_name="$(word 2,$(MAKECMDGOALS))"; \
	if [ -z "$$test_name" ]; then \
		echo "Usage: make test <test_name>"; \
		echo "Available tests: $(TEST_NAMES)"; \
		exit 1; \
	fi; \
	if [ "$(words $(MAKECMDGOALS))" -ne 2 ]; then \
		echo "Usage: make test <test_name>"; \
		echo "Only one test name is supported at a time."; \
		exit 1; \
	fi; \
	case " $(TEST_NAMES) " in \
		*" $$test_name "*) ;; \
		*) \
			echo "Unknown test: $$test_name"; \
			echo "Available tests: $(TEST_NAMES)"; \
			exit 1; \
			;; \
	esac; \
	mkdir -p $(TEST_BUILD_DIR); \
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(INCLUDE_DIR) \
		$(TEST_ROOT_DIR)/$$test_name/main.cpp \
		$(PORT_SRC) $(PORT_GROUP_SRC) $(AXI_SRC) $(SIM_SRC) $(KERNEL_SRC) $(KERNEL_COMPONENT_SRC) \
		-o $(TEST_BUILD_DIR)/$$test_name; \
	$(TEST_BUILD_DIR)/$$test_name

run-cycle-sim:
	@$(MAKE) test abctest

clean:
	rm -rf $(BUILD_DIR)

$(TARGET): $(MAIN_SRC) $(PORT_SRC) $(PORT_GROUP_SRC) $(AXI_SRC) $(SIM_SRC) $(KERNEL_SRC) $(KERNEL_COMPONENT_SRC) $(SRC_DIR)/CgSolverGolden.hpp $(SRC_DIR)/CsrDataset.hpp $(BASE_INCLUDE_DIR)/CycleSimulator.h $(BASE_INCLUDE_DIR)/Kernel.h $(BASE_INCLUDE_DIR)/KernelComponent.h $(BASE_INCLUDE_DIR)/Port.h $(BASE_INCLUDE_DIR)/PortGroup.h $(BASE_INCLUDE_DIR)/Axi.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(INCLUDE_DIR) $(MAIN_SRC) $(PORT_SRC) $(PORT_GROUP_SRC) $(AXI_SRC) $(SIM_SRC) $(KERNEL_SRC) $(KERNEL_COMPONENT_SRC) -o $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
