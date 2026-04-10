#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>

typedef struct { 
  int created_success; 
  pid_t pid;
  int waited_success;
} Child;

Child quicksort_subproc( int64_t *arr, unsigned long start, unsigned long end, unsigned long par_threshold );
void quicksort_wait( Child *child );
int quicksort_check_success( Child *child );
int compare( const void *left, const void *right );
void swap( int64_t *arr, unsigned long i, unsigned long j );
unsigned long partition( int64_t *arr, unsigned long start, unsigned long end );
int quicksort( int64_t *arr, unsigned long start, unsigned long end, unsigned long par_threshold );

int main( int argc, char **argv ) {

  // example usage: ./parsort test_data_1.bin 65536, first argument filename, secoond parallelization threshold
  unsigned long par_threshold;
  // check commandline arguments
  if ( argc != 3 || sscanf( argv[2], "%lu", &par_threshold ) != 1 ) {
    fprintf( stderr, "Usage: parsort <file> <par threshold>\n" );
    exit( 1 );
  }

  char* filename = argv[1];

  // Use the open syscall to open the file in read-write mode and get a file descriptor:
  int fd = open(filename, O_RDWR);
  if (fd < 0) {
    fprintf( stderr, "File failed to open with syscall open");
    exit( 1 );
  }

  // determine file size and number of elements
  unsigned long file_size, num_elements;
  struct stat statbuf;
  // fstat is syscall that gets file status
  int rc = fstat( fd, &statbuf );
  if ( rc != 0 ) {
      // handle fstat error and exit
      fprintf( stderr, "Failed to get the file status with syscall fstat");
      exit( 1 );
  }
  // statbuf.st_size indicates the number of bytes in the file
  file_size = statbuf.st_size;
  if (file_size % sizeof(int64_t) != 0) {
    fprintf( stderr, "File malformed, should be all int64_t");
    exit( 1 );
  }
  num_elements = file_size / sizeof(int64_t);

  // mmap the file data, syscall that maps a file into memory, so that we can treat the file data as an array in memory
  int64_t *arr;
  arr = mmap( NULL, file_size, PROT_READ | PROT_WRITE,
            MAP_SHARED, fd, 0 );
  close( fd ); // file can be closed now
  if ( arr == MAP_FAILED ) {
    fprintf( stderr, "Failed to map the file data with syscall mmap");
    exit( 1 );
  }
  /* Passing in NULL for the requested mapping address gives mmap
   complete freedom to choose any address in memory as the base address
   for the mapping. Since we don't care where the file's data ends up
   in memory, so long as we can access it, this is what we want.
   Similarly, we want to map the entire file, so we set the offset to zero.*/

  // *arr now behaves like a standard array of int64_t.
  // Be careful though! Going off the end of the array will
  // silently extend the file, which can rapidly lead to
  // disk space depletion!

  // Sort the data!
  int success;
  success = quicksort( arr, 0, num_elements, par_threshold );
  if ( !success ) {
    fprintf( stderr, "Error: sorting failed\n" );
    exit( 1 );
  }

  // Unmap the file data
  munmap( arr, file_size );

  return 0;
}

// Compare elements.
// This function can be used as a comparator for a call to qsort.
//
// Parameters:
//   left - pointer to left element
//   right - pointer to right element
//
// Return:
//   negative if *left < *right,
//   positive if *left > *right,
//   0 if *left == *right
int compare( const void *left, const void *right ) {
  int64_t left_elt = *(const int64_t *)left, right_elt = *(const int64_t *)right;
  if ( left_elt < right_elt )
    return -1;
  else if ( left_elt > right_elt )
    return 1;
  else
    return 0;
}

// Swap array elements.
//
// Parameters:
//   arr - pointer to first element of array
//   i - index of element to swap
//   j - index of other element to swap
void swap( int64_t *arr, unsigned long i, unsigned long j ) {
  int64_t tmp = arr[i];
  arr[i] = arr[j];
  arr[j] = tmp;
}

// Partition a region of given array from start (inclusive)
// to end (exclusive).
//
// Parameters:
//   arr - pointer to first element of array
//   start - inclusive lower bound index
//   end - exclusive upper bound index
//
// Return:
//   index of the pivot element, which is globally in the correct place;
//   all elements to the left of the pivot will have values less than
//   the pivot element, and all elements to the right of the pivot will
//   have values greater than or equal to the pivot
unsigned long partition( int64_t *arr, unsigned long start, unsigned long end ) {
  assert( end > start );

  // choose the middle element as the pivot
  unsigned long len = end - start;
  assert( len >= 2 );
  unsigned long pivot_index = start + (len / 2);
  int64_t pivot_val = arr[pivot_index];

  // stash the pivot at the end of the sequence
  swap( arr, pivot_index, end - 1 );

  // partition all of the elements based on how they compare
  // to the pivot element: elements less than the pivot element
  // should be in the left partition, elements greater than or
  // equal to the pivot should go in the right partition
  unsigned long left_index = start,
                right_index = start + ( len - 2 );

  while ( left_index <= right_index ) {
    // extend the left partition?
    if ( arr[left_index] < pivot_val ) {
      ++left_index;
      continue;
    }

    // extend the right partition?
    if ( arr[right_index] >= pivot_val ) {
      --right_index;
      continue;
    }

    // left_index refers to an element that should be in the right
    // partition, and right_index refers to an element that should
    // be in the left partition, so swap them
    swap( arr, left_index, right_index );
  }

  // at this point, left_index points to the first element
  // in the right partition, so place the pivot element there
  // and return the left index, since that's where the pivot
  // element is now
  swap( arr, left_index, end - 1 );
  return left_index;
}

// Sort specified region of array.
// Note that the only reason that sorting should fail is
// if a child process can't be created or if there is any
// other system call failure.
//
// Parameters:
//   arr - pointer to first element of array
//   start - inclusive lower bound index
//   end - exclusive upper bound index
//   par_threshold - if the number of elements in the region is less
//                   than or equal to this threshold, sort sequentially,
//                   otherwise sort in parallel using child processes
//
// Return:
//   1 if the sort was successful, 0 otherwise
int quicksort( int64_t *arr, unsigned long start, unsigned long end, unsigned long par_threshold ) {
  assert( end >= start );
  unsigned long len = end - start;

  // Base case: if there are fewer than 2 elements to sort,
  // do nothing
  if ( len < 2 )
    return 1;

  // Base case: if number of elements is less than or equal to
  // the threshold, sort sequentially using qsort
  if ( len <= par_threshold ) {
    qsort( arr + start, len, sizeof(int64_t), compare );
    return 1;
  }

  // Partition
  unsigned long mid = partition( arr, start, end );

  // Recursively sort the left and right partitions
  int left_success, right_success;

  Child left, right;
  left = quicksort_subproc( arr, start, mid, par_threshold );
  right = quicksort_subproc( arr, mid + 1, end, par_threshold );

  if (left.created_success) {
    quicksort_wait( &left );
  } 
  if (right.created_success) {
    quicksort_wait( &right );
  }

  left_success = quicksort_check_success( &left );
  right_success = quicksort_check_success( &right );

  return left_success && right_success;
}

Child quicksort_subproc( int64_t *arr, unsigned long start, unsigned long end, unsigned long par_threshold ) {
  pid_t child_pid = fork();
  Child child;
  child.created_success = 0; 
  child.waited_success = 0;
  // The fork is successful
  // if this is child process: 
  if ( child_pid == 0 ) {
    // What to execute in child
    int success_sort = quicksort( arr, start, end, par_threshold );

    if ( success_sort ) {
      exit( 0 );
    }
    // the work failed
    else{
      exit( 1 );
    }

  // The fork failed
  } else if ( child_pid < 0 ) {
    child.created_success = 0;

  // if this is parent process, we only manage the child struct
  } else {
    child.created_success = 1;
    child.pid = child_pid;
  }
  return child;

}

void quicksort_wait( Child *child ) {
  int result, wstatus;

  // wait for child to finish and get its exit status, we check for success outside of this method
  result = waitpid( child->pid, &wstatus, 0 );

  if ( result < 0 ) {
    // child wait failed
    child->waited_success = 0;
  } else {
    // check status of child
    if ( !WIFEXITED( wstatus ) ) {
      // child did not exit normally (e.g., it was terminated by a signal)
      child->waited_success = 0;
    } else if ( WEXITSTATUS( wstatus ) != 0 ) {
      // child exited with a non-zero exit code
      child->waited_success = 0;
    } else {
      // child exited with exit code zero (it was successful)
      child->waited_success = 1;
    }
  }
} 

int quicksort_check_success( Child *child ) {
  if ( !child->created_success ) {
    return 0;
  }
  if ( !child->waited_success ) {
    return 0;
  }
  return 1;
}



// TODO: define additional helper functions if needed
