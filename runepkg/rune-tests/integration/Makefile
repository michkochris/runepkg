all: test

test:
	@echo "Running integration test suite..."
	./test_fsm_signal_injection.sh
	./test_concurrent_ops.sh
	../../test_fsm_chaos.sh
	@echo "All integration tests passed successfully!"

clean:
	rm -rf test_chaos_workspace
