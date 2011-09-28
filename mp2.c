#include "mp2.h"

//HELPER FUNCTION TO INITIALIZE A TIMER WITH A SINGLE CALL
inline void timer_init(struct timer_list  *timer, void (*function)(unsigned long))
{
    BUG_ON(timer == NULL || function == NULL);
    init_timer(timer);
    timer->function = function;
}

//THIS IS A HELPER FUNCTION TO SET A TIMER WITH ONE SINGLE CALL
//KERNEL TIMERS ARE ABOLUTE AND EXPRESSED IN JIFFIES SINCE BOOT.
//THIS HELPER FUNCTION SPECIFY THE RELATIVE TIME IN MILLISECONDS
inline void set_timer(struct timer_list* tlist, long release_time)
{
    BUG_ON(tlist==NULL);
    tlist->expires = jiffies+MS_TO_JIFF(release_time);
    mod_timer(tlist, tlist->expires);
}

void _destroy_task_list(void)
{
    struct list_head *pos, *tmp;
    struct task *p;

    list_for_each_safe(pos, tmp, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        list_del(pos);
        del_timer_sync(&p->wakeup_timer);
        kfree(p);
    }
}

void _insert_task(struct task* t)
{
    BUG_ON(t==NULL);
    list_add_tail(&t->task_node, &task_list);
}

struct task* _lookup_task(unsigned long pid)
{
    struct list_head *pos;
    struct task *p;

    list_for_each(pos, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        if(p->pid == pid)
            return p;
    }

    return NULL;
}

void deregister_task(unsigned long pid)
{
    struct task *t;
    if((t = _lookup_task(pid)) == NULL)
        return;

    mutex_lock(&mutex);
    list_del(&t->task_node);
    mutex_unlock(&mutex);
    del_timer_sync(&t->wakeup_timer);
    kfree(t);
}

int proc_registration_read(char *page, char **start, off_t off, int count, int* eof, void* data)
{
    off_t i;
    struct list_head *pos;
    struct task *p;

    i = 0;

    list_for_each(pos, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        i += sprintf(page+off+i, "%u: %lu %lu\n", p->pid, p->period, p->computation);
    }
    *eof = 1;
    return i;
}

//THIS IS THE TIMER HANDLER (INTERRUPT CONTEXT)
//THIS MUST BE VERY FAST SO WE USE A TWO HALVES APPROACH
//WE DONT UPDATE HERE BUT SIGNAL THE THREAD THAT AN UPDATE MUST OCCUR
void up_handler(unsigned long ptr)
{
    struct task *t = (struct task *)ptr;
    BUG_ON(t == NULL);
    set_timer(&t->wakeup_timer, t->period);
    t->state = READY;

    //SCHEDULE THE THREAD TO RUN (WAKE UP THE THREAD)
    wake_up_process(update_kthread);
}

int register_task(unsigned long pid, unsigned long period, unsigned long computation)
{
    struct task* newtask;

    if (_lookup_task(pid) != NULL) return -1;

    newtask = kmalloc(sizeof(struct task),GFP_KERNEL);
    newtask->pid = pid;
    timer_init(&newtask->wakeup_timer, up_handler);
    newtask->wakeup_timer.data = (unsigned long)newtask;
    newtask->linux_task = find_task_by_pid(pid);
    newtask->period = period;
    newtask->computation = computation;
    newtask->state = SLEEPING;
    mutex_lock(&mutex);
    _insert_task(newtask);
    mutex_unlock(&mutex);

    return 0;
}

int can_schedule(unsigned long period, unsigned long computation)
{
    struct list_head *pos;
    struct task *p;
    unsigned long sum = (computation * 100000) / period;

    //Check scheduling up to two decimal places
    list_for_each(pos, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        sum += (p->computation * 100000) / p->period;
    }

    if(sum <= 69300)
        return 1;
    else
        return 0;
}

int proc_registration_write(struct file *file, const char *buffer, unsigned long count, void *data)
{
    char *proc_buffer;
    char reg_type;
    unsigned long pid, period, computation;
    struct task *t;

    proc_buffer = kmalloc(count, GFP_KERNEL);
    copy_from_user(proc_buffer, buffer, count);

    reg_type = proc_buffer[0];

    switch(reg_type)
    {
        case 'R':
            sscanf(proc_buffer, "%c, %lu, %lu, %lu", &reg_type, &pid, &period, &computation);

            if(!can_schedule(period, computation))
            {
                printk(KERN_ALERT "Cannot register task:%lu %lu %lu\n", pid, period, computation);
                break;
            }

            register_task(pid, period, computation);
            printk(KERN_ALERT "Register Task:%lu %lu %lu\n", pid, period, computation);
            break;
        case 'Y':
            sscanf(proc_buffer, "%c, %lu", &reg_type, &pid);
            t = _lookup_task(pid);

            if(t->state == SLEEPING) //THIS IS OUR FIRST YIELD
            {
                set_timer(&t->wakeup_timer, t->period);
                t->state = READY;
            }
            else
            {
                t->state = SLEEPING;
            }

            set_task_state(t->linux_task, TASK_UNINTERRUPTIBLE);
            wake_up_process(update_kthread);
            printk(KERN_ALERT "Yield Task:%lu\n", pid);
            break;
        case 'D':
            sscanf(proc_buffer, "%c, %lu", &reg_type, &pid);
            t = _lookup_task(pid);
            deregister_task(pid);
            printk(KERN_ALERT "Deregister Task:%lu\n", pid);
            break;
        default:
            printk(KERN_ALERT "Malformed registration string\n");
            break;
    }

    kfree(proc_buffer);
    return count;
}

struct task *_get_next_task(void)
{
    struct list_head *pos;
    struct task *p;
    struct task *next_task = NULL;

    list_for_each(pos, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        //IF RUNNING TASK HAS HIGHEST PRIORITY, CHOOSE IT
        if((p->state == READY || p->state == RUNNING) && (next_task == NULL || p->period < next_task->period))
            next_task = p;
    }

    return next_task;
}

//THIS IS THE THREAD FUNCTION (KERNEL CONTEXT)
//WE DO ALL THE UPDATE WORK HERE
int context_switch(void *data)
{
    struct sched_param sparam;
    struct task *next_task;

    while(1)
    {
        mutex_lock(&mutex);
        if (stop_thread==1) break;
        printk(KERN_ALERT "CONTEXT SWITCH\n");
        next_task = _get_next_task();
        mutex_unlock(&mutex);

        if(next_task == currtask)
            goto same_task;

        if(next_task != NULL) //SWAP IN NEW TASK
        {
            printk("swapping in %u\n", next_task->pid);
            next_task->state = RUNNING;
            wake_up_process(next_task->linux_task);
            sparam.sched_priority = MAX_USER_RT_PRIO - 1;
            sched_setscheduler(next_task->linux_task, SCHED_FIFO, &sparam);
        }

        if(currtask != NULL) //SWAP OUT OLD TASK
        {
            printk("swapping out %u\n", currtask->pid);
            if(currtask->state == SLEEPING) // TASK HAS YIELDED
            {
                //WILL SWITCH TO NEW TASK IF ONE EXISTS, OTHERWISE NULL
                currtask = next_task;
                goto same_task;
            }

            //TASK IS STILL RUNNING
            currtask->state = READY;
            sparam.sched_priority = 0;
            sched_setscheduler(currtask->linux_task, SCHED_NORMAL, &sparam);
        }

        if(next_task != NULL)
            currtask = next_task;

same_task:
        //SLEEP OUR THREAD
        set_current_state(TASK_INTERRUPTIBLE);
        schedule();
        set_current_state(TASK_RUNNING);
    }

    mutex_unlock(&mutex);
    return 0;
}

//THIS FUNCTION GETS EXECUTED WHEN THE MODULE GETS LOADED
//NOTE THE __INIT ANNOTATION AND THE FUNCTION PROTOTYPE
int __init my_module_init(void)
{
    currtask = NULL;
    calling_task = NULL;

    proc_dir = proc_mkdir(PROC_DIRNAME, NULL);
    register_task_file = create_proc_entry(PROC_FILENAME, 0666, proc_dir);
    register_task_file->read_proc = proc_registration_read;
    register_task_file->write_proc =proc_registration_write;

    update_kthread = kthread_create(context_switch, NULL, UPDATE_THREAD_NAME);

    //THE EQUIVALENT TO PRINTF IN KERNEL SPACE
    printk(KERN_ALERT "MODULE LOADED\n");
    return 0;
}

//THIS FUNCTION GETS EXECUTED WHEN THE MODULE GETS UNLOADED
//NOTE THE __EXIT ANNOTATION AND THE FUNCTION PROTOTYPE
void __exit my_module_exit(void)
{
    remove_proc_entry(PROC_FILENAME, proc_dir);
    remove_proc_entry(PROC_DIRNAME, NULL);

    stop_thread = 1;
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
