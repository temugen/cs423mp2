#ifndef __MP1_INCLUDE__
#define __MP1_INCLUDE__

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <asm/uaccess.h>

#include "mp1_given.h"

#define JIFF_TO_MS(t) ((t*1000)/ HZ)
#define MS_TO_JIFF(j) ((j * HZ) / 1000)

#define UPDATE_TIME 5000

struct mp1_task_stats
{
  int pid;
  struct list_head task_node;
  long unsigned cpu_use;
};

//PROC FILESYSTEM ENTRIES
static struct proc_dir_entry *mp1_proc_dir;
static struct proc_dir_entry *register_task_file;

struct timer_list up_timer;
struct task_struct* update_kthread;
int stop_thread=0;

LIST_HEAD(mp1_task_list);
static DEFINE_MUTEX(mp1_mutex);
#endif
