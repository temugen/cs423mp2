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

//REMOVE ALL TASKS FROM THE LIST, RESET THEIR SCHEDULING, AND FREE THEM
void _destroy_task_list(void)
{
    struct list_head *pos, *tmp;
    struct task *p;
    struct sched_param sparam_nice;

    sparam_nice.sched_priority = 0;

    list_for_each_safe(pos, tmp, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        list_del(pos);
        del_timer_sync(&p->wakeup_timer);
        sched_setscheduler(p->linux_task, SCHED_NORMAL, &sparam_nice);
        kfree(p);
    }
}

void _insert_task(struct task* t)
{
    BUG_ON(t == NULL);
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

//REMOVE TASK FROM LIST, MARK FOR DEREGISTRATION, CONTEXT SWITCH
int deregister_task(unsigned long pid)
{
    struct task *t;
    if((t = _lookup_task(pid)) == NULL)
        return -1;

    mutex_lock(&mutex);
    list_del(&t->task_node);
    mutex_unlock(&mutex);
    del_timer_sync(&t->wakeup_timer);
    t->state = DEREGISTERING;

    wake_up_process(dispatch_kthread);

    return 0;
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
        i += sprintf(page+off+i, "%lu: %lu %lu\n", p->pid, p->period, p->computation);
    }
    *eof = 1;
    return i;
}

//THIS IS THE TIMER HANDLER (INTERRUPT CONTEXT)
//THIS MUST BE VERY FAST SO WE USE A TWO HALVES APPROACH
void up_handler(unsigned long ptr)
{
    struct task *t = (struct task *)ptr;
    BUG_ON(t == NULL);
    set_timer(&t->wakeup_timer, t->period);
    t->state = READY;

    //SCHEDULE THE THREAD TO RUN (WAKE UP THE THREAD)
    wake_up_process(dispatch_kthread);
}

//ALLOCATE AND POPULATE TASK, ADD TO LIST
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
    newtask->state = REGISTERING;
    mutex_lock(&mutex);
    _insert_task(newtask);
    mutex_unlock(&mutex);

    return 0;
}

//SET THE INITIAL TIMER IF TASK IS REGISTERING, SLEEP TASK, CONTEXT SWITCH
int yield_task(unsigned long pid)
{
    struct task *t;
    if((t = _lookup_task(pid)) == NULL)
        return -1;

    switch(t->state)
    {
        case REGISTERING:
            set_timer(&t->wakeup_timer, t->period);
            t->state = READY;
            break;
        default:
            t->state = SLEEPING;
            break;
    }

    set_task_state(t->linux_task, TASK_UNINTERRUPTIBLE);
    wake_up_process(dispatch_kthread);

    return 0;
}

//CHECK UTILIZATION BOUNDS OF ALL PROCESSES
int can_schedule(unsigned long period, unsigned long computation)
{
    struct list_head *pos;
    struct task *p;
    //UP TO TWO EXTRA DECIMAL PLACES
    unsigned long sum = (computation * 100000) / period;

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

    proc_buffer = kmalloc(count, GFP_KERNEL);
    if(copy_from_user(proc_buffer, buffer, count) != 0)
        goto copy_fail;

    reg_type = proc_buffer[0];
    switch(reg_type)
    {
        case 'R':
            sscanf(proc_buffer, "%c, %lu, %lu, %lu", &reg_type, &pid, &period, &computation);

            if(!can_schedule(period, computation))
            {
                printk(KERN_ALERT "Task %lu is not schedulable\n", pid);
                break;
            }

            register_task(pid, period, computation);
            printk(KERN_ALERT "Register Task:%lu %lu %lu\n", pid, period, computation);
            break;
        case 'Y':
            sscanf(proc_buffer, "%c, %lu", &reg_type, &pid);
            yield_task(pid);
            break;
        case 'D':
            sscanf(proc_buffer, "%c, %lu", &reg_type, &pid);
            deregister_task(pid);
            printk(KERN_ALERT "Deregister Task:%lu\n", pid);
            break;
        default:
            printk(KERN_ALERT "Malformed registration string\n");
            break;
    }

copy_fail:
    kfree(proc_buffer);
    return count;
}

//FIND NEXT TASK READY TO RUN WITH HIGHEST PRIORITY
struct task *_get_next_task(void)
{
    struct list_head *pos;
    struct task *p;
    struct task *next_task = NULL;

    list_for_each(pos, &task_list)
    {
        p = list_entry(pos, struct task, task_node);
        //IF RUNNING TASK HAS HIGHER PRIORITY, CHOOSE IT
        if((p->state == READY || p->state == RUNNING) && (next_task == NULL || p->period < next_task->period))
            next_task = p;
    }

    return next_task;
}

//THIS IS THE THREAD FUNCTION (KERNEL CONTEXT)
//WE DO ALL THE UPDATE WORK HERE
int context_switch(void *data)
{
    struct sched_param sparam_nice, sparam_rt;
    struct task *next_task;
    struct task *currtask = NULL;

    sparam_nice.sched_priority = 0;
    sparam_rt.sched_priority = MAX_USER_RT_PRIO - 1;

    while(1)
    {
        mutex_lock(&mutex);
        if (stop_thread==1) break;
        next_task = _get_next_task();
        mutex_unlock(&mutex);

        if(next_task == currtask)
            goto sleep;

        if(currtask != NULL) //SWAP OUT OLD TASK
        {
            switch(currtask->state)
            {
                case DEREGISTERING:
                    sched_setscheduler(currtask->linux_task, SCHED_NORMAL, &sparam_nice);
                    kfree(currtask);
                    currtask = NULL;
                    break;
                case SLEEPING:
                    currtask = NULL;
                    break;
                default:
                    currtask->state = READY;
                    sched_setscheduler(currtask->linux_task, SCHED_NORMAL, &sparam_nice);
                    break;
            }
        }

        if(next_task != NULL) //SWAP IN NEW TASK
        {
            next_task->state = RUNNING;
            wake_up_process(next_task->linux_task);
            sched_setscheduler(next_task->linux_task, SCHED_FIFO, &sparam_rt);
            currtask = next_task;
        }

sleep:
        //SLEEP OUR THREAD AND SCHEDULE CHANGES
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
    struct sched_param sparam;

    proc_dir = proc_mkdir(PROC_DIRNAME, NULL);
    register_task_file = create_proc_entry(PROC_FILENAME, 0666, proc_dir);
    register_task_file->read_proc = proc_registration_read;
    register_task_file->write_proc = proc_registration_write;

    //DISPATCH THREAD WILL HAVE HIGHEST PRIORITY
    dispatch_kthread = kthread_create(context_switch, NULL, UPDATE_THREAD_NAME);
    sparam.sched_priority = MAX_RT_PRIO;
    sched_setscheduler(dispatch_kthread, SCHED_FIFO, &sparam);

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
    wake_up_process(dispatch_kthread);
    kthread_stop(dispatch_kthread);

    _destroy_task_list();
    printk(KERN_ALERT "MODULE UNLOADED\n");
}

//WE REGISTER OUR INIT AND EXIT FUNCTIONS HERE SO INSMOD CAN RUN THEM
//MODULE_INIT AND MODULE_EXIT ARE MACROS DEFINED IN MODULE.H
module_init(my_module_init);
module_exit(my_module_exit);

//THIS IS REQUIRED BY THE KERNEL
MODULE_LICENSE("GPL");
