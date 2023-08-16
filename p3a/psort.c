#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/sysinfo.h> // doesn't exist on MacOS
#include <sys/stat.h>
#include <sys/mman.h>

typedef struct {
    void *start_addr; // starting address of chunk to sort
    void *end_addr;
    size_t num_sort; // number of records, not number of bytes
    int num_threads;
} args;

// function to swap elements in memory
void swap(void *a, void *b) {
    char *tmp = malloc(100);
    if (tmp == NULL) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    memcpy(tmp, a, 100);
    memcpy(a, b, 100);
    memcpy(b, tmp, 100);
    free(tmp);
}

// insertion sort for short arrays
void insertionSort(void *start, int n) {
    void *loc = malloc(100);
    if (loc == NULL) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    int i, j;
    for (i = 1; i < n; i++) {
        memcpy(loc, start+(i*100), 100); // to compare for swapping
	j = i - 1; // index in the array
	
	while (j >= 0 && *(int *)(start+(j*100)) > *(int *)loc) {
            memcpy(start+((j+1)*100), start+(j*100), 100);
            j = j - 1;
        }
        memcpy(start+((j+1)*100), loc, 100);
    }
}


// iterative verion of quicksort to reduce recursive calls
void* quicksort_iterative(void *arguments) {
    args *arg = (args*) arguments;
    void *start = arg->start_addr; // pointer to the beginning
    size_t size = arg->num_sort;
    if (size <= 10) {
	insertionSort(start, size);
        return arg->start_addr;
    }

    // Create a stack
    int stack[size];
    int top = -1; // top of the stack
 
    // push initial indexes in the mem space
    stack[++top] = 0; 
    stack[++top] = size-1;
 
    int first;
    int last;
    // Keep popping from stack while is not empty
    while (top >= 0) {
        // Pop h and l
        last = stack[top--];
        first = stack[top--];

        // partition
        int pivot = *(int *)(start+(last*100)); // rightmost element is the pivot, extract the int key
        int i = first-1; // pointer to an element greater than the pivot

        for(int j = first; j < last; j++) {
            void *loc = start+(j*100); // location in mem is by 100 byte records
            if (*(int *)loc < pivot) { // compare to the pivot looking for an element less
                i++; 
                swap(loc, start+(i*100)); // swap the lower element with the greater one
            }
        }
        // swap pivot with the pointer
        swap(start+((i+1)*100), start+(last*100));
        // index of pivot element
        int p = i + 1;
 
        // If there are elements on left side of pivot, push to stack
        if (p - 1 > first) {
            stack[++top] = first;
            stack[++top] = p - 1;
        }
 
        // If there are elements on right side of pivot
        if (p + 1 < last) {
            stack[++top] = p + 1;
            stack[++top] = last;
        }
    }

    free(arg);
    return arg->start_addr;
}

void *quicksort(void *arguments) {
    args *arg = (args*) arguments;
    void *start = arg->start_addr; // pointer to the beginning
    size_t size = arg->num_sort;
    void *end = arg->end_addr; // pointer to the last element
    if (size <= 10) {
	insertionSort(start, size);
        return arg->start_addr;
    }
    //printf("quicksort size: %zu\n", size);
    //printf("start: %p\n", start);
    //printf("end: %p\n", end);

    int first = 0; 
    int last = size-1;
    // partition
    int pivot = *(int *)(start+(last*100)); // rightmost element is the pivot, extract the int key
    int i = first-1; // pointer to an element greater than the pivot
    
    //printf("before partition loop\n");
    for(int j = first; j < last; j++) {
        void *loc = start+(j*100); // location in mem is by 100 byte records
        if (*(int *)loc < pivot) { // compare to the pivot looking for an element less
	    i++;
            void *greater = start+(i*100);
	    swap(loc, greater); // swap the lower element with the greater one
        }
    }
    // swap pivot with the pointer
    swap(start+((i+1)*100), start+(last*100));
    // index of pivot element
    int p = i + 1;
  
    args *arg1 = malloc(sizeof(args));
    if (arg1 == NULL) {
        free(arg);
	fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    args *arg2 = malloc(sizeof(args));
    if (arg2 == NULL) {
        free(arg);
	free(arg1);
	fprintf(stderr, "An error has occurred\n");
        exit(0);
    } 
    if (arg->num_threads != 0) { // not reached maximum number of threads
        // recursive call on the left of pivot
        pthread_t t1;
        arg1->start_addr = start;
        arg1->num_sort = p;
        arg1->end_addr = start+((p-1)*100);
        arg1->num_threads = arg->num_threads-2;
        if (pthread_create(&t1, NULL, quicksort, arg1)) {
           free(arg1);
           free(arg2);
           free(arg);
	   fprintf(stderr, "An error has occurred\n");
           exit(0);
        }

        // recursive call on the right of pivot
        pthread_t t2;
        arg2->start_addr = start+((p+1)*100);
        arg2->num_sort = size-p-1;
        arg2->end_addr = end;
        arg2->num_threads = arg->num_threads-2;
        if (pthread_create(&t2, NULL, quicksort, arg2)) {
            free(arg1);
            free(arg2);
            free(arg);
	    fprintf(stderr, "An error has occurred\n");
            exit(0);
        }

	pthread_join(t1, NULL);
        pthread_join(t2, NULL);
    } else {
        // iterative quicksort
        arg1->start_addr = start;
        arg1->num_sort = p;
        arg1->end_addr = start+((p-1)*100);
        arg1->num_threads = arg->num_threads-2;
        quicksort_iterative(arg1);

        arg2->start_addr = start+((p+1)*100);
        arg2->num_sort = size-p-1;
        arg2->end_addr = end;
        arg2->num_threads = arg->num_threads-2;
        quicksort_iterative(arg2);
    }

    free(arg);
    return arg->start_addr;
}


// open file, read it into memory, split sort with threads 
int main(int argc, char *argv[]) {
    //printf("begin\n");
    // need 2 arguments
    if (argc != 3) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }

    // input file is first argument
    char *input = argv[1];
    // output file is second argument
    char *output = argv[2]; 

    int fdin;
    int fdout;
    // open the input file 
    if ((fdin = open (input, O_RDONLY)) < 0) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    // open/create the output file
    if ((fdout = open (output, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR)) < 0) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }

    // get size of the file in bytes, every 100 bytes is a record
    struct stat st;
    if (stat(input, &st) == -1) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    size_t f_size = st.st_size; // size
    // pointer to the beginning
    void *start;
    if ((start = mmap(NULL, f_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fdin, 0)) == MAP_FAILED) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    fsync(fdin);
    close(fdin);

    size_t num_rec = f_size/100; // each record is 100 bytes
    int num_threads = get_nprocs(); //number of available processors
    if (num_threads == 0) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    if (num_threads % 2 != 0) { // odd num of threads, add one to make it easier for merging
        num_threads++;
    }
    //size_t sort = (num_rec + (num_threads-1)) / num_threads; // num of records for each thread
    /*for (int i=0; i<num_rec; i++) {
        printf("%d\n", *(int*)(start + i*100));
    }
    printf("\n");*/
    
    // call quicksort with args
    pthread_t t;
    args *arg = malloc(sizeof(args));
    if (arg == NULL) {
        fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    arg->start_addr = start;
    arg->num_sort = num_rec;
    arg->end_addr = start+(100*(num_rec-1));
    arg->num_threads = num_threads;

    //printf("%d\n", num_threads);
    if (pthread_create(&t, NULL, quicksort, arg)) {
        free(arg);
	fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    pthread_join(t, NULL);

    if (ftruncate(fdout, f_size) == -1) {
	fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    
   /* for (int i=0; i<num_rec; i++) {
        printf("%d\n", *(int*)(start + i*100));
    }*/
    void *dst;
    if ((dst = mmap (0, f_size, PROT_READ | PROT_WRITE, MAP_SHARED, fdout, 0)) == MAP_FAILED) {
	fprintf(stderr, "An error has occurred\n");
        exit(0);
    }
    // copy input to output
    memcpy(dst, start, f_size);

    msync(dst, f_size, MS_SYNC);
    munmap(dst, f_size);
    munmap(start, f_size);

    fsync(fdout);
    close(fdout);
    return 0;
}
