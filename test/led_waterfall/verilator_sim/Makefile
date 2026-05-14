VERILATOR ?= verilator
TOP := led_waterfall
VCD := led_waterfall.vcd
SAMPLES := samples.jsonl

.PHONY: all run clean

all: run

obj_dir/V$(TOP): $(TOP).v sim_main.cpp
	$(VERILATOR) --cc --exe --trace --build -Wall \
		-GCNT_MAX=4 \
		--top-module $(TOP) \
		$(TOP).v sim_main.cpp

run: obj_dir/V$(TOP)
	./obj_dir/V$(TOP)

clean:
	rm -rf obj_dir $(VCD) $(SAMPLES)
