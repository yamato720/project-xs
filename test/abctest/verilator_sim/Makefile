VERILATOR ?= verilator
TOP := abc_demo
VCD := abc_demo.vcd
SAMPLES := samples.jsonl

.PHONY: all run clean

all: run

obj_dir/V$(TOP): $(TOP).v sim_main.cpp
	$(VERILATOR) --cc --exe --trace --build -Wall \
		--top-module $(TOP) \
		$(TOP).v sim_main.cpp

run: obj_dir/V$(TOP)
	./obj_dir/V$(TOP)

clean:
	rm -rf obj_dir $(VCD) $(SAMPLES)
