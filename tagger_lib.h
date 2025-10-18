#define START_TAG "{{"
#define END_TAG "}}"
#define MAX_RECURSION_DEPTH 3


typedef struct
{
    char* Name;
    char* Replacement;
} Tag;


typedef struct
{
    Tag *Tags;
    int Count;
} TagDictionary;


typedef struct
{
    int Depth;
    char** ActiveTags;
    int ActiveCount;
} RecursionContext;


char* ProcessInput( const char* Input, RecursionContext *Ctx, TagDictionary *Dict );


char* ResolveTag( const char* TagName, RecursionContext *Ctx, TagDictionary *Dict );


int IsTagActive( RecursionContext *Ctx, const char* TagName );


void AddActiveTag( RecursionContext *Ctx, const char* TagName );


void RemoveActiveTag( RecursionContext *Ctx, const char* TagName );


char* LookupTag( TagDictionary *Dict, const char* TagName );

