local M = {}

if not jit or not jit.version then
    vim.notify( "sharebuf_shm: needs LuaJIT; falling back to sockets is recommended.", vim.log.levels.ERROR )
    return M
end

-- Cheating by using ffi but we take those
local ffi = require("ffi")

ffi.cdef[[

    int shm_open(const char *name, int oflag, unsigned mode);
    int ftruncate(int fd, long length);
    void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
    int munmap(void *addr, size_t length);
    int close(int fd);
    int shm_unlink(const char *name);

    typedef struct __sem_t sem_t;
    sem_t *sem_open(const char *name, int oflag, unsigned int mode, unsigned int value);
    int sem_post(sem_t *sem);
    int sem_close(sem_t *sem);

    static const int O_CREAT = 0100;
    static const int O_RDWR  = 0002;
    static const int PROT_READ  = 1;
    static const int PROT_WRITE = 2;
    static const int MAP_SHARED = 1;

]]

local SHM_NAME = "/nvim_buf_shm"
local SEM_NAME = "/nvim_buf_sem"
local CAPACITY = ( 4 * 1024 * 1024 )  -- 4 MiB

local Base = nil
local Cap = CAPACITY
local Sem = nil

local function u32be( N )
    local Bit = require( "bit" )
    return string.char(
            Bit.rshift( Bit.band( N, 0xFF000000 ), 24 ),
            Bit.rshift( Bit.band( N, 0x00FF0000 ), 16 ),
            Bit.rshift( Bit.band( N, 0x0000FF00 ), 8 ),
            Bit.band( N, 0x000000FF )
        )
end

local function ensure_mapping()
    if Base ~= nil then return true end
    local FileDescriptor = ffi.C.shm_open( SHM_NAME, bit.bor( ffi.C.O_RDWR, ffi.C.O_CREAT ), 0x180 )  -- 0600
    if FileDescriptor < 0 then
        vim.notify( "sharebuf_shm: shm_open failed", vim.log.levels.ERROR )
        return false
    end
    if ffi.C.ftruncate( FileDescriptor, Cap ) ~= 0 then
        ffi.C.close( FileDescriptor )
        vim.notify( "sharebuf_shm: ftruncate failed", vim.log.levels.ERROR )
        return false
    end
    Base = ffi.C.mmap( nil, Cap, bit.bor( ffi.C.PROT_READ, ffi.C.PROT_WRITE ), ffi.C.MAP_SHARED, FileDescriptor, 0 )
    ffi.C.close( FileDescriptor )
    if Base == ffi.cast( "void*", -1 ) then
        Base = nil
        vim.notify( "sharebuf_shm: mmap failed", vim.log.levels.ERROR )
        return false
    end
    Sem = ffi.C.sem_open( SEM_NAME, bit.bor( ffi.C.O_CREAT ), 0x180, 0 )  -- 0600, initial 0
    if Sem == ffi.cast( "sem_t *", -1 ) then
        vim.notify( "sharebuf_shm: sem_open failed", vim.log.levels.ERROR )
        return false
    end
    return true
end

function M.send_current_buffer()
    if not ensure_mapping() then return end
    local Lines = vim.api.nvim_buf_get_lines( 0, 0, -1, true )
    local Text = table.concat( Lines, "\n")
    local Needed = 4 + #Text
    if Needed > Cap then
        vim.notify( 
            ( "sharebuf_shm: buffer too large ( %d > %d ); increase CAPACITY" ):format( Needed, Cap ),
            vim.log.levels.ERROR
        )
        return
    end

    local Header = u32be( #Text )
    local Payload = ffi.cast( "uint8_t*", Base )
    ffi.copy( Payload, Header, 4 )
    ffi.copy( Payload + 4, Text, #Text )

    if ffi.C.sem_post( Sem ) ~= 0 then
        vim.notify( "sharebuf_shm: sem_post failed", vim.log.levels.ERROR )
    else
        vim.notify( ( "sharebuf_shm: sent %d bytes via SHM" ):format( #Text ) )
    end
end

function M.close()
    if Base ~= nil then ffi.C.munmap( Base, Cap); Base = nil end
    if Sem ~= nil then ffi.C.sem_close( Sem ); Sem = nil end
end

return M

