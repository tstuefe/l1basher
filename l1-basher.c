#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>


// Default: touch every cache line once in a 32K 8-way cache
static int num_lines = 64 * 8;
static int num_lines_skip = 0;
static int seconds = -1; // infinite
static int randomized = 0;
static int verbose = 0;
static int num_threads = 1;
static int wait_before_exit = 0;

static const size_t cacheline_size = 64;

#define PRINT(...) 	{ printf(__VA_ARGS__); putc('\n', stdout); fflush(stdout); }
#define ERRPRINT(...) 	{ fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); fflush(stderr); }
#define VERBOSE(...) 	{ if (verbose) { PRINT(__VA_ARGS__); } }

#define ERRBYE(...) { ERRPRINT(__VA_ARGS__); exit(-1); }

#define USAGE "Usage: l1-basher [-d <secs>] [-T <threads>] [-n <cachelines>] [--skip-lines <skip cachelines when touching>] [-v] [-w|--wait-before-exit]"

#define EXPECT(cond, s) if (!(cond)) { ERRBYE(s); }

struct thread_data {
	pthread_t tid;
	unsigned long long iterations;
	unsigned long long useless;
	volatile int stop;
	unsigned char* memory;
};

static void print_stats() {
	FILE* f = fopen("/proc/self/status", "r");
  	char line[1024];
  	while (fgets(line, sizeof(line), f) == line) {
    		line[sizeof(line) - 1] = '\0';
		if (strstr(line, "ctx") != NULL) {
			printf("%s", line);
		}
	}
	fclose(f);
}

// In verbose mode, print rhythmic cadence once to ease understanding and to make sure we did not make errors.
static void print_cadence(struct thread_data* td) {
	const size_t distance = cacheline_size + (num_lines_skip * cacheline_size);
	const size_t total_size = num_lines * cacheline_size;
	PRINT("Thread %u will touch, in sequence: ", (unsigned)td->tid);
	int zeros = 0;
	const unsigned char* last = NULL;
	for (int i = 0; zeros < 2 && i < 8; i ++) {
		const size_t offset = (i * distance) % total_size;
		const unsigned char* address = td->memory + offset;
		printf("%p ", address);
		if (last != NULL && last < address) {
			printf("(+0x%x) ", (unsigned)(address - last));
		}
		last = address;
		if (offset == 0) {
			zeros ++;
		}
	}
	printf(" ...\n");
}

static void* do_work(void* data) {

	struct thread_data* const td = (struct thread_data*) data;	
	unsigned long long iterations = 0;
	int useless = 0;

	const int stop_check_interval = 0x100000;

	const size_t distance = cacheline_size + (num_lines_skip * cacheline_size);
	const size_t total_size = num_lines * cacheline_size;

	while(td->stop == 0) {
		for (int i = 0; i < stop_check_interval; i ++) {
			const size_t offset = (i * distance) % total_size;
			const unsigned char* address = td->memory + offset;
			// read access
			useless += *(address);
		}
		iterations ++;
	}

	td->useless = useless;
	td->iterations = iterations;
	return data;
}

static void create_thread_and_run(struct thread_data* data) {
	int rc = 0;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	rc = pthread_create(&(data->tid), &attr, do_work, data);
	if (rc == 0) {
		VERBOSE("started thread %u", (unsigned)(data->tid));
		if (verbose) {
			print_cadence(data);
		}
	} else {
		ERRBYE("thread start failure %d", errno);
	}
}


int main(int argc, char** argv) {
	int a = 1;
	while (a < argc) {
		
		if (strcmp(argv[a], "-d") == 0) {
			a ++;
			EXPECT(a < argc, USAGE);
			seconds = atoi(argv[a]);
			EXPECT(seconds > 0, USAGE);
		} else if (strcmp(argv[a], "-T") == 0) {
                        a ++;
                        EXPECT(a < argc, USAGE);
                        num_threads = atoi(argv[a]);
                        EXPECT(num_threads >= 0, USAGE); // we allow T=0 aka do nothing
		} else if (strcmp(argv[a], "-n") == 0) {
                        a ++;
                        EXPECT(a < argc, USAGE);
                        num_lines = atoi(argv[a]);
                        EXPECT(num_lines > 0, USAGE);
		} else if (strcmp(argv[a], "--skip-lines") == 0) {
                        a ++;
                        EXPECT(a < argc, USAGE);
                        num_lines_skip = atoi(argv[a]);
                        EXPECT(num_lines_skip > 0, USAGE);
                        EXPECT(num_lines_skip % 2 == 1 && num_lines_skip < num_lines ,
                               "--skip-lines should be an uneven number smaller than number of cache lines");
		} else if (strcmp(argv[a], "-r") == 0) {
		 	randomized = 1;
		} else if (strcmp(argv[a], "-v") == 0) {
			verbose = 1;
		} else if (strcmp(argv[a], "-w") == 0) {
			wait_before_exit = 1;
		} else {
			ERRBYE(USAGE);
		}

		a++;
	}
	

	struct thread_data* tdarr = calloc(num_threads, sizeof(struct thread_data));

	PRINT("Bashing %d lines, skip %d, with %d threads, randomized:%d ...", num_lines, num_lines_skip, num_threads, randomized);
	PRINT("My PID is %u.", getpid());

	for (int t = 0; t < num_threads; t++) {
		const size_t total_size = num_lines * cacheline_size;
		void* mem = NULL;
		if (posix_memalign(&mem, 64, total_size) != 0) {
			ERRBYE("posix_memalign");
		}
		tdarr[t].memory = (unsigned char*) mem;
		// fill with upredictable data
		time_t t1;
		time(&t1);
		memset(mem, (unsigned char)t1 & 0xFF, total_size);
	}

	for (int t = 0; t < num_threads; t++) {
		create_thread_and_run(tdarr + t);
	}	

	if (seconds > 0) {
		PRINT("Running for %d seconds...", seconds);
		sleep(seconds);
	} else {
		PRINT("Press key to stop...");
		getchar();
	}

	for (int t = 0; t < num_threads; t++) {
		tdarr[t].stop = 1;
	}	

	for (int t = 0; t < num_threads; t++) {
		void* dummy;
		if (pthread_join(tdarr[t].tid, &dummy) != 0) {
			ERRBYE("pthread_join err %d", errno);
		}
	}	

	unsigned long long iterations = 0;
	int useless = 0;
	for (int t = 0; t < num_threads; t++) {
		VERBOSE("Thread %d Iterations %llu", t, tdarr[t].iterations);
		useless += tdarr[t].useless;
		iterations += tdarr[t].iterations;
	}
	
	PRINT("All Iterations: %llu (%.2f mio)", iterations, (double)iterations / 1000000.0f);
	VERBOSE("useless: %d", useless);

	print_stats();

	if (wait_before_exit) {
		PRINT("Finished, press key to exit process...");
		getchar();
	}

	return 0;	
}

