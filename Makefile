.PHONY: all build clean test run-monitor run-dashboard

all: build

build:
	$(MAKE) -C module1_monitor
	$(MAKE) -C module2_governor

clean:
	$(MAKE) -C module1_monitor clean
	$(MAKE) -C module2_governor clean
	rm -f data/processes.json data/processes.json.tmp data/actions.jsonl

test: build
	python3 tests/test_all.py
	python3 tests/test_governor.py
	python3 tests/test_dashboard.py

run-monitor: build
	./module1_monitor/monitor --output data/processes.json --interval 1

run-dashboard:
	python3 module3_analytics/dashboard.py --input data/processes.json --history data/actions.jsonl
