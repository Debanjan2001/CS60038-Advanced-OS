/**
 * @file : lkm_module.c
 * @authors : D. Saha  -&-  P. Godhani
 * @brief : An LKM(loadable kernel module) for Linux-5.6.9 Kernel that replaces read/write/open/release file API with a priority queue
 * @version : 1.0
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define DEVICE_NAME "partb_1_7"
#define current get_current()
#define INF 1000000000

MODULE_AUTHOR("PRIT_BOB");
MODULE_LICENSE("GPL");

static DEFINE_SPINLOCK(pq_mutex);

typedef struct _data {
    int32_t value;
    int32_t priority;
    int32_t in_time;
} data;

typedef struct _priority_queue{
    data *arr;
    int32_t capacity;
    int32_t count;
    int32_t timer;
} priority_queue;

typedef struct hashtable{
    int key;
    priority_queue *pq;
    struct hashtable *next;
} hashtable;

struct hashtable *htable;

/* Function Prototypes */
/* Priority Queue Methods */
static priority_queue* init_priority_queue(int32_t capacity);
static priority_queue* destroy_priority_queue(priority_queue* pq);
static int32_t push_value(priority_queue *pq, int32_t value, int32_t priority);
static int32_t pop_value(priority_queue *pq);
static void heapify_bottom_top(priority_queue *pq, int32_t index);
static void heapify_top_bottom(priority_queue *pq, int32_t parent_index);

/* Hashtable methods */
static hashtable* get_hashtable_entry(int key);
static void add_process_entry(hashtable* entry);
static void destroy_hashtable(void);
static void remove_process_entry(int key);
static void print_all_processes(void);

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);

static int num_open_processes = 0;

static struct proc_ops file_ops =
{
	.proc_open = dev_open,
	.proc_read = dev_read,
	.proc_write = dev_write,
	.proc_release = dev_release,
};

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

static void add_process_entry(hashtable* entry){
    entry->next = htable->next;
    htable->next = entry;
}

static void destroy_hashtable(void){
    hashtable *entry, *temp;
    entry = htable->next;
    while(entry != NULL){
        temp = entry;
        printk(KERN_INFO DEVICE_NAME ": (free_hashtable_entry) [key = %d]", entry->key);
        entry = entry->next;
        kfree(temp);
    }
    kfree(htable);
}

static void remove_process_entry(int key){
    hashtable *entry = htable->next;
    hashtable *prev = htable;
    while(entry != NULL){
        if(entry->key == key){
            prev->next = entry->next;
            destroy_priority_queue(entry->pq);
            entry->next = NULL;
            printk(KERN_INFO DEVICE_NAME ": (remove_process_entry) [pid = %d], [key = %d]", current->pid, entry->key);
            kfree(entry);
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

static void print_all_processes(void){
    hashtable *entry = htable->next;
    printk(KERN_INFO DEVICE_NAME ": (print_all_processes) Total %d processes", num_open_processes);
    while(entry != NULL){
        printk(KERN_INFO DEVICE_NAME ": (print_all_processes) [pid = %d]", entry->key);
		entry = entry->next;
    }
}

static priority_queue* init_priority_queue(int32_t capacity){
    priority_queue *pq = (priority_queue *)kmalloc(sizeof(priority_queue), GFP_KERNEL);

    // Failure Check
    if(pq == NULL){
        printk(KERN_ALERT DEVICE_NAME ": [pid = %d] Memory Error in allocating priority queue!", current->pid);
		return NULL;
    }

    pq->capacity = capacity;
    pq->count = 0;
    pq->timer = 0;
    pq->arr = (data *)kmalloc_array(capacity, sizeof(data), GFP_KERNEL);

    //check if allocation succeed
	if (pq->arr == NULL) {
		printk(KERN_ALERT DEVICE_NAME ": [pid = %d] Memory Error while allocating priority queue->arr!", current->pid);
		return NULL;
	}
    return pq;
}


static priority_queue* destroy_priority_queue(priority_queue* pq){
    if(pq == NULL){
        return pq;
    }
    printk(KERN_INFO DEVICE_NAME ": [pid = %d], %ld bytes of priority_queue->arr Space freed.\n", current->pid, sizeof(pq->arr));
	kfree_const(pq->arr);
	kfree_const(pq);
    return NULL;
}

static int32_t push_value(priority_queue *pq, int32_t value, int32_t priority){
    data d = (data){value, priority, pq->timer};

    if(pq->count >= pq->capacity){
        return -EACCES;
    }

    pq->timer += 1;
    pq->arr[pq->count] = d;
    heapify_bottom_top(pq, pq->count);
    pq->count += 1;

    return 0;
}

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


static ssize_t dev_write(struct file* file, const char* inbuffer, size_t inbuffer_size, loff_t* pos) {
    char arr[8];
    int32_t pq_size;
    int32_t pq_is_init = 0;
    hashtable *proc_entry;
    char buffer[256] = {0};
    int32_t buffer_size = 0;
    int32_t num;
    int32_t pri; 
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
        if(inbuffer_size != 8) {
            printk(KERN_ALERT DEVICE_NAME ": <dev_write> [PID:%d] %ld bytes received instead of expected 8 bytes[2*sizeof(int)].", current->pid, inbuffer_size);
            return -EINVAL;
        }

        memset(arr, 0, 8*sizeof(char));
        memcpy(arr, inbuffer, inbuffer_size*sizeof(char));
        memcpy(&num, arr, sizeof(num));
        memcpy(&pri, arr+sizeof(num), sizeof(pri));
        printk(KERN_INFO DEVICE_NAME ": <dev_write> [PID:%d] writing (%d, %d) to priority_queue.\n", current->pid, num, pri);

        ret = push_value(proc_entry->pq, num, pri);
        if(ret < 0) {
            return -EACCES;
        }
        return sizeof(num);
    }

    if(buffer_size != 1) {
        return -EACCES;
    }

    pq_size = inbuffer[0];
    printk(KERN_INFO DEVICE_NAME ": <dev_write> [PID:%d] priority_queue size recieved : %d.\n", current->pid, pq_size);

    if(pq_size <= 0 || pq_size > 100) {
        printk(KERN_ALERT DEVICE_NAME ": <dev_write> [PID:%d] priority_queue size must be integer in [0,100]. \n", current->pid);
        return -EINVAL;
    }

    proc_entry->pq = destroy_priority_queue(proc_entry->pq);
    proc_entry->pq = init_priority_queue(pq_size);
    return buffer_size;
}


static ssize_t dev_read(struct file* file, char* inbuffer, size_t inbuffer_size, loff_t* pos) {
    int32_t ret = -1;
    int32_t pq_is_init = 0;
    hashtable* proc_entry;
    int32_t pq_top_elem;
    int32_t pq_top_elem_pri;

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
        push_value(proc_entry->pq, pq_top_elem, pq_top_elem_pri);
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

static int dev_release(struct inode* inode, struct file* file) {
    spin_lock(&pq_mutex);
    remove_process_entry(current->pid);
    num_open_processes--;
    printk(KERN_INFO DEVICE_NAME ": <dev_released> [PID:%d] closed device. device currently opened by %d proc(s). \n", current->pid, num_open_processes);
    print_all_processes();
    spin_unlock(&pq_mutex);
    return 0;
}

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

static void land_module(void) {
    destroy_hashtable();
    remove_proc_entry(DEVICE_NAME, NULL);
    printk(KERN_INFO DEVICE_NAME ": <LKM_exit_module> priority_queue LKM terminated.\n");
}

module_init(launch_module);
module_exit(land_module);