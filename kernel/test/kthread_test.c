// kthread_test.c - Comprehensive tests for kernel thread infrastructure
//
// Tests cover:
// 1. Basic kthread creation and execution
// 2. Multiple kthread creation
// 3. Kthread argument passing
// 4. Kthread sleep/wakeup
// 5. Kthread exit behavior
// 6. Kthread scheduling
// 7. Kthread state transitions
// 8. Stress tests

#include "kthread_test.h"

#include "../kalloc.h"
#include "../printf.h"
#include "../proc.h"
#include "../riscv.h"
#include "../spinlock.h"
#include "../string.h"
#include "../trap.h"

// Test synchronization primitives
static struct spinlock test_lock;
static volatile int test_counter;
static volatile int test_flag;
static volatile int test_results[16];

// External declarations
extern struct proc proc[];
extern struct spinlock tickslock;
extern uint ticks;

// ============================================================================
// Test 1: Basic kthread creation
// ============================================================================

static void test1_thread_func(void *arg) {
  (void)arg;
  acquire(&test_lock);
  test_flag = 1;
  release(&test_lock);
}

int kthread_test_basic_create(void) {
  initlock(&test_lock, "test");
  test_flag = 0;

  struct proc *p = kthread_create(test1_thread_func, 0, "test1");
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  // Verify kthread properties
  if (!p->is_kthread) {
    printf("  FAIL: is_kthread not set\n");
    return 0;
  }

  if (p->pagetable != 0) {
    printf("  FAIL: kthread should have no user pagetable\n");
    return 0;
  }

  if (p->usyscall != 0) {
    printf("  FAIL: kthread should have no usyscall page\n");
    return 0;
  }

  // Wait for thread to execute (with timeout)
  int timeout = 1000000;
  while (test_flag == 0 && timeout-- > 0) {
    // Yield to let the kthread run
    yield();
  }

  if (test_flag != 1) {
    printf("  FAIL: kthread did not execute (flag=%d)\n", test_flag);
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 2: Kthread argument passing
// ============================================================================

static void test2_thread_func(void *arg) {
  uint64 val = (uint64)arg;
  acquire(&test_lock);
  test_counter = (int)val;
  release(&test_lock);
}

int kthread_test_arg_passing(void) {
  initlock(&test_lock, "test");
  test_counter = 0;

  uint64 expected = 0xDEADBEEF;
  struct proc *p = kthread_create(test2_thread_func, (void *)expected, "test2");
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  // Wait for thread to execute
  int timeout = 1000000;
  while (test_counter == 0 && timeout-- > 0) {
    yield();
  }

  if (test_counter != (int)expected) {
    printf("  FAIL: argument not passed correctly (got %d, expected %d)\n",
           test_counter, (int)expected);
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 3: Multiple kthread creation
// ============================================================================

static void test3_thread_func(void *arg) {
  int id = (int)(uint64)arg;
  acquire(&test_lock);
  test_results[id] = id + 100;
  test_counter++;
  release(&test_lock);
}

int kthread_test_multiple_create(void) {
  initlock(&test_lock, "test");
  test_counter = 0;
  for (int i = 0; i < 8; i++) {
    test_results[i] = 0;
  }

  const int NUM_THREADS = 8;

  // Create multiple kthreads
  for (int i = 0; i < NUM_THREADS; i++) {
    struct proc *p = kthread_create(test3_thread_func, (void *)(uint64)i, "test3");
    if (p == 0) {
      printf("  FAIL: failed to create kthread %d\n", i);
      return 0;
    }
  }

  // Wait for all threads to complete
  int timeout = 10000000;
  while (test_counter < NUM_THREADS && timeout-- > 0) {
    yield();
  }

  if (test_counter != NUM_THREADS) {
    printf("  FAIL: only %d/%d threads completed\n", test_counter, NUM_THREADS);
    return 0;
  }

  // Verify each thread executed correctly
  for (int i = 0; i < NUM_THREADS; i++) {
    if (test_results[i] != i + 100) {
      printf("  FAIL: thread %d result incorrect (got %d, expected %d)\n",
             i, test_results[i], i + 100);
      return 0;
    }
  }

  return 1;
}

// ============================================================================
// Test 4: Kthread sleep/wakeup
// ============================================================================

static int sleep_channel;
static struct spinlock sleep_lock;

static void test4_sleeper_func(void *arg) {
  (void)arg;
  acquire(&sleep_lock);
  test_flag = 1;  // Signal that we're about to sleep
  sleep(&sleep_channel, &sleep_lock);
  test_flag = 2;  // Signal that we woke up
  release(&sleep_lock);
}

int kthread_test_sleep_wakeup(void) {
  initlock(&sleep_lock, "sleep_test");
  test_flag = 0;

  struct proc *p = kthread_create(test4_sleeper_func, 0, "sleeper");
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  // Wait for thread to start sleeping
  int timeout = 1000000;
  while (test_flag != 1 && timeout-- > 0) {
    yield();
  }

  if (test_flag != 1) {
    printf("  FAIL: thread did not reach sleep point\n");
    return 0;
  }

  // Give it time to actually enter sleep
  for (int i = 0; i < 1000; i++) {
    yield();
  }

  // Wake up the thread
  acquire(&sleep_lock);
  wakeup(&sleep_channel);
  release(&sleep_lock);

  // Wait for thread to wake up
  timeout = 1000000;
  while (test_flag != 2 && timeout-- > 0) {
    yield();
  }

  if (test_flag != 2) {
    printf("  FAIL: thread did not wake up (flag=%d)\n", test_flag);
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 5: Kthread state verification
// ============================================================================

static void test5_long_running_func(void *arg) {
  (void)arg;
  // Run for a while to allow state inspection
  for (volatile int i = 0; i < 100000; i++) {
    // Busy wait
  }
  acquire(&test_lock);
  test_flag = 1;
  release(&test_lock);
}

int kthread_test_state_verification(void) {
  initlock(&test_lock, "test");
  test_flag = 0;

  struct proc *p = kthread_create(test5_long_running_func, 0, "statetest");
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  // Check that kthread has correct initial setup
  if (p->sz != 0) {
    printf("  FAIL: kthread sz should be 0\n");
    return 0;
  }

  if (p->cwd != 0) {
    printf("  FAIL: kthread cwd should be NULL\n");
    return 0;
  }

  if (p->parent != 0) {
    printf("  FAIL: kthread parent should be NULL\n");
    return 0;
  }

  // Check no open files
  for (int i = 0; i < NOFILE; i++) {
    if (p->ofile[i] != 0) {
      printf("  FAIL: kthread should have no open files\n");
      return 0;
    }
  }

  // Wait for completion
  int timeout = 10000000;
  while (test_flag != 1 && timeout-- > 0) {
    yield();
  }

  return 1;
}

// ============================================================================
// Test 6: Kthread naming
// ============================================================================

static void test6_dummy_func(void *arg) {
  (void)arg;
  acquire(&test_lock);
  test_flag = 1;
  release(&test_lock);
}

int kthread_test_naming(void) {
  initlock(&test_lock, "test");
  test_flag = 0;

  const char *name = "mythread";
  struct proc *p = kthread_create(test6_dummy_func, 0, (char *)name);
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  // Verify name was set correctly
  if (strncmp(p->name, name, sizeof(p->name)) != 0) {
    printf("  FAIL: kthread name not set correctly (got '%s', expected '%s')\n",
           p->name, name);
    return 0;
  }

  // Wait for completion
  int timeout = 1000000;
  while (test_flag != 1 && timeout-- > 0) {
    yield();
  }

  return 1;
}

// ============================================================================
// Test 7: Kthread context (kernel stack)
// ============================================================================

static void test7_stack_func(void *arg) {
  (void)arg;
  // Allocate some stack space to verify stack is working
  volatile char buffer[256];
  for (int i = 0; i < 256; i++) {
    buffer[i] = (char)i;
  }

  // Verify stack didn't corrupt
  int ok = 1;
  for (int i = 0; i < 256; i++) {
    if (buffer[i] != (char)i) {
      ok = 0;
      break;
    }
  }

  acquire(&test_lock);
  test_flag = ok ? 1 : -1;
  release(&test_lock);
}

int kthread_test_kernel_stack(void) {
  initlock(&test_lock, "test");
  test_flag = 0;

  struct proc *p = kthread_create(test7_stack_func, 0, "stacktest");
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  // Verify kstack is set
  if (p->kstack == 0) {
    printf("  FAIL: kthread kstack not set\n");
    return 0;
  }

  // Wait for completion
  int timeout = 1000000;
  while (test_flag == 0 && timeout-- > 0) {
    yield();
  }

  if (test_flag != 1) {
    printf("  FAIL: stack test failed (flag=%d)\n", test_flag);
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 8: Kthread yield behavior
// ============================================================================

static volatile int yield_order[4];
static volatile int yield_index;

static void test8_yield_func(void *arg) {
  int id = (int)(uint64)arg;

  for (int i = 0; i < 3; i++) {
    acquire(&test_lock);
    if (yield_index < 12) {
      yield_order[yield_index % 4] = id;
      yield_index++;
    }
    release(&test_lock);
    yield();  // Give other threads a chance
  }

  acquire(&test_lock);
  test_counter++;
  release(&test_lock);
}

int kthread_test_yield_behavior(void) {
  initlock(&test_lock, "test");
  test_counter = 0;
  yield_index = 0;

  const int NUM_THREADS = 2;

  for (int i = 0; i < NUM_THREADS; i++) {
    struct proc *p = kthread_create(test8_yield_func, (void *)(uint64)i, "yield");
    if (p == 0) {
      printf("  FAIL: failed to create kthread %d\n", i);
      return 0;
    }
  }

  // Wait for all threads
  int timeout = 10000000;
  while (test_counter < NUM_THREADS && timeout-- > 0) {
    yield();
  }

  if (test_counter != NUM_THREADS) {
    printf("  FAIL: not all threads completed\n");
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 9: Kthread stress test - many threads
// ============================================================================

static void test9_stress_func(void *arg) {
  int id = (int)(uint64)arg;
  (void)id;

  // Do some work
  volatile int sum = 0;
  for (int i = 0; i < 1000; i++) {
    sum += i;
  }

  acquire(&test_lock);
  test_counter++;
  release(&test_lock);
}

int kthread_test_stress_many_threads(void) {
  initlock(&test_lock, "test");
  test_counter = 0;

  // Create many threads (but not too many to exhaust proc table)
  const int NUM_THREADS = 16;
  int created = 0;

  for (int i = 0; i < NUM_THREADS; i++) {
    struct proc *p = kthread_create(test9_stress_func, (void *)(uint64)i, "stress");
    if (p != 0) {
      created++;
    }
  }

  if (created == 0) {
    printf("  FAIL: could not create any threads\n");
    return 0;
  }

  // Wait for all threads
  int timeout = 100000000;
  while (test_counter < created && timeout-- > 0) {
    yield();
  }

  if (test_counter != created) {
    printf("  FAIL: only %d/%d threads completed\n", test_counter, created);
    return 0;
  }

  return 1;
}

// ============================================================================
// Test 10: Kthread function pointer storage
// ============================================================================

int kthread_test_func_pointer(void) {
  struct proc *p = kthread_create(test1_thread_func, 0, "functest");
  if (p == 0) {
    printf("  FAIL: kthread_create returned NULL\n");
    return 0;
  }

  if (p->kthread_func != test1_thread_func) {
    printf("  FAIL: kthread_func not stored correctly\n");
    return 0;
  }

  // Wait for it to complete
  int timeout = 1000000;
  while (p->state != ZOMBIE && p->state != UNUSED && timeout-- > 0) {
    yield();
  }

  return 1;
}

// ============================================================================
// Test Runner
// ============================================================================

// Wrapper to run tests in a kthread context
static void kthread_test_runner(void *arg) {
  (void)arg;
  kthread_test_all();
}

// Initialize the test runner as a kthread
void kthread_test_init(void) {
  struct proc *p = kthread_create(kthread_test_runner, 0, "ktest");
  if (p == 0) {
    printf("ERROR: Failed to create kthread test runner\n");
  }
}

void kthread_test_all(void) {
  printf("\n========================================\n");
  printf("Running Kernel Thread Tests\n");
  printf("========================================\n\n");

  int passed = 0;
  int failed = 0;

  struct {
    const char *name;
    int (*func)(void);
  } tests[] = {
      {"Basic kthread creation", kthread_test_basic_create},
      {"Argument passing", kthread_test_arg_passing},
      {"Multiple kthread creation", kthread_test_multiple_create},
      {"Sleep/wakeup", kthread_test_sleep_wakeup},
      {"State verification", kthread_test_state_verification},
      {"Naming", kthread_test_naming},
      {"Kernel stack", kthread_test_kernel_stack},
      {"Yield behavior", kthread_test_yield_behavior},
      {"Stress test (many threads)", kthread_test_stress_many_threads},
      {"Function pointer storage", kthread_test_func_pointer},
  };

  int num_tests = sizeof(tests) / sizeof(tests[0]);

  for (int i = 0; i < num_tests; i++) {
    printf("Test %d: %s... ", i + 1, tests[i].name);
    if (tests[i].func()) {
      printf("PASSED\n");
      passed++;
    } else {
      printf("FAILED\n");
      failed++;
    }

    // Give some time between tests for cleanup
    for (int j = 0; j < 10000; j++) {
      yield();
    }
  }

  printf("\n----------------------------------------\n");
  printf("Kthread Tests: %d passed, %d failed, %d total\n", passed, failed,
         num_tests);
  printf("========================================\n\n");
}
