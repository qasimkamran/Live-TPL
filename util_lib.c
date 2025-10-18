#include "util_lib.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>


void PrintProgressBar( double Fraction )
{
    int Completed = (int)( Fraction * PROGRESS_BAR_WIDTH );

    int Index;

    printf("\r[");

    for( Index = 0; Index < Completed; ++Index )
    {
        putchar('=');
    }

    if( Completed < PROGRESS_BAR_WIDTH )
    {
        putchar('>');
        for( Index = Completed + 1; Index < PROGRESS_BAR_WIDTH; ++Index )
        {
            putchar(' ');
        }
    }
    else
    {
        putchar('=');
    }

    printf( "] %3.0f%%", Fraction * 100.0 );
    fflush( stdout );
}


void PrintError( const char* Message )
{
    perror( Message );
}


void StandardError( const char *Format, ... )
{
    va_list Args;
    va_start( Args, Format );

    vfprintf( stderr, Format, Args );

    va_end( Args );
}


char* AllocateBuffer( size_t BufferSize, AllocPolicy Policy )
{
    char* Buffer = NULL;

    if( Policy == ALLOC_CALLOC )
        Buffer = (char*)calloc( 1, BufferSize );
    else
    {
        Buffer = (char*)malloc( BufferSize );
        if( Buffer && Policy == ALLOC_MEMSET )
        {
            memset( Buffer, 0, BufferSize );
        }
    }

    if( Buffer == NULL )
    {
        PrintError( "Failed to allocate memory to string buffer" );
        return NULL;
    }

    return Buffer;
}


void DynamicBufferInit( DynamicBuffer* Buf )
{
    Buf->Data = NULL;
    Buf->Length = 0;
    Buf->Capacity = 0;
}


void DynamicBufferFree( DynamicBuffer* Buf )
{
    free( Buf->Data );
    Buf->Data = NULL;
    Buf->Length = Buf->Capacity = 0;
}


int DynamicBufferReserve( DynamicBuffer* Buf, size_t Capacity )
{
    char* Ptr = realloc( Buf->Data, Capacity );
    
    if( !Ptr ) return 0;
    
    Buf->Data = Ptr;
    Buf->Capacity = Capacity;
    Buf->Length = 0;
    Buf->Data[0] = '\0';

    return 1;
}


void AppendToDynamicBuffer( DynamicBuffer* Buf, const char *Text )
{
    if( Text == NULL ) return;

    size_t TextLength = strlen( Text );
    size_t NeededSize = Buf->Length + TextLength + 1;

    if( NeededSize >  Buf->Capacity )
    {
        size_t NewCapacity = Buf->Capacity ? Buf->Capacity : 1;
        while( NewCapacity < NeededSize )
        {
            NewCapacity <<= 1;
        }

        char* Ptr = realloc( Buf->Data, NewCapacity );

        if( !Ptr )
        {
            StandardError( "Ran out of memory\n" );
            return;
        }

        Buf->Data = Ptr;
        Buf->Capacity = NewCapacity;
    }

    memcpy( Buf->Data + Buf->Length, Text, TextLength + 1 );
    Buf->Length += TextLength;
}


int Matches( const char *Input, int Pos, const char *Pattern )
{
    if( Input == NULL || Pos < 0 || Pattern == NULL )
        return -1;

    size_t InputLength = strlen( Input );
    if( Pos >= InputLength ) 
        return -1;

    size_t PatternLength = strlen( Pattern );

    if( Pos + PatternLength > InputLength )
        return 0;

    for( size_t Index = 0; Index < PatternLength; Index++ )
    {
        if( Input[Pos + Index] != Pattern[Index] )
            return 0;
    }

    return 1;
}


char* ExtractTagName( const char *Input, int *Pos, const char *TagStart, const char *TagEnd )
{
    if( Input == NULL || Pos == NULL || TagStart == NULL || TagEnd == NULL )
        return NULL;

    const char* StartPos = strstr( Input + *Pos, TagStart );
    if( StartPos == NULL )
        return NULL;

    StartPos += strlen( TagStart );

    const char* EndPos = strstr( StartPos, TagEnd );
    if( EndPos == NULL )
        return NULL;

    size_t TagLength = EndPos - StartPos;

    char* TagName = AllocateBuffer( TagLength + 1, ALLOC_CALLOC );
    if( TagName == NULL )
        return NULL;

    strncpy( TagName, StartPos, TagLength );
    TagName[TagLength] = '\0';

    *Pos = EndPos - Input + strlen( TagEnd );

    return TagName;
}


char* ExtractPlainText( const char* Input, int* Pos, const char* TagStart )
{
    if( Input == NULL || Pos == NULL || TagStart == NULL )
        return NULL;

    const char* NextTagStart = strstr( Input + *Pos, TagStart );

    const char* EndPos = NextTagStart ? NextTagStart : Input + strlen( Input );

    size_t PlainTextLength = EndPos - ( Input + *Pos );

    char* PlainText = AllocateBuffer( PlainTextLength + 1, ALLOC_CALLOC );
    if( PlainText == NULL )
        return NULL;

    strncpy( PlainText, Input + *Pos, PlainTextLength );
    PlainText[PlainTextLength] = '\0';

    *Pos = EndPos - Input;

    return PlainText;
}

