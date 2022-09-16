#include <bits/stdc++.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
using namespace std;

#define PROC_FILE "/proc/partb_1_7"
#define SIZE_ULIMIT 100
#define pid_cout cout << "PID : " << getpid()
int main() {
    int fd = open(PROC_FILE, O_RDWR);
    if(fd < 0) {
        pid_cout << " failed to open proc file. " << endl;
    }

    char buffer[1];
    buffer[0] = SIZE_ULIMIT;
    int retval = write(fd, buffer, 1);
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
        retval = write(fd, arr, 2*sizeof(int));
        if(retval < 0) {
            pid_cout << "failed to write the value : (" << i.first << ", " << i.second << ")" << endl;
            close(fd);
            return -1;
        }
    }
    int output[1];

    for(int i = 0; i < 5; i++) {
        retval = read(fd, output, sizeof(int32_t));
        if(retval < 0) {
            pid_cout << "failed to read value" << endl;
            close(fd);
            return -1;
        }

        pid_cout << "Read : " << output[0] << endl;
    }

    pid_cout << "All Done !!" << endl;
    // expected output 2, 34, 1, -2, -4
    close(fd);
    return 0;

}