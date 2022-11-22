#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
using namespace std;
#define PB2_SET_CAPACITY    _IOW(0x10, 0x31, int32_t*)
#define PB2_INSERT_INT      _IOW(0x10, 0x32, int32_t*)
#define PB2_INSERT_PRIO     _IOW(0x10, 0x33, int32_t*)
#define PB2_GET_INFO        _IOR(0x10, 0x34, int32_t*)
#define PB2_GET_MIN         _IOR(0x10, 0x35, int32_t*)
#define PB2_GET_MAX         _IOR(0x10, 0x36, int32_t*)
#define PROC_FILE "/proc/CS60038_a2_Grp7"
#define SIZE_ULIMIT 100
#define pid_cout cout << "PID : " << getpid()


typedef struct _obj_info {
	int32_t prio_que_size; 	// current number of elements in priority-queue
	int32_t capacity;		// maximum capacity of priority-queue
} obj_info;

int main() {
    int fd = open(PROC_FILE, O_RDWR);
    if(fd < 0) {
        pid_cout << " failed to open proc file. " << endl;
    }

    // char buffer[1];
    int32_t cap = SIZE_ULIMIT;
    int retval = ioctl(fd, PB2_SET_CAPACITY, &cap);
    if(retval < 0) {
        pid_cout << " could not write capacity to proc file. " << endl;
        close(fd);
        return -1;
    }
    
    pid_cout << " initialized empty p_queue of capacity :" << SIZE_ULIMIT << endl;

    vector<pair<int,int>> input = {
        {1,2}, 
        {2,1}, 
        {-2,3}, 
        {-4,5}, 
        {90,10}, 
        {34,32}, 
        {34,1}, 
        {9, 34}, 
        {456, 79},
        {-543, 23},
        {-4521, 7}
    };
    int arr[2];
    for(auto i : input) {
        arr[0] = i.first;
        arr[1] = i.second;
        retval = ioctl(fd, PB2_INSERT_INT, &arr[0]);
        if(retval < 0) {
            pid_cout << "failed to write the value :" << i.first << endl;
            close(fd);
            return -1;
        }

        retval = ioctl(fd, PB2_INSERT_PRIO, &arr[1]);
        if(retval < 0) {
            pid_cout << "failed to write the priority :" << i.second << endl;
            close(fd);
            return -1;
        }

    }
    int output[1];

    for(int i = 0; i < 1; i++) {
        int32_t output = -1;
        retval = ioctl(fd, PB2_GET_MAX, &output);
        if(retval < 0) {
            pid_cout << "failed to read value" << endl;
            close(fd);
            return -1;
        }

        pid_cout << "Read Max : " << output << endl;

        retval = ioctl(fd, PB2_GET_MIN, &output);
        if(retval < 0) {
            pid_cout << "failed to read value" << endl;
            close(fd);
            return -1;
        }

        pid_cout << "Read Min : " << output << endl;

        obj_info outdata;
        retval = ioctl(fd, PB2_GET_MIN, &outdata);
        if(retval < 0) {
            pid_cout << "failed to read value" << endl;
            close(fd);
            return -1;
        }

        pid_cout << "Read Obj_Info : " << outdata.capacity << ", " << outdata.prio_que_size << endl;
    }

    pid_cout << "All Done !!" << endl;
    close(fd);
    return 0;

}