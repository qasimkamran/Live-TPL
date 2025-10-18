#include <stddef.h>


#define BUFFER_SIZE 32
#define PROGRESS_BAR_WIDTH 50


typedef struct
{
    char *Data;
    size_t Length;
    size_t Capacity;
} DynamicBuffer;


typedef enum
{
    ALLOC_NONE,
    ALLOC_MEMSET,
    ALLOC_CALLOC
} AllocPolicy;


void PrintProgressBar( double Fraction );


void DynamicBufferInit( DynamicBuffer* Buf );


void DynamicBufferFree( DynamicBuffer* Buf );


int DynamicBufferReserve( DynamicBuffer* Buf, size_t Capacity );


char* AllocateBuffer( size_t BufferSize, AllocPolicy Policy );


int Matches( const char* Input, int Pos, const char* Pattern );


char* ExtractTagName( const char* Input, int* Pos, const char* TagStart, const char* TagEnd );


char* ExtractPlainText( const char* Input, int* Pos, const char* TagStart );


int ContainsTag( const char* Text, const char* TagStart );


void AppendToDynamicBuffer( DynamicBuffer* Buf, const char* Text );


void PrintError( const char* Messsage );


void StandardError( const char *Format, ... );

