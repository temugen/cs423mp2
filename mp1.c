/****************************************************************************
CS423: Operating Systems Machine Problem 1
Illinois Open Source License

University of Illinois/NCSA
Open Source License

Copyright © 2011, The Board of Trustees of the University of Illinois.  All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal with the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimers.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimers in the documentation and/or other materials provided with the distribution.
* Neither the names of, Computer Sciences, the University of Illinois, nor the names of its contributors may be used to endorse or promote products derived from this Software without specific prior written permission.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE.

Contact Information:
Raoul Rivas Toledano
Department of Computer Science
University of Illinois at Urbana-Champaign
Email: trivas@uiuc.edu
******************************************************************************/

#include "mp1.h"

//HELPER FUNCTION TO INITIALIZE A TIMER WITH A SINGLE CALL
inline void timer_init(struct timer_list  *timer, void (*function)(unsigned long))
{
  BUG_ON(timer==NULL || function==NULL);
  init_timer(timer);
  timer->function=function;
  timer->data=(unsigned long) timer;
}

//THIS IS A HELPER FUNCTION TO SET A TIMER WITH ONE SINGLE CALL
//KERNEL TIMERS ARE ABOLUTE AND EXPRESSED IN JIFFIES SINCE BOOT.
//THIS HELPER FUNCTION SPECIFY THE RELATIVE TIME IN MILLISECONDS
inline void set_timer(struct timer_list* tlist, long release_time)
{
  BUG_ON(tlist==NULL);
  tlist->expires=jiffies+MS_TO_JIFF(release_time);
  mod_timer(tlist, tlist->expires);
}

void _destroy_task_list(void)
{
  struct list_head *pos, *tmp;
  struct mp1_task_stats *p;
 
  list_for_each_safe(pos, tmp, &mp1_task_list)
    {
      p = list_entry(pos, struct mp1_task_stats, task_node);
      list_del(pos);
      kfree(p);
    }
}

void _insert_task(struct mp1_task_stats* t)
{
  BUG_ON(t==NULL);
  list_add_tail(&t->task_node, &mp1_task_list);
}

void update_entries(void)
{    
  struct list_head *pos;
  struct mp1_task_stats *p;
  unsigned long cpuval;
 
  list_for_each(pos, &mp1_task_list)
    {
      p = list_entry(pos, struct mp1_task_stats, task_node);
      if (get_cpu_use(p->pid, &cpuval)==0) p->cpu_use=cpuval;
      else p->cpu_use=0;
    }
}

struct mp1_task_stats* _lookup_task(long pid)
{    
  struct list_head *pos;
  struct mp1_task_stats *p;
 
  list_for_each(pos, &mp1_task_list)
    {
      p = list_entry(pos, struct mp1_task_stats, task_node);
      if(p->pid == pid) 
	{
	  return p;
	}
    }

  return NULL;
}

int proc_registration_read(char *page, char **start, off_t off, int count, int* eof, void* data)
{
  off_t i;
  struct list_head *pos;
  struct mp1_task_stats *p;
 
  i=0;
 
  mutex_lock(&mp1_mutex);
  list_for_each(pos, &mp1_task_list)
    {
      p = list_entry(pos, struct mp1_task_stats, task_node);
      i+=sprintf(page+off+i, "%u: %lu\n", p->pid, p->cpu_use);
    }
 
  mutex_unlock(&mp1_mutex);
  *eof=1;
  return i;
}

int register_task(long pid)
{
   struct mp1_task_stats* newtask;

   if (_lookup_task(pid)!=NULL) return -1;

   newtask=kmalloc(sizeof(struct mp1_task_stats),GFP_KERNEL);
   newtask->pid=pid;
   newtask->cpu_use=0;
   mutex_lock(&mp1_mutex);
   _insert_task(newtask); 
   mutex_unlock(&mp1_mutex);

   return 0;
}

int proc_registration_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
  char *proc_buffer;
  unsigned int  pid;

  proc_buffer=kmalloc(count, GFP_KERNEL); 
  copy_from_user(proc_buffer, buffer, count);

  sscanf(proc_buffer, "%u", &pid); 
  register_task(pid);
  printk(KERN_ALERT "Register Task:%u\n", pid);

  kfree(proc_buffer);
  return count;
}

//THIS IS THE TIMER HANDLER (INTERRUPT CONTEXT)
//THIS MUST BE VERY FAST SO WE USE A TWO HALVES APPROACH
//WE DONT UPDATE HERE BUT SIGNAL THE THREAD THAT AN UPDATE MUST OCCUR
void up_handler(unsigned long ptr)
{
  //SCHEDULE THE THREAD TO RUN (WAKE UP THE THREAD)
  wake_up_process(update_kthread);
}

//THIS IS THE THREAD FUNCTION (KERNEL CONTEXT)
//WE DO ALL THE UPDATE WORK HERE
int scheduled_update(void *data)
{
    while(1)
    {
      mutex_lock(&mp1_mutex);
      if (stop_thread==1) break;
      printk(KERN_ALERT "STATS UPDATED\n");
      update_entries();
      mutex_unlock(&mp1_mutex);

      set_timer(&up_timer,UPDATE_TIME);
      set_current_state(TASK_INTERRUPTIBLE);
      schedule();
      set_current_state(TASK_RUNNING);
    }

    mutex_unlock(&mp1_mutex);
    return 0;
}

//THIS FUNCTION GETS EXECUTED WHEN THE MODULE GETS LOADED
//NOTE THE __INIT ANNOTATION AND THE FUNCTION PROTOTYPE
int __init my_module_init(void)
{
   timer_init(&up_timer, up_handler);
 
   mp1_proc_dir=proc_mkdir("mp1",NULL);
   register_task_file=create_proc_entry("status", 0666, mp1_proc_dir);
   register_task_file->read_proc= proc_registration_read;
   register_task_file->write_proc=proc_registration_write;

   update_kthread=kthread_create(scheduled_update, NULL,"kmp1");
   set_timer(&up_timer,UPDATE_TIME);
   //THE EQUIVALENT TO PRINTF IN KERNEL SPACE
   printk(KERN_ALERT "MODULE LOADED\n");
   return 0;   
}

//THIS FUNCTION GETS EXECUTED WHEN THE MODULE GETS UNLOADED
//NOTE THE __EXIT ANNOTATION AND THE FUNCTION PROTOTYPE
void __exit my_module_exit(void)
{
   remove_proc_entry("status", mp1_proc_dir);
   remove_proc_entry("mp1", NULL);
   del_timer_sync(&up_timer);

   stop_thread=1;
   wake_up_process(update_kthread);
   kthread_stop(update_kthread);

   _destroy_task_list();
   printk(KERN_ALERT "MODULE UNLOADED\n");
}

//WE REGISTER OUR INIT AND EXIT FUNCTIONS HERE SO INSMOD CAN RUN THEM
//MODULE_INIT AND MODULE_EXIT ARE MACROS DEFINED IN MODULE.H
module_init(my_module_init);
module_exit(my_module_exit);

//THIS IS REQUIRED BY THE KERNEL
MODULE_LICENSE("GPL");
