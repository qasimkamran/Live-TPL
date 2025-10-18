#define _GNU_SOURCE

#include <ncurses.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>

#define TOPBAR_MAX_LINES 6
#define TITLE "Live Preview  •  [q]: quit  [↑/↓]: navigate  [s]: save"


struct Data
{
    char* Frame;
    size_t FrameLen;
    int Scroll;
    int TermHeight, TermWidth;
    int Running;
    int MaxScroll;
    int TotalLines;
    int ExitStatus;
    pid_t ChildPID;
    bool Dirty;
    bool ExitStatusValid;
};

static pthread_mutex_t DataLock = PTHREAD_MUTEX_INITIALIZER;


static int SetFlags( int FileDescriptor, int OrFlags, int SetCloexec )
{
    int Flag = fcntl( FileDescriptor, F_GETFL );
    
    if( Flag < 0 )
        return -1;

    if( OrFlags && fcntl( FileDescriptor, F_SETFL, Flag | OrFlags ) < 0 )
        return -1;

    if( SetCloexec )
    {
        int Clo = fcntl( FileDescriptor, F_GETFD );

        if( Clo < 0 )
            return -1;

        if( fcntl( FileDescriptor, F_SETFD, Clo |  FD_CLOEXEC ) < 0 )
            return -1;
    }

    return 0;
}


static void UpdateFrame( struct Data* D, const char* Buffer, size_t Len )
{
    if( !D )
        return;

    char* NewFrame = ( char* )malloc( Len + 1 );

    if( !NewFrame )
        return;

    if( Len )
        memcpy( NewFrame, Buffer, Len );

    NewFrame[Len] = '\0';

    pthread_mutex_lock( &DataLock );

    int PreviousScroll = D->Scroll;

    free( D->Frame );
    D->Frame = NewFrame;
    D->FrameLen = Len;
    D->Scroll = PreviousScroll;
    D->MaxScroll = 0;
    D->TotalLines = 0;
    D->Dirty = true;

    pthread_mutex_unlock( &DataLock );
}


int RunShmServer( struct Data* D, const char* Path, char* const argv[], int Timeout )
{
    if( !D || !Path )
    {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock( &DataLock );
    D->Running = 0;
    D->ChildPID = -1;
    D->ExitStatusValid = false;
    pthread_mutex_unlock( &DataLock );

    int PipeFileDescriptor[2];

    if( pipe2( PipeFileDescriptor, O_CLOEXEC ) < 0 )
        return -1;

    pid_t PID = fork(  );

    if( PID < 0  )
    {
        int e = errno;
        close( PipeFileDescriptor[0] );
        close( PipeFileDescriptor[1] );
        errno = e;
        return -1;
    }

    if( PID == 0  )
    {
        close( PipeFileDescriptor[0] );

        if( dup2( PipeFileDescriptor[1], STDERR_FILENO ) < 0  )
            _exit( 127 );

        if( PipeFileDescriptor[1] != STDOUT_FILENO )
            close( PipeFileDescriptor[1] );

        execvp( Path, argv );

        _exit( 127 );
    }

    close( PipeFileDescriptor[1] );

    pthread_mutex_lock( &DataLock );
    D->Running = 1;
    D->ChildPID = PID;
    pthread_mutex_unlock( &DataLock );

    if( SetFlags( PipeFileDescriptor[0], O_NONBLOCK, 0 ) < 0 )
    {
        int e = errno;    
        close( PipeFileDescriptor[0] );
        pthread_mutex_lock( &DataLock );
        D->Running = 0;
        D->ChildPID = -1;
        pthread_mutex_unlock( &DataLock );
        errno = e;
        return -1;
    }

    const size_t BUFFER_SIZE = 64 * 1024;
    char *Buffer = ( char* )malloc( BUFFER_SIZE );
    if( !Buffer )
    {
        int e = errno;
        close( PipeFileDescriptor[0] );
        errno = e;
        return -1;
    }

    D->Running = 1;

    int Status = 0;
    bool ChildExited = false;

    for( ;; )
    {
        if( !ChildExited )
        {
            pid_t R = waitpid( PID, &Status, WNOHANG );
            if( R == PID )
                ChildExited = true;
        }

        struct pollfd Poll = { .fd = PipeFileDescriptor[0],
                               .events = POLLIN | POLLHUP | POLLERR | POLLNVAL };
        int PR = poll( &Poll, 1, Timeout );

        if( PR < 0 )
        {
            if( errno == EINTR )
                continue;
            break;
        }
        if( PR == 0 )
        {
            if( ChildExited )
            {
                ssize_t N = read( Poll.fd, Buffer, BUFFER_SIZE );
                if( N > 0 )
                {
                    UpdateFrame( D, Buffer, ( size_t )N );
                }
                else if( N == 0 || ( N < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) )
                {
                    break;
                }
            }
            continue;
        }

        if( Poll.revents & POLLIN )
        {
            for( ;; )
            {
                ssize_t N = read( Poll.fd, Buffer, BUFFER_SIZE );
                if( N > 0 )
                {
                    UpdateFrame( D, Buffer, ( size_t )N );
                    continue;
                }
                else if( N == 0 )
                {
                    if( ChildExited )
                        goto out;
                    break;
                }
                else
                {
                    if( errno == EAGAIN || errno == EWOULDBLOCK )
                        break;
                    goto out;
                }
            }
        }
        if( Poll.revents & ( POLLHUP | POLLERR | POLLNVAL ) )
        {
            ssize_t N = read( Poll.fd, Buffer, BUFFER_SIZE );
            if( N > 0 )
            {
                UpdateFrame( D, Buffer, ( size_t )N );
            }
            break;
        }
    }

out:
    free( Buffer );
    close( PipeFileDescriptor[0] );
    pthread_mutex_lock( &DataLock );
    D->Running = 0;
    D->ChildPID = -1;
    pthread_mutex_unlock( &DataLock );

    if( !ChildExited )
        (void)waitpid( PID, &Status, 0 );

    if( WIFEXITED( Status ) )
        return WEXITSTATUS( Status );
    if( WIFSIGNALED( Status ) )
        return 128 + WTERMSIG( Status );
    return -1;
}


static void MarkDirty( struct Data* D )
{
    if( !D )
        return;

    pthread_mutex_lock( &DataLock );
    D->Dirty = true;
    pthread_mutex_unlock( &DataLock );
}


static void UpdateStatus( char* Buffer, size_t Size, bool* Flag, bool Error, const char* Format, ... )
{
    if( !Buffer || !Size || !Flag || !Format )
        return;

    va_list args;
    va_start( args, Format );
    vsnprintf( Buffer, Size, Format, args );
    va_end( args );

    Buffer[Size - 1] = '\0';
    *Flag = Error;
}


static void UpdateTerminalSize( struct Data* D )
{
    if( !D )
        return;

    int Height = 0;
    int Width = 0;
    getmaxyx( stdscr, Height, Width );

    if( Height < 0 )
        Height = 0;
    if( Width < 0 )
        Width = 0;

    pthread_mutex_lock( &DataLock );
    D->TermHeight = Height;
    D->TermWidth = Width;
    D->Dirty = true;
    pthread_mutex_unlock( &DataLock );
}


static int WrapCount( size_t Len, int Width )
{
    if( Width <= 0 )
        return 1;

    size_t Segment = Len / ( size_t )Width;

    if( Len % ( size_t )Width != 0 || Len == 0 )
        ++Segment;

    if( Segment == 0 )
        Segment = 1;

    if( Segment > ( size_t )INT_MAX )
        return INT_MAX;

    return ( int )Segment;
}


static void Render( struct Data* D, const char* Status, bool StatusIsError, const char* LastSaved )
{
    if( !D )
        return;

    pthread_mutex_lock( &DataLock );

    if( !D->Dirty )
    {
        pthread_mutex_unlock( &DataLock );
        return;
    }

    int TermHeight = D->TermHeight;
    int TermWidth = D->TermWidth;
    char* Frame = D->Frame;
    size_t FrameLen = D->FrameLen;

    werase( stdscr );

    if( TermWidth > 0 )
    {
        mvhline( 0, 0, ' ', TermWidth );
        mvaddnstr( 0, 0, TITLE, TermWidth );
    }

    int ContentX = 0;
    int ContentWidth = TermWidth > 0 ? TermWidth : 1;

    char InfoLines[TOPBAR_MAX_LINES][256];
    int InfoCount = 0;
    int LinesLine = -1;
    int ScrollLine = -1;
    int SavedLine = -1;
    const char* SourceStatus = D->Running ? "running" : ( D->ExitStatusValid ? "stopped" : "idle" );

    if( InfoCount < TOPBAR_MAX_LINES )
        snprintf( InfoLines[InfoCount++], sizeof InfoLines[0], "Source: %s", SourceStatus );

    if( D->ExitStatusValid && InfoCount < TOPBAR_MAX_LINES )
        snprintf( InfoLines[InfoCount++], sizeof InfoLines[0], "Exit: %d", D->ExitStatus );

    if( InfoCount < TOPBAR_MAX_LINES )
        snprintf( InfoLines[InfoCount++], sizeof InfoLines[0], "Bytes: %zu", D->FrameLen );

    if( InfoCount < TOPBAR_MAX_LINES )
    {
        LinesLine = InfoCount;
        InfoLines[LinesLine][0] = '\0';
        ++InfoCount;
    }

    if( InfoCount < TOPBAR_MAX_LINES )
    {
        ScrollLine = InfoCount;
        InfoLines[ScrollLine][0] = '\0';
        ++InfoCount;
    }

    if( LastSaved && *LastSaved && InfoCount < TOPBAR_MAX_LINES )
    {
        SavedLine = InfoCount;
        InfoLines[SavedLine][0] = '\0';
        ++InfoCount;
    }

    int MaxAvailableTopbar = TermHeight - 2;

    if( MaxAvailableTopbar < 0 )
        MaxAvailableTopbar = 0;

    int TopbarHeight = InfoCount;

    if( TopbarHeight > MaxAvailableTopbar )
        TopbarHeight = MaxAvailableTopbar;

    int ContentStartRow = 1 + TopbarHeight;
    int ContentHeight = TermHeight - 1 - ContentStartRow;

    if( ContentHeight < 0 )
        ContentHeight = 0;

    int TotalLines = 1;

    if( Frame && FrameLen > 0 )
    {
        const char* Cursor = Frame;
        const char* End = Frame + FrameLen;
        TotalLines = 0;

        while( Cursor < End )
        {
            const char* NL = memchr( Cursor, '\n', ( size_t )( End - Cursor ) );
            size_t Len = NL ? ( size_t )( NL - Cursor ) : ( size_t )( End - Cursor );
            TotalLines += WrapCount( Len, ContentWidth );

            if( NL )
            {
                Cursor = NL + 1;
            }
            else
            {
                break;
            }
        }

        if( TotalLines <= 0 )
            TotalLines = 1;
    }

    int MaxScroll = 0;

    if( ContentHeight > 0 )
    {
        MaxScroll = TotalLines - ContentHeight;

        if( MaxScroll < 0 )
            MaxScroll = 0;

        if( D->Scroll > MaxScroll )
            D->Scroll = MaxScroll;

        if( D->Scroll < 0 )
            D->Scroll = 0;
    }
    else
    {
        D->Scroll = 0;
        MaxScroll = 0;
    }

    D->MaxScroll = MaxScroll;
    D->TotalLines = TotalLines;

    if( LinesLine >= 0 )
        snprintf( InfoLines[LinesLine], sizeof InfoLines[LinesLine], "Lines: %d", TotalLines );

    if( ScrollLine >= 0 )
        snprintf( InfoLines[ScrollLine], sizeof InfoLines[ScrollLine], "Scroll: %d/%d", D->Scroll, MaxScroll );

    if( SavedLine >= 0 )
        snprintf( InfoLines[SavedLine], sizeof InfoLines[SavedLine], "Saved: %s", LastSaved );

    int TopbarWidth = TermWidth > 0 ? TermWidth : 1;

    for( int r = 0; r < TopbarHeight; ++r )
    {
        int y = 1 + r;
        mvhline( y, 0, ' ', TopbarWidth );
        mvaddnstr( y, 0, InfoLines[r], TopbarWidth );
    }

    int Row = ContentStartRow;
    int Drawn = 0;

    if( ContentHeight > 0 )
    {
        if( Frame && FrameLen > 0 )
        {
            const char* Cursor = Frame;
            const char* End = Frame + FrameLen;
            int Skip = D->Scroll;

            while( Cursor < End && Row < TermHeight - 1 && Drawn < ContentHeight )
            {
                const char* NL = memchr( Cursor, '\n', ( size_t )( End - Cursor ) );
                size_t Len = NL ? ( size_t )( NL - Cursor ) : ( size_t )( End - Cursor );
                int Segments = WrapCount( Len, ContentWidth );

                for( int seg = 0; seg < Segments && Row < TermHeight - 1 && Drawn < ContentHeight; ++seg )
                {
                    if( Skip > 0 )
                    {
                        --Skip;
                        continue;
                    }

                    mvhline( Row, ContentX, ' ', ContentWidth );

                    size_t Offset = ( size_t )seg * ( size_t )ContentWidth;
                    size_t ChunkLen = Len > Offset ? Len - Offset : 0;

                    if( ChunkLen > ( size_t )ContentWidth )
                        ChunkLen = ( size_t )ContentWidth;

                    if( ChunkLen > 0 )
                        mvaddnstr( Row, ContentX, Cursor + Offset, ( int )ChunkLen );

                    ++Row;
                    ++Drawn;
                }

                if( NL )
                {
                    Cursor = NL + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            mvhline( Row, ContentX, ' ', ContentWidth );
            mvaddnstr( Row, ContentX, "(waiting for data)", ContentWidth );
            ++Row;
            ++Drawn;
        }

        while( Drawn < ContentHeight && Row < TermHeight - 1 )
        {
            mvhline( Row, ContentX, ' ', ContentWidth );
            ++Row;
            ++Drawn;
        }
    }

    if( TermHeight > 0 )
    {
        int StatusRow = TermHeight - 1;
        mvhline( StatusRow, 0, ' ', TermWidth );

        char Summary[512];
        int VisibleTop = ContentHeight > 0 ? D->Scroll + 1 : 0;
        int VisibleBottom = ContentHeight > 0 ? D->Scroll + ContentHeight : 0;

        if( VisibleBottom > TotalLines )
            VisibleBottom = TotalLines;

        snprintf( Summary, sizeof Summary,
                  "Lines %d  Visible %d-%d  Scroll %d/%d  Bytes %zu  Source %s",
                  TotalLines,
                  VisibleTop,
                  VisibleBottom >= VisibleTop ? VisibleBottom : VisibleTop,
                  D->Scroll,
                  MaxScroll,
                  D->FrameLen,
                  SourceStatus );

        mvaddnstr( StatusRow, 0, Summary, TermWidth );

        if( Status && *Status && TermWidth > 0 )
        {
            int Col = ( int )strlen( Summary );

            if( Col > TermWidth )
                Col = TermWidth;

            if( Col + 2 < TermWidth )
            {
                mvaddnstr( StatusRow, Col, "  |  ", TermWidth - Col );
                Col += 5;

                if( Col < TermWidth )
                {
                    int Remaining = TermWidth - Col;

                    if( StatusIsError )
                        attron( A_BOLD );
                    else
                        attron( A_DIM );

                    mvaddnstr( StatusRow, Col, Status, Remaining );

                    if( StatusIsError )
                        attroff( A_BOLD );
                    else
                        attroff( A_DIM );
                }
            }
        }
    }

    D->Dirty = false;
    pthread_mutex_unlock( &DataLock );
    refresh();
}


static bool SaveFrame( struct Data* D, char* SavedPath, size_t SavedPathLen,
                       char* Status, size_t StatusLen, bool* StatusIsError )
{
    if( !D || !Status || !StatusLen || !StatusIsError )
        return false;

    if( SavedPath && SavedPathLen )
        SavedPath[0] = '\0';

    size_t Len = 0;
    bool HasFrame = false;
    char* Copy = NULL;

    pthread_mutex_lock( &DataLock );

    if( D->Frame )
    {
        HasFrame = true;
        Len = D->FrameLen;

        if( Len )
        {
            Copy = ( char* )malloc( Len );

            if( Copy )
                memcpy( Copy, D->Frame, Len );
        }
    }

    pthread_mutex_unlock( &DataLock );

    if( !HasFrame )
    {
        UpdateStatus( Status, StatusLen, StatusIsError, true, "no frame available to save" );
        MarkDirty( D );
        return false;
    }

    if( Len && !Copy )
    {
        UpdateStatus( Status, StatusLen, StatusIsError, true, "out of memory while saving" );
        MarkDirty( D );
        return false;
    }

    time_t Now = time( NULL );
    struct tm Local;

    if( !localtime_r( &Now, &Local ) )
    {
        free( Copy );
        UpdateStatus( Status, StatusLen, StatusIsError, true, "localtime failed" );
        MarkDirty( D );
        return false;
    }

    char Filename[64];

    if( strftime( Filename, sizeof Filename, "snapshot-%Y%m%d-%H%M%S.txt", &Local ) == 0 )
    {
        free( Copy );
        UpdateStatus( Status, StatusLen, StatusIsError, true, "strftime failed" );
        MarkDirty( D );
        return false;
    }

    FILE* File = fopen( Filename, "wb" );

    if( !File )
    {
        int e = errno;
        free( Copy );
        UpdateStatus( Status, StatusLen, StatusIsError, true, "save failed: %s", strerror( e ) );
        MarkDirty( D );
        return false;
    }

    size_t Written = 0;

    if( Len )
        Written = fwrite( Copy, 1, Len, File );

    int Error = 0;

    if( ( Len && Written != Len ) || fflush( File ) != 0 )
        Error = errno;

    if( fclose( File ) != 0 && !Error )
        Error = errno;

    free( Copy );

    if( Error )
    {
        UpdateStatus( Status, StatusLen, StatusIsError, true, "save failed: %s", strerror( Error ) );
        MarkDirty( D );
        return false;
    }

    if( SavedPath && SavedPathLen )
        snprintf( SavedPath, SavedPathLen, "%s", Filename );

    UpdateStatus( Status, StatusLen, StatusIsError, false, "saved to %s", Filename );
    MarkDirty( D );
    return true;
}


struct WorkerArgs
{
    struct Data* Data;
    const char* Path;
    char* const* Argv;
    int Timeout;
    int Result;
};


static void* ServerThread( void* Arg )
{
    struct WorkerArgs* Worker = ( struct WorkerArgs* )Arg;

    int ReturnCode = RunShmServer( Worker->Data, Worker->Path, Worker->Argv, Worker->Timeout );
    Worker->Result = ReturnCode;

    pthread_mutex_lock( &DataLock );
    Worker->Data->ExitStatus = ReturnCode;
    Worker->Data->ExitStatusValid = true;
    Worker->Data->Dirty = true;
    pthread_mutex_unlock( &DataLock );

    return NULL;
}


static void RestoreTerminal( void )
{
    if( !isendwin() )
        endwin();
}


static bool ResolveDefaultCommand( char* Buffer, size_t Size )
{
    if( !Buffer || !Size )
        return false;

    char ExePath[PATH_MAX];
    ssize_t Len = readlink( "/PRoc/self/exe", ExePath, sizeof( ExePath ) - 1 );

    if( Len >= 0 )
    {
        ExePath[Len] = '\0';
        char* Slash = strrchr( ExePath, '/' );

        if( Slash )
        {
            *Slash = '\0';
            int Written = snprintf( Buffer, Size, "%s/%s", ExePath, "shm_server" );

            if( Written > 0 && ( size_t )Written < Size && access( Buffer, X_OK ) == 0 )
                return true;
        }
    }

    int Written = snprintf( Buffer, Size, "./shm_server" );

    if( Written > 0 && ( size_t )Written < Size && access( Buffer, X_OK ) == 0 )
        return true;

    Written = snprintf( Buffer, Size, "shm_server" );

    if( Written > 0 && ( size_t )Written < Size )
        return true;

    return false;
}


int main( int argc, char* argv[] )
{
    int CommandIndex = 1;

    if( argc > 1 && strcmp( argv[1], "--" ) == 0 )
        CommandIndex = 2;

    char DefaultPath[PATH_MAX] = "";
    char* DefaultArgv[2] = { NULL, NULL };
    char* const* ChildArgv = NULL;
    const char* ChildPath = NULL;

    if( CommandIndex >= argc )
    {
        if( !ResolveDefaultCommand( DefaultPath, sizeof( DefaultPath ) ) )
        {
            fprintf( stderr, "Usage: %s [--] [command [args...]]\n", argv[0] );
            fprintf( stderr, "failed to locate bundled shm_server\n" );
            return 1;
        }

        DefaultArgv[0] = DefaultPath;
        ChildArgv = DefaultArgv;
        ChildPath = DefaultArgv[0];
    }
    else
    {
        ChildArgv = &argv[CommandIndex];
        ChildPath = ChildArgv[0];
    }

    if( !ChildPath || !*ChildPath )
    {
        fprintf( stderr, "Usage: %s [--] [command [args...]]\n", argv[0] );
        return 1;
    }

    setlocale( LC_ALL, "" );

    if( initscr() == NULL )
    {
        fprintf( stderr, "failed to initialize ncurses\n" );
        return 1;
    }

    atexit( RestoreTerminal );
    cbreak();
    noecho();
    keypad( stdscr, TRUE );
    nodelay( stdscr, TRUE );
    curs_set( 0 );
    start_color();
    use_default_colors();

    struct Data Data = { 0 };
    Data.ChildPID = -1;
    UpdateTerminalSize( &Data );

    char Status[256] = "";
    bool StatusIsError = false;
    char LastSaved[256] = "";
    bool LastSavedValid = false;

    struct WorkerArgs Worker = { .Data = &Data, .Path = ChildPath, .Argv = ChildArgv, .Timeout = 100, .Result = 0 };
    pthread_t Thread;

    int ThreadInit = pthread_create( &Thread, NULL, ServerThread, &Worker );

    if( ThreadInit != 0 )
    {
        endwin();
        fprintf( stderr, "pthread_create failed: %s\n", strerror( ThreadInit ) );
        return 1;
    }

    bool AppRunning = true;
    bool ExitReported = false;

    while( AppRunning )
    {
        int Key = getch();

        switch( Key )
        {
            case 'q':
            case 'Q':
                AppRunning = false;
                break;

            case 's':
            case 'S':
                if( SaveFrame( &Data, LastSaved, sizeof LastSaved, Status, sizeof Status, &StatusIsError ) )
                    LastSavedValid = true;
                break;

            case KEY_UP:
                pthread_mutex_lock( &DataLock );

                if( Data.Scroll > 0 )
                {
                    --Data.Scroll;
                    Data.Dirty = true;
                }

                pthread_mutex_unlock( &DataLock );
                break;

            case KEY_DOWN:
                pthread_mutex_lock( &DataLock );

                if( Data.Scroll < Data.MaxScroll )
                {
                    ++Data.Scroll;
                    Data.Dirty = true;
                }

                pthread_mutex_unlock( &DataLock );
                break;

            case KEY_RESIZE:
                UpdateTerminalSize( &Data );
                break;

            case ERR:
                break;

            default:
                break;
        }

        pthread_mutex_lock( &DataLock );
        bool ExitValid = Data.ExitStatusValid;
        int ExitStatus = Data.ExitStatus;
        pthread_mutex_unlock( &DataLock );

        if( ExitValid && !ExitReported )
        {
            bool Error = ExitStatus != 0;

            if( ExitStatus < 0 )
                Error = true;

            UpdateStatus( Status, sizeof Status, &StatusIsError, Error,
                          Error ? "source exited (code %d)" : "source exited cleanly",
                          ExitStatus );
            MarkDirty( &Data );
            ExitReported = true;
        }

        Render( &Data, Status, StatusIsError, LastSavedValid ? LastSaved : NULL );

        if( Key == ERR )
            napms( 25 );
    }

    pthread_mutex_lock( &DataLock );
    pid_t Child = Data.ChildPID;
    pthread_mutex_unlock( &DataLock );

    if( Child > 0 )
        kill( Child, SIGTERM );

    pthread_join( Thread, NULL );

    endwin();

    pthread_mutex_lock( &DataLock );
    free( Data.Frame );
    Data.Frame = NULL;
    pthread_mutex_unlock( &DataLock );

    return Worker.Result;
}
