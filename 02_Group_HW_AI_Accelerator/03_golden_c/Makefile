CFLAGS = -O2 -Wall -Wextra -std=c11
golden: golden.c npu_ref.c npu_ref.h model_params.h test_vectors.h
	$(CC) $(CFLAGS) -o $@ golden.c npu_ref.c
run: golden
	./golden
clean:
	rm -f golden
