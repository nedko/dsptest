/* -*- Mode: C ; c-basic-offset: 2 -*- */
/* dsptest - test how much CPUs/cores/threads affect each other.
 * Copyright (C) 2013 Nedko Arnaudov <nedko@arnaudov.name>
 * Copyright (C) 2013 Petko Bordjukov <bordjukov@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DISTURB_SWITCH_PERIOD 5
#define TEST_LENGTH DISTURB_SWITCH_PERIOD * 4
#define FIFO_PRIORITY 10

#define _GNU_SOURCE 

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#include <pthread.h>

#if defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/cpuset.h>
#define cpu_set_t cpuset_t
#include <pthread_np.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool configure_thead(int priority, int cpu)
{
  cpu_set_t set;
  struct sched_param sched_param;

  CPU_ZERO(&set);
  CPU_SET(cpu, &set);

  if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set) != 0)
  {
    perror("Cannot set scheduling affinity");
    return false;
  }

  if (priority > 0)
  {
    sched_param.sched_priority = priority;
    if (sched_setscheduler(0, SCHED_FIFO, &sched_param) != 0)
    {
      perror("Cannot set scheduling policy");
      return false;
    }
  }

  return true;
}

typedef void (* work)(void);

struct thread_params
{
  const char * name;
  unsigned int priority;
  unsigned int cpu;
  work work;
  pthread_t thread;
  bool stop;
  unsigned int counter;
};

void work_int(void)
{
  uint32_t n, n1, n2;
  uint32_t i;

  n2 = 0;
  n1 = 1;
  i = 0;
  while (i++ < 1000)
  {
    n = n1 + n2;
    n2 = n1;
    n1 = n;
  }
}

void work_float(void)
{
  long double n, n1, n2;
  uint32_t i;

  n2 = 0;
  n1 = 1;
  i = 0;
  while (i++ < 1000)
  {
    n = n1 + n2;
    //n = cosl(n + 0.666) + 0.2315;
    n2 = n1;
    n1 = n;
  }
}

void work_comb1(void)
{
  work_float();
  work_int();
}

void work_comb2(void)
{
  work_int();
  work_float();
}

static const struct work_descriptor
{
  const work work;
  const char ch;
  const char const * name;
  const char const * descr;
} g_work_descriptors[] = {
  { work_float, 'f', "float", "floating point test (fibbonaci)" },
  { work_int,   'i', "int",   "integer test (fibbonaci)" },
  { work_comb1, '1', "comb1", "combined test that first runs float test, then int test" },
  { work_comb2, '2', "comb2", "combined test that first runs int test, then float test" },
  { NULL,       'n', "null",  "null test that spins (branching, function call, etc)" },
  { NULL,        0,   NULL,   NULL },
};

static work decode_work(const char * str)
{
  const struct work_descriptor * descr_ptr;

  if (str[0] == '\0')
  {
    return NULL;
  }

  for (descr_ptr = g_work_descriptors; descr_ptr->name; descr_ptr++)
  {
    if ((str[1] == '\0' &&
         str[0] == descr_ptr->ch) ||
        strcmp(str, descr_ptr->name) == 0)
    {
      return descr_ptr->work;
    }
  }

  return NULL;
}

static const char * work_descr(work work)
{
  const struct work_descriptor * descr_ptr;

  for (descr_ptr = g_work_descriptors; descr_ptr->name; descr_ptr++)
  {
    if (descr_ptr->work == work)
    {
      return descr_ptr->name;
    }
  }

  return "?";
}

#define params_ptr ((struct thread_params *)ctx)
static void * thread(void * ctx)
{
  if (!configure_thead(params_ptr->priority, params_ptr->cpu))
  {
    perror("Cannot set scheduling policy");
    return NULL;
  }

  while (!params_ptr->stop)
  {
    if (params_ptr->work != NULL)
    {
      params_ptr->work();
    }

    params_ptr->counter++;
  }

  return NULL;
}
#undef params_ptr

static bool start_thread(struct thread_params * params_ptr, const char * name, int priority, int cpu, work work)
{
  int ret;

  printf("starting %s thread to do %s work at priority %d on cpu/core %d\n", name, work_descr(work), priority, cpu);

  params_ptr->name = name;
  params_ptr->priority = priority;
  params_ptr->cpu = cpu;
  params_ptr->work = work;

  params_ptr->stop = false;
  params_ptr->counter = 0;

  ret = pthread_create(&params_ptr->thread, NULL, thread, params_ptr);
  if (ret != 0)
  {
    perror("Cannot create thread");
    return false;
  }

  return true;
}

bool stop_thread(struct thread_params * params_ptr)
{
  int ret;

  printf("stopping %s thread\n", params_ptr->name);

  params_ptr->stop = true;

  ret = pthread_join(params_ptr->thread, NULL);
  if (ret != 0)
  {
    perror("Cannot stop thread");
    return false;
  }

  return true;
}

static int get_avaliable_cpu_count(void)
{
  cpu_set_t set;

  CPU_ZERO(&set);

  pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);

#if defined(__FreeBSD__)
  int ncpu;
  size_t length = sizeof( ncpu );

  if( sysctlbyname("hw.ncpu", &ncpu, &length, NULL, 0) )
  {
    ncpu = 1;
  }

  return ncpu;
#else
  return CPU_COUNT(&set);
#endif
}

static void help(FILE * stream)
{
  const struct work_descriptor * descr_ptr;

  fprintf(
    stream,
    "dsptest - test how much CPUs/cores/threads affect each other.\n"
    "\n"
    "Copyright (C) 2013 Nedko Arnaudov <nedko@arnaudov.name>\n"
    "Copyright (C) 2013 Petko Bordjukov <bordjukov@gmail.com>\n"
    "\n"
    "Usage: dspload [<dsp_cpu> <dsp_work> <disturb_cpu> <disturb_work> [<extra_work>]]\n"
    "\n"
    " <dsp_cpu>, <disturb_cpu> - cpu/core index (zero based). default is 0 for dsp_cpu and 1 for disturb_cpu.\n"
    " <dsp_work>, <disturb_work>, <extra_work> - work to be done by the threads.\n"
    "\n"
    "If <extra_work> is specified, SCHED_OTHER threads will be started on cpu/cores\n"
    "that are not occupied by the dsp or disturb thread.\n"
    "\n"
    "The dsp and disturb threads are SCHED_FIFO ones and run at priority %d\n"
    "\n"
    "Work can be specified as full string or single char:\n",
    FIFO_PRIORITY);

  for (descr_ptr = g_work_descriptors; descr_ptr->name; descr_ptr++)
  {
    fprintf(stream, " %5s or %c - %s\n", descr_ptr->name, descr_ptr->ch, descr_ptr->descr);
  }

  fprintf(
    stream,
    "\n"
    "This program is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation, either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n"
    );
}

int main(int argc, char ** argv)
{
  int counter;
  struct thread_params dsp_thread;
  struct thread_params disturb_thread;
  bool disturb;
  unsigned int dsp_counter;
  unsigned int disturb_counter;
  unsigned int dsp_counter_old;
  unsigned int disturb_counter_old;
  int dsp_cpu;
  int disturb_cpu;
  work dsp_work;
  work disturb_work;
  struct thread_params extra_threads[CPU_SETSIZE];
  int i;
  int max_cpus;
  work extra_work;

  if (argc != 1 && (argc < 5 || argc > 6))
  {
    help(stderr);
    exit(1);
  }

  max_cpus = get_avaliable_cpu_count();
  printf("%d cpu(s)\n", max_cpus);

  if (argc >= 5)
  {
    dsp_cpu = atoi(argv[1]);
    dsp_work = decode_work(argv[2]);
    disturb_cpu = atoi(argv[3]);
    disturb_work = decode_work(argv[4]);
  }
  else
  {
    dsp_cpu = 0;
    disturb_cpu = 1;
    dsp_work = work_float;
    disturb_work = work_float;
  }

  if (argc >= 6)
  {
    extra_work = decode_work(argv[5]);
  }
  else
  {
    max_cpus = 0;               /* disable extra threads */
  }

  for (i = 0; i < max_cpus; i++)
  {
    if (i != dsp_cpu && i != disturb_cpu)
    {
      if (!start_thread(extra_threads + i, "extra", 0, i, extra_work))
      {
        exit(1);
      }
    }
  }

  printf("-----------------------\n");
  printf("       dsp |  disturb\n");
  printf("cpu %6d |   %6d\n", dsp_cpu, disturb_cpu);
  printf("work %5s |   %6s\n", work_descr(dsp_work), work_descr(disturb_work));
  printf("-----------------------\n");

  if (!start_thread(&dsp_thread, "dsp", FIFO_PRIORITY, dsp_cpu, dsp_work))
  {
    exit(1);
  }

  disturb = false;

  dsp_thread.counter = 0;
  disturb_thread.counter = 0;
  dsp_counter = 0;
  disturb_counter = 0;
  dsp_counter_old = 0;
  disturb_counter_old = 0;

  for (counter = 0; counter < TEST_LENGTH; counter++)
  {
    if (counter % DISTURB_SWITCH_PERIOD == 0)
    {
      if (counter / DISTURB_SWITCH_PERIOD % 2 == 1)
      {
        if (start_thread(&disturb_thread, "disturb", FIFO_PRIORITY, disturb_cpu, disturb_work))
        {
          disturb = true;
        }
        else
        {
          exit(1);
        }
      }
      else
      {
        if (disturb)
        {
          if (stop_thread(&disturb_thread))
          {
            disturb_thread.counter = 0;
            disturb_counter = 0;
            disturb_counter_old = 0;
            disturb = false;
          }
          else
          {
            exit(1);
          }
        }
      }
    }

    usleep(1000000);
    dsp_counter = dsp_thread.counter;
    disturb_counter = disturb_thread.counter;
    printf("%10u %10u\n", dsp_counter - dsp_counter_old, disturb_counter - disturb_counter_old);
    dsp_counter_old = dsp_counter;
    disturb_counter_old = disturb_counter;
  }

  if (disturb)
  {
    stop_thread(&disturb_thread);
  }

  stop_thread(&dsp_thread);

  for (i = 0; i < max_cpus; i++)
  {
    if (i != dsp_cpu && i != disturb_cpu)
    {
      stop_thread(extra_threads + i);
    }
  }

  return 0;
}
