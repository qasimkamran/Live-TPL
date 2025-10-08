#define _GNU_SOURCE

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "errno.h"

#define SHM_NAME "/nvim_buf_shm"
#define SEM_NAME "/nvim_buf_sem"

#define CAPACITY ( 4 * 1024 * 1024 )  // 4 MiB

int main( void )
{
    int FileDescriptor = shm_open( SHM_NAME, O_RDWR | O_CREAT, 0600 );

    if( FileDescriptor < 0 )
    {
        perror( "shm_open" );
        return -1;
    }

    if( ftruncate( FileDescriptor, CAPACITY ) < 0 )
    {
        perror( "ftruncate" );
        return -1;
    }

    void* Base = mmap( NULL, CAPACITY, PROT_READ | PROT_WRITE, MAP_SHARED, FileDescriptor, 0 );
    
    if( Base == MAP_FAILED )
    {
        perror( "mmap" );
        return -1;
    }

    close( FileDescriptor );

    sem_t* Semaphore = sem_open( SEM_NAME, O_CREAT, 0600, 0 );

    if( Semaphore == SEM_FAILED )
    {
        perror( "sem_open" );
        return -1;
    }

    for( ; ; )
    {
        if( sem_wait( Semaphore ) < 0 )
        {
            if( errno == EINTR )
                continue;

            perror( "sem_wait" );
            return -1;
        }

        uint8_t* Ptr = (uint8_t*)Base;
        uint32_t BigEndian_Len = ( Ptr[ 0 ] << 24 ) | ( Ptr[ 1 ] << 16 ) | ( Ptr[ 2 ] << 8 ) | ( Ptr[ 3 ] );
        int32_t Len = BigEndian_Len;

        if( Len > CAPACITY - 4 )
        {
            fprintf( stderr, "Len too big: %u\n", Len);
            continue;
        }

        fwrite( Ptr + 4, 1, Len, stdout );;
        fputc( '\n', stdout );
        fflush( stdout );
    }

    sem_close( Semaphore );
    sem_unlink( SEM_NAME );
    munmap( Base, CAPACITY );
    shm_unlink( SHM_NAME );

    return 0;
}

