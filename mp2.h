#ifndef __MP1_INCLUDE__
#define __MP1_INCLUDE__

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include "mp2_given.h"

#define JIFF_TO_MS(t) ((t*1000)/ HZ)
#define MS_TO_JIFF(j) ((j * HZ) / 1000)

#define UPDATE_TIME 5000

#define PROC_DIRNAME "mp2"
#define PROC_FILENAME "status"
#define UPDATE_THREAD_NAME "kmp2"

struct task
{
  unsigned int pid;
  struct list_head task_node;
  struct task_struct *linux_task;
  struct timer_list wakeup_timer;
  unsigned long period;
  unsigned long computation;
};

//PROC FILESYSTEM ENTRIES
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *register_task_file;

struct task_struct* update_kthread;
int stop_thread=0;

LIST_HEAD(task_list);
static DEFINE_MUTEX(mutex);
#endif
