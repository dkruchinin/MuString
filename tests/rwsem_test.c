/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * eza/sync/rwsem_test.c: Read-Write semaphore test.
 */

#include <test.h>
#include <eza/rwsem.h>
#include <eza/waitqueue.h>
#include <kernel/syscalls.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

#define RWSEM_TEST_ID "RW semaphore test"
#define NUM_WRITERS 5
#define NUM_READERS 5

static RWSEM_DEFINE(rwsem);
static bool finished = false;
static atomic_t num_readers;
static atomic_t num_writers;
static int readers[NUM_READERS];

static void writer_thread(void *arg)
{
  atomic_inc(&num_writers);
  rwsem_down_write(&rwsem);
  kprintf("Writer [pid = %ld] downed the semaphore!\n", current_task()->pid);
  rwsem_up_write(&rwsem);
  kprintf("Writer [pid = %ld] upped the semaphore!\n", current_task()->pid);

  atomic_dec(&num_writers);
  sys_exit(0);
}

static void reader_thread(void *arg)
{
  atomic_inc(&num_readers);
  rwsem_down_read(&rwsem);
  kprintf("Reader [pid = %ld] downed the semaphore!\n", current_task()->pid);  
  while(!readers[(long)arg]);  
  rwsem_up_read(&rwsem);
  kprintf("Reader [pid = %ld] upped the semaphore!\n", current_task()->pid);

  atomic_dec(&num_readers);
  sys_exit(0);
}

static void check_num_readers(test_framework_t *tf, int exp)
{
  if (atomic_get(&num_readers) != exp) {
    tf->printf("Unexpected number of readers %d. %d was expected!\n",
               atomic_get(&num_readers), exp);
    tf->failed();
  }
}

static void check_num_writers(test_framework_t *tf, int exp)
{
  if (atomic_get(&num_writers) != exp) {
    tf->printf("Unexpected number of writers %d. %d was expected!",
               atomic_get(&num_writers), exp);
    tf->failed();
  }
}

static void create_writers(test_framework_t *tf, int nw)
{
  while (nw--) {
    if (kernel_thread(writer_thread, NULL, NULL)) {
      tf->printf("Can't create kernel thread for writer task!\n");
      tf->abort();
    }
  }
}

static void create_readers(test_framework_t *tf, int nr)
{
  int i;

  for (i = 0; i < nr; i++) {
    if (kernel_thread(reader_thread, (void *)(long)i, NULL)) {
      tf->printf("Can't create kernel thread for reader task!\n");
      tf->abort();
    }
  }
}

static void rws_tests_runner(void *ctx)
{
  test_framework_t *tf = ctx;
  int i;

  tf->printf("Run %d reader tasks one-by-one and then run one writer.\n", NUM_READERS);
  create_readers(tf, NUM_READERS);
  sleep(HZ);
  check_num_readers(tf, NUM_READERS);
  create_writers(tf, 1);

  waitqueue_dump(&rwsem.writers_wq);
  sleep(HZ);
  check_num_writers(tf, 1);
  waitqueue_dump(&rwsem.writers_wq);
  for (i = 1; i < NUM_READERS; i++)
    readers[i]++;

  sleep(HZ);
  check_num_readers(tf, 1);
  check_num_writers(tf, 1);
  waitqueue_dump(&rwsem.writers_wq);
  readers[0]++;
  sleep(HZ);
  check_num_readers(tf, 0);
  check_num_writers(tf, 0);
  tf->printf("ok\n");

  tf->printf("Now create 4 writer then %d readers\n", NUM_READERS);
  create_writers(tf, 4);
  create_readers(tf, NUM_READERS);
  sleep(HZ * 2);
  check_num_readers(tf, 0);
  check_num_writers(tf, 0);
  finished = true;
}

static void rws_test_run(test_framework_t *tf, void *ctx)
{
  if (kernel_thread(rws_tests_runner, tf, NULL)) {
    tf->printf("Can't create kernel thread!");
    tf->abort();
  }

  tf->test_completion_loop(RWSEM_TEST_ID, &finished);
}

static bool rws_test_init(void **ctx)
{
  atomic_set(&num_readers, 0);
  atomic_set(&num_writers, 0);
  memset(readers, 0, sizeof(int) * NUM_READERS);
  return true;
}

static void rws_test_deinit(void *unused)
{
}

testcase_t rws_testcase = {
  .id = RWSEM_TEST_ID,
  .initialize = rws_test_init,
  .deinitialize = rws_test_deinit,
  .run = rws_test_run,
};
