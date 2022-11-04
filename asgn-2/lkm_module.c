/**
 * @file : lkm_module.c
 * @authors : D. Saha  -&-  P. Godhani
 * @brief : an LKM that supports various ioctl calls to set and retrieve various configurations and internal information respectively
 * @version : 1.0
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>


/* ioctl commands */
#define PB2_SET_CAPACITY    _IOW(0x10, 0x31, int32_t*)
#define PB2_INSERT_INT      _IOW(0x10, 0x32, int32_t*)
#define PB2_INSERT_PRIO     _IOW(0x10, 0x33, int32_t*)
#define PB2_GET_INFO        _IOR(0x10, 0x34, int32_t*)
#define PB2_GET_MIN         _IOR(0x10, 0x35, int32_t*)
#define PB2_GET_MAX         _IOR(0x10, 0x36, int32_t*)

#define DEVICE_NAME "bob_prit_asgn2"
#define current get_current()
#define INF 1000000000

MODULE_AUTHOR("PRIT_BOB");
MODULE_LICENSE("GPL");


typedef struct _obj_info {
	int32_t prio_que_size; 	// current number of elements in priority-queue
	int32_t capacity;		// maximum capacity of priority-queue
} obj_info;

/* Data structure definitions */
/* Data struct that is stored in the priority_queue */
typedef struct _data {
    int32_t value;
    int32_t priority;
    int32_t in_time;
} data;

/* priority_queue struct */
typedef struct _priority_queue{
    data *arr;
    int32_t capacity;
    int32_t count;
    int32_t timer;
    /* 
     * 1 = To be read => value 
     * 2 = To be read => priority
     */
    int32_t input_state;
} priority_queue;

/* hashtable struct that maps individual priority_queue's to processes using PID's */
typedef struct hashtable{
    int key;
    priority_queue *pq;
    struct hashtable *next;
} hashtable;

// A spinlock to avoid concurrency issues when the global hashtable is accessed/modified.
static DEFINE_SPINLOCK(pq_mutex);

// Global hashtable that maps pid's to priority_queues
struct hashtable *htable;

// Global variable to keep track of the number of process currently using the LKM 
static int num_open_processes = 0;

/* Priority Queue Methods */
static priority_queue* init_priority_queue(int32_t capacity);
static priority_queue* destroy_priority_queue(priority_queue* pq);
static int32_t push_value(priority_queue *pq, int32_t num);
static int32_t pop_value(priority_queue *pq);
static void heapify_bottom_top(priority_queue *pq, int32_t index);
static void heapify_top_bottom(priority_queue *pq, int32_t parent_index);

/* Hashtable methods */
static hashtable* get_hashtable_entry(int key);
static void add_process_entry(hashtable* entry);
static void destroy_hashtable(void);
static void remove_process_entry(int key);
static void print_all_processes(void);

/* API used by user process whenever they try to write to the /proc file */
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static long dev_ioctl(struct file *, unsigned int, unsigned long);

/* map the /proc file function calls to the LKM functions that serve the desired input */
static struct proc_ops file_ops =
{
	.proc_open = dev_open,
	.proc_read = dev_read,
	.proc_write = dev_write,
	.proc_release = dev_release,
    .proc_ioctl = dev_ioctl,
};

// hashtable lookup function : returns a hashtable entry for the given pid(key)
static hashtable* get_hashtable_entry(int key){
    hashtable *entry = htable->next;
    while(entry != NULL){
        if(entry->key == key){
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

// hashtable insert function : inserts the given entry in the hashtable
static void add_process_entry(hashtable* entry){
    entry->next = htable->next;
    htable->next = entry;
}

// hashtable destroy function : deletes all entries and the empties the hashtable
static void destroy_hashtable(void){
    hashtable *entry, *temp;
    entry = htable->next;
    while(entry != NULL){
        temp = entry;
        printk(KERN_INFO DEVICE_NAME ": <free_hashtable_entry> [key = %d]", entry->key);
        entry = entry->next;
        kfree(temp);
    }
    kfree(htable);
}

// hashtable delete function : deletes entry with the given key(pid)
static void remove_process_entry(int key){
    hashtable *entry = htable->next;
    hashtable *prev = htable;
    while(entry != NULL){
        if(entry->key == key){
            prev->next = entry->next;
            destroy_priority_queue(entry->pq);
            entry->next = NULL;
            printk(KERN_INFO DEVICE_NAME ": <remove_process_entry> [PID:%d], [key = %d]", current->pid, entry->key);
            kfree(entry);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

// hashtable print function : print all the processes currently present in the hashtable
static void print_all_processes(void){
    hashtable *entry = htable->next;
    printk(KERN_INFO DEVICE_NAME ": <print_all_processes> Total %d processes", num_open_processes);
    while(entry != NULL){
        printk(KERN_INFO DEVICE_NAME ": <print_all_processes> [PID:%d]", entry->key);
		entry = entry->next;
    }
}

// pq init function : creates an empty priority queue
static priority_queue* init_priority_queue(int32_t capacity){
    priority_queue *pq = (priority_queue *)kmalloc(sizeof(priority_queue), GFP_KERNEL);

    // Failure Check
    if(pq == NULL){
        printk(KERN_ALERT DEVICE_NAME ": [PID:%d] Memory Error in allocating priority queue!", current->pid);
		return NULL;
    }

    pq->capacity = capacity;
    pq->count = 0;
    pq->timer = 0;
    pq->arr = (data *)kmalloc_array(capacity, sizeof(data), GFP_KERNEL);
    pq->input_state = 1;

    //check if allocation succeed
	if (pq->arr == NULL) {
		printk(KERN_ALERT DEVICE_NAME ": [PID:%d] Memory Error while allocating priority queue->arr!", current->pid);
		return NULL;
	}
    return pq;
}

// pq destroy function : deletes all nodes, and empties the priority queue
static priority_queue* destroy_priority_queue(priority_queue* pq){
    if(pq == NULL){
        return pq;
    }
    printk(KERN_INFO DEVICE_NAME ": [PID:%d], %ld bytes of priority_queue->arr Space freed.\n", current->pid, sizeof(pq->arr));
	kfree_const(pq->arr);
	kfree_const(pq);
    return NULL;
}

// pq insert function : insert given value in the priority_queue
/** @note we maintain a state variable in the priority queue struct that 
 * keeps track whether the input value is a number 
 * or priority since both will be given by the user 
 * process via identical write() calls
 */
static int32_t push_value(priority_queue *pq, int32_t num) {
    // data d = (data){value, priority, pq->timer};

    if(pq->count >= pq->capacity){
        return -EACCES;
    }

    if(pq->input_state == 1){
        pq->arr[pq->count].value = num;
        pq->arr[pq->count].in_time = pq->timer;
        
        pq->input_state = 2;
    }else{
        if(num < 0){
            return -EINVAL;
        }
        pq->arr[pq->count].priority = num;
        heapify_bottom_top(pq, pq->count);
        pq->count += 1;
        pq->timer += 1;

        pq->input_state = 1;
    }

    return 0;
}

// pq delete function : remvoes the top element of the priority_queue
static int32_t pop_value(priority_queue *pq){
    data d = pq->arr[0];

    if(pq->count == 0){
        return -INF;
    }

    pq->arr[0] = pq->arr[pq->count - 1];
    pq->count -=1;
    heapify_top_bottom(pq, 0);

    return d.value;
}

// pq helper function 1 : rearranges nodes to maintain priority order in the priority queue
static void heapify_bottom_top(priority_queue *pq, int32_t index){
    int32_t parent;
    data temp;
    while(index != 0){
        parent = (index-1)/2;
        if( (pq->arr[parent].priority > pq->arr[index].priority) 
            ||
            (
                (pq->arr[index].priority == pq->arr[parent].priority)
                &&
                (pq->arr[parent].in_time > pq->arr[index].in_time)
            )
        ){
            temp = pq->arr[parent];
            pq->arr[parent] = pq->arr[index];
            pq->arr[index] = temp;
            index = parent;
        }else{
            break;
        }
    }
}

// pq helper function 2 : rearranges nodes to maintain priority order in the priority queue
static void heapify_top_bottom(priority_queue *pq, int32_t parent_index){
    int32_t left_child = parent_index * 2 + 1;
    int32_t right_child = parent_index * 2 + 2;
    int32_t smallest=parent_index;

    if(left_child < pq->count){
        if( (pq->arr[smallest].priority > pq->arr[left_child].priority) 
            ||
            (
                (pq->arr[left_child].priority == pq->arr[smallest].priority)
                &&
                (pq->arr[smallest].in_time > pq->arr[left_child].in_time)
            )
        ){
            smallest = left_child;
        }
    }

    if(right_child < pq->count){
        if( (pq->arr[smallest].priority > pq->arr[right_child].priority) 
            ||
            (
                (pq->arr[right_child].priority == pq->arr[smallest].priority)
                &&
                (pq->arr[smallest].in_time > pq->arr[right_child].in_time)
            )
        ){
            smallest = right_child;
        }
    }

    if(smallest != parent_index){
        data temp = pq->arr[smallest];
        pq->arr[smallest] = pq->arr[parent_index];
        pq->arr[parent_index] = temp;
        heapify_top_bottom(pq, smallest);
    }
}

// WRITE : recieves values (size, number and priority) from the user procs
// models the write() signature
static ssize_t dev_write(struct file* file, const char* inbuffer, size_t inbuffer_size, loff_t* pos) {
    char arr[8];
    int32_t pq_size;
    int32_t pq_is_init = 0;
    hashtable *proc_entry;
    char buffer[256] = {0};
    int32_t buffer_size = 0;
    int32_t num;
    int32_t ret;

    if(!inbuffer || !inbuffer_size) 
        return -EINVAL;
    
    if(copy_from_user(buffer,inbuffer,inbuffer_size<256? inbuffer_size : 256))
        return -ENOBUFS;

    proc_entry = get_hashtable_entry(current->pid);
    if(proc_entry == NULL) {
        printk(KERN_ALERT DEVICE_NAME ": <dev_write> [PID:%d] hashtable entry for current pid is non-existent", current->pid);
        return -EACCES;
    }

    pq_is_init = (proc_entry->pq) ? 1 : 0;
    buffer_size = inbuffer_size < 256 ? inbuffer_size : 256;

    if(pq_is_init) {
        if(inbuffer_size != 4) {
            printk(KERN_ALERT DEVICE_NAME ": <dev_write> [PID:%d] %ld bytes received instead of expected 8 bytes[2*sizeof(int)].", current->pid, inbuffer_size);
            return -EINVAL;
        }

        memset(arr, 0, 4*sizeof(char));
        memcpy(arr, inbuffer, inbuffer_size*sizeof(char));
        memcpy(&num, arr, sizeof(num));

        if(proc_entry->pq->input_state == 1){
            printk(KERN_INFO DEVICE_NAME ": <dev_write> [PID:%d] received value=%d for inserting into priority_queue.\n", current->pid, num);
        }else{
            printk(KERN_INFO DEVICE_NAME ": <dev_write> [PID:%d] received priority=%d for inserting into priority_queue.\n", current->pid, num);
        }

        ret = push_value(proc_entry->pq, num);
        if(ret < 0) {
            return -EACCES;
        }
        return sizeof(num);
    }

    if(buffer_size != 1) {
        return -EACCES;
    }

    pq_size = buffer[0];
    printk(KERN_INFO DEVICE_NAME ": <dev_write> [PID:%d] priority_queue size recieved : %d.\n", current->pid, pq_size);

    if(pq_size <= 0 || pq_size > 100) {
        printk(KERN_ALERT DEVICE_NAME ": <dev_write> [PID:%d] priority_queue size must be integer in [0,100]. \n", current->pid);
        return -EINVAL;
    }

    proc_entry->pq = destroy_priority_queue(proc_entry->pq);
    proc_entry->pq = init_priority_queue(pq_size);
    return buffer_size;
}

// READ : returns values to the user procs 
// models the read() signature
static ssize_t dev_read(struct file* file, char* inbuffer, size_t inbuffer_size, loff_t* pos) {
    int32_t ret = -1;
    int32_t pq_is_init = 0;
    hashtable* proc_entry;
    int32_t pq_top_elem;

    if(!inbuffer || !inbuffer_size) {
        return -EINVAL;
    }

    proc_entry = get_hashtable_entry(current->pid);
    if(proc_entry == NULL) {
        printk(KERN_ALERT DEVICE_NAME ": <dev_read> [PID:%d] hashtable entry for current pid is non-existent", current->pid);
        return -EACCES;
    }

    pq_is_init = (proc_entry->pq) ? 1 : 0;

    if(!pq_is_init) {
        printk(KERN_ALERT DEVICE_NAME ": <dev_read> [PID:%d] priority_queue not initialized.\n", current->pid);
        return -EACCES;
    }

    if(sizeof(pq_top_elem) != inbuffer_size) {
        printk(KERN_INFO DEVICE_NAME ": <dev_read> [PID:%d] failed to send top of priority_queue due to invalid read by user proc. \n", current->pid);
        return -EACCES;
    }
    pq_top_elem = pop_value(proc_entry->pq);
    
    printk(KERN_INFO DEVICE_NAME ": <dev_read> [PID:%d] expecting %ld bytes.\n", current->pid, inbuffer_size);
    ret = copy_to_user(inbuffer, (int32_t*)&pq_top_elem, inbuffer_size < sizeof(pq_top_elem) ? inbuffer_size : sizeof(pq_top_elem));
    if(ret == 0 && pq_top_elem != -INF) {
        printk(KERN_INFO DEVICE_NAME ": <dev_read> [PID:%d] sending data [%ld bytes] with value = %d to the user proc. \n ", current->pid, sizeof(pq_top_elem), pq_top_elem);
        return sizeof(pq_top_elem);
    } else {
        printk(KERN_INFO DEVICE_NAME ": <dev_read> [PID:%d] failed to send to the user proc.\n", current->pid );
        return -EACCES;
    }
}

// OPEN : opens a new priority queue, generates a new hashtable entry
// models the open() signature
// @note : before changing the hashtable spinlock is acquired
static int dev_open(struct inode* inode, struct file* file) {
    hashtable* proc_entry;
    if(get_hashtable_entry(current->pid) != NULL) {
        printk(KERN_ALERT DEVICE_NAME ": <dev_open> [PID:%d] process tried to open file twice.\n", current->pid);
        return -EACCES;
    }

    proc_entry = (hashtable *)kmalloc(sizeof(hashtable), GFP_KERNEL);
    *proc_entry = (hashtable) {current->pid, NULL, NULL};


    spin_lock(&pq_mutex);
    printk(DEVICE_NAME ": <dev_open> [PID:%d] adding %d to hashtable.\n", current->pid, proc_entry->key);
    add_process_entry(proc_entry);

    num_open_processes ++;
    printk(KERN_INFO DEVICE_NAME ": <dev_open> [PID:%d] device openend by %d proc(s). \n", current->pid, num_open_processes);
    print_all_processes();
    spin_unlock(&pq_mutex);
    return 0;
}

// release : empties the corresponding priority_queue, deletes the proc entry from the hashtable and
// models the release() signature
// @note : before changing the hashtable spinlock is acquired
static int dev_release(struct inode* inode, struct file* file) {
    spin_lock(&pq_mutex);
    remove_process_entry(current->pid);
    num_open_processes--;
    printk(KERN_INFO DEVICE_NAME ": <dev_released> [PID:%d] closed device. device currently opened by %d proc(s). \n", current->pid, num_open_processes);
    print_all_processes();
    spin_unlock(&pq_mutex);
    return 0;
}


/* handle ioctl commands for device */
static long dev_ioctl(struct file *file, unsigned int command, unsigned long arg) 
{
    hashtable *proc_entry;
    int32_t pq_size;
    int32_t value;
    int32_t priority;
    int32_t retval;
	obj_info pq_info;

    switch (command){
        case PB2_SET_CAPACITY:
            proc_entry = get_hashtable_entry(current->pid);/* get hashtable entry corresponding to the current process PID */
            if (proc_entry == NULL) {
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_SET_CAPACITY) (PID %d) Process entry does not exist", current->pid);
                return -EACCES;
            }

            if (copy_from_user(&pq_size, (int *)arg, sizeof(int32_t)))
                return -EINVAL;
        
            printk(KERN_INFO DEVICE ": (dev_ioctl : PB2_SET_CAPACITY) (PID %d) Priority Queue Size received: %d", current->pid, deque_size);

            /* check pq size */
            if (pq_size <= 0 || pq_size > 100){
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_SET_CAPACITY) (PID %d) Priority Queue size value must be in the range between 1 and 100 (both inclusive)", current->pid);
                return -EINVAL;
            }

            proc_entry->pq = destroy_priority_queue(proc_entry->pq);
            proc_entry->pq = init_priority_queue(pq_size); /* allocate space for new priority_queue */
            break;

        case PB2_INSERT_INT:

            proc_entry = get_hashtable_entry(current->pid);/* get hashtable entry corresponding to the current process PID */
            if (proc_entry == NULL) {
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_INSERT_INT) (PID %d) Process entry does not exist", current->pid);
                return -EACCES;
            }

            if(proc_entry->pq == NULL){
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_INSERT_INT) (PID %d) Priority Queue not initialized", current->pid);
			    return -EACCES;
            }

            if( copy_from_user(&value, (int32_t *)arg, sizeof(int32_t)) ){
                return -EINVAL;
            }

            printk(KERN_INFO DEVICE ": (dev_ioctl : PB2_INSERT_INT) (PID %d) Writing %d to Priority Queue\n", current->pid, num);

            retval = push_value(proc_entry->pq, value);
            if(retval < 0){
                return retval;
            }
            break;

        case PB2_INSERT_PRIO:
            proc_entry = get_hashtable_entry(current->pid);/* get hashtable entry corresponding to the current process PID */
            if (proc_entry == NULL) {
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_INSERT_PRIO) (PID %d) Process entry does not exist", current->pid);
                return -EACCES;
            }

            if(proc_entry->pq == NULL){
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_INSERT_PRIO) (PID %d) Priority Queue not initialized", current->pid);
			    return -EACCES;
            }

            if( copy_from_user(&priority, (int32_t *)arg, sizeof(int32_t)) ){
                return -EINVAL;
            }

            printk(KERN_INFO DEVICE ": (dev_ioctl : PB2_INSERT_PRIO) (PID %d) Writing prio = %d to Priority Queue\n", current->pid, num);

            retval = push_value(proc_entry->pq, value);
            if(retval < 0){
                return retval;
            }
            break;

        case PB2_GET_INFO:
            proc_entry = get_hashtable_entry(current->pid);/* get hashtable entry corresponding to the current process PID */
            if (proc_entry == NULL) {
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_GET_INFO) (PID %d) Process entry does not exist", current->pid);
                return -EACCES;
            }

            if(proc_entry->pq == NULL){
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_GET_INFO) (PID %d) Priority Queue not initialized", current->pid);
			    return -EACCES;
            }

            pq_info.prio_que_size = proc_entry->pq->count;
            pq_info.capacity = proc_entry->pq->capacity;

            retval = copy_to_user((obj_info *)arg, &pq_info, sizeof(obj_info));
		    if (retval != 0){
			    return -EACCES;
            }
            break;

        case PB2_GET_MIN:
            proc_entry = get_hashtable_entry(current->pid);/* get hashtable entry corresponding to the current process PID */
            if (proc_entry == NULL) {
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_GET_MIN) (PID %d) Process entry does not exist", current->pid);
                return -EACCES;
            }

            if(proc_entry->pq == NULL){
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_GET_MIN) (PID %d) Priority Queue not initialized", current->pid);
			    return -EACCES;
            }

            if(proc_entry->pq->count == 0){
                printk(KERN_ALERT DEVICE ": (dev_ioctl : PB2_GET_MIN) (PID %d) Priority Queue is empty", current->pid);
			    return -EACCES;
            }

            value = pop_value(proc_entry->pq);
            retval = copy_to_user((int32_t*)arg, (int32_t*)&value, sizeof(int32_t));
            if(retval != 0){
                printk(KERN_INFO DEVICE ": (dev_ioctl : PB2_GET_MIN) (PID %d) Error! Unable to send data of %ld bytes with value %d to the user process", current->pid, sizeof(value), value);
                return -EACCES;
            }

            printk(KERN_INFO DEVICE ": (dev_ioctl : PB2_GET_MIN) (PID %d) Sending data of %ld bytes with value %d to the user process", current->pid, sizeof(value), value);
            break;

        case PB2_GET_MAX:
            /*
             * Left for discussion and writing
            */
            break;

        default: 
            return -EINVAL;

    }
    return 0;
}

// init_module overload
static int launch_module(void) {
    struct proc_dir_entry *proc_entry = proc_create(DEVICE_NAME, 0, NULL, &file_ops);
    if(!proc_entry) return -ENOENT;

    htable = kmalloc(sizeof(hashtable), GFP_KERNEL);
    if(htable == NULL) {
        printk(KERN_ALERT DEVICE_NAME ": <LKM_init_module> insufficient memory.\n");
        return -ENOMEM;
    }

    *htable = (hashtable) {-1, NULL, NULL};
    printk(KERN_INFO DEVICE_NAME ": <LKM_init_module> priority_queue LKM initialized.\n");
    spin_lock_init(&pq_mutex);
    return 0;
}

// cleanup_module overload
static void land_module(void) {
    destroy_hashtable();
    remove_proc_entry(DEVICE_NAME, NULL);
    printk(KERN_INFO DEVICE_NAME ": <LKM_exit_module> priority_queue LKM terminated.\n");
}

module_init(launch_module);
module_exit(land_module);