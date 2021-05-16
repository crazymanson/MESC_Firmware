
#include "MESCprofile.h"

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ID_ENTRY(id) { id, #id }

static char const * getProfileStatusName( ProfileStatus const ps )
{
    static struct
    {
        ProfileStatus val;
        char const *  str;
    } const status_id[] =
    {
        ID_ENTRY( PROFILE_STATUS_UNKNOWN                ),

        ID_ENTRY( PROFILE_STATUS_SUCCESS                ),

        ID_ENTRY( PROFILE_STATUS_INIT_SUCCESS_DEFAULT   ),
        ID_ENTRY( PROFILE_STATUS_INIT_SUCCESS_LOADED    ),
        ID_ENTRY( PROFILE_STATUS_INIT_FALLBACK_DEFAULT  ),

        ID_ENTRY( PROFILE_STATUS_ERROR_STORAGE_READ     ),
        ID_ENTRY( PROFILE_STATUS_ERROR_STORAGE_WRITE    ),

        ID_ENTRY( PROFILE_STATUS_ERROR_ENTRY_ALLOC      ),
        ID_ENTRY( PROFILE_STATUS_ERROR_ENTRY_READONLY   ),

        ID_ENTRY( PROFILE_STATUS_ERROR_HEADER_SIGNATURE ),
        ID_ENTRY( PROFILE_STATUS_ERROR_HEADER_SIZE      ),
        ID_ENTRY( PROFILE_STATUS_ERROR_HEADER_VERSION   ),
        ID_ENTRY( PROFILE_STATUS_ERROR_HEADER_CHECKSUM  ),
        ID_ENTRY( PROFILE_STATUS_ERROR_HEADER_ZEROS     ),

        ID_ENTRY( PROFILE_STATUS_ERROR_IMAGE_LENGTH     ),
        ID_ENTRY( PROFILE_STATUS_ERROR_IMAGE_CHECKSUM   ),

        ID_ENTRY( PROFILE_STATUS_ERROR_ENTRY_SIGNATURE  ),
        ID_ENTRY( PROFILE_STATUS_ERROR_ENTRY_SIZE       ),
        ID_ENTRY( PROFILE_STATUS_ERROR_ENTRY_ZEROS      ),

        ID_ENTRY( PROFILE_STATUS_ERROR_NAME_LENGTH      ),
        ID_ENTRY( PROFILE_STATUS_ERROR_NAME             ),

        ID_ENTRY( PROFILE_STATUS_ERROR_DATA_SIGNATURE   ),
        ID_ENTRY( PROFILE_STATUS_ERROR_DATA_OFFSET      ),
        ID_ENTRY( PROFILE_STATUS_ERROR_DATA_LENGTH      ),

        ID_ENTRY( PROFILE_STATUS_COMMIT_SUCCESS         ),
        ID_ENTRY( PROFILE_STATUS_COMMIT_SUCCESS_NOOP    ),

        ID_ENTRY( PROFILE_STATUS_SUCCESS_ENTRY_ALLOC    ),
        ID_ENTRY( PROFILE_STATUS_SUCCESS_ENTRY_FREE     ),
        ID_ENTRY( PROFILE_STATUS_SUCCESS_ENTRY_NOOP     ),
        ID_ENTRY( PROFILE_STATUS_SUCCESS_ENTRY_UPDATE   ),
    };

    assert( status_id[ps].val == ps );

    return status_id[ps].str;
}

struct DemoEntry
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

typedef struct DemoEntry DemoEntry;

static DemoEntry demo_entry;

static void * read_buffer = NULL;
static void const * write_buffer = NULL;

static uint8_t use_mem_not_fs = 1;
static uint8_t read_zero_on_error = 0;

static uint8_t * mem = NULL;

static uint32_t corrupt_offset = 0;
static uint32_t corrupt_length = 0;

static void apply_corruption()
{
    if (corrupt_length > 0)
    {
        uint32_t buffer = 0;

        memcpy( &buffer, &((uint8_t *)read_buffer)[corrupt_offset], corrupt_length );

        buffer ^= UINT32_MAX;

        memcpy( &((uint8_t *)read_buffer)[corrupt_offset], &buffer, corrupt_length );
    }
}

#define revoke_corruption apply_corruption

static ProfileStatus read( void * data, uint32_t const length )
{
    read_buffer = data;

    FILE * f = NULL;

    if (use_mem_not_fs == 0)
    {
        f = fopen( "FLASH", "rb" );
    }

    if (f != NULL)
    {
        size_t const len = fread( data, 1, length, f );

        fclose( f );

        if (len != length)
        {
            return PROFILE_STATUS_ERROR_STORAGE_READ;
        }

        apply_corruption();

        return PROFILE_STATUS_SUCCESS;
    }

    if (use_mem_not_fs != 0)
    {
        memcpy( data, mem, length );

        apply_corruption();

        return PROFILE_STATUS_SUCCESS;
    }
    if (read_zero_on_error == 0)
    {
        return PROFILE_STATUS_ERROR_STORAGE_READ;
    }
    else
    {
        memset( data, 0, length );

        apply_corruption();

        return PROFILE_STATUS_SUCCESS;
    }
}

static ProfileStatus write( void const * data, uint32_t const length )
{
    write_buffer = data;

    FILE * f = NULL;

    if (use_mem_not_fs == 0)
    {
        f = fopen( "FLASH", "wb" );
    }

    if (f != NULL)
    {
        size_t const len = fwrite( data, 1, length, f );

        fclose( f );

        if (len != length)
        {
            return PROFILE_STATUS_ERROR_STORAGE_WRITE;
        }

        return PROFILE_STATUS_COMMIT_SUCCESS;
    }

    if (use_mem_not_fs != 0)
    {
        memcpy( mem, data, length );

        return PROFILE_STATUS_COMMIT_SUCCESS;
    }

    return PROFILE_STATUS_ERROR_STORAGE_WRITE;
}

static void reset( void )
{
    if (use_mem_not_fs == 0)
    {
        remove( "FLASH" );
    }
}

static void corrupt( char const * name, uint32_t const offset, uint32_t const length )
{
    if (length == 0)
    {
        fprintf( stdout, "INFO: Corruption disabled\n" );
    }
    else
    {
        fprintf( stdout, "INFO: Corrupting %s offset %" PRIu32 " size %" PRIu32 "\n", name, offset, length );
    }

    corrupt_offset = offset;
    corrupt_length = length;
}

#define CORRUPTION_ENTRY(T,m) { offsetof(T,m), sizeof(((T *)NULL)->m), #T "." #m }

static struct
{
    uint32_t offset;
    uint32_t length;
    char const * name;
}   const corruptions[] =
{
    // ProfileHeader
    CORRUPTION_ENTRY( ProfileHeader, signature ),

    CORRUPTION_ENTRY( ProfileHeader, _zero_signature ),
    CORRUPTION_ENTRY( ProfileHeader, size ),
    CORRUPTION_ENTRY( ProfileHeader, version_major ),
    CORRUPTION_ENTRY( ProfileHeader, version_minor ),

    CORRUPTION_ENTRY( ProfileHeader, checksum ),

    // Skip ProfileHeader entry_map

    CORRUPTION_ENTRY( ProfileHeader, _reserved ),

    CORRUPTION_ENTRY( ProfileHeader, image_length ),
    CORRUPTION_ENTRY( ProfileHeader, image_checksum ),

    // End
    { 0, 0, NULL },
};

void bist_profile( void )
{
    ProfileStatus ret;
    ProfileStatus s;
    ProfileStatus h;
    ProfileStatus e;
    ProfileStatus o;

    fprintf( stdout, "Starting Profile BIST\n" );

    mem = calloc( 4096/*TODO*/, 1 );

    demo_entry.x = 1;
    demo_entry.y = 3;
    demo_entry.z = 2;

    read_zero_on_error = 1;
    use_mem_not_fs = 1;

    profile_configure_storage( read, write );

    fprintf( stdout, "INFO: Performing initial load & store\n" );

    ret = profile_init();
    profile_get_last( &s, &h, &e, & o );
    fprintf( stdout, "INFO: Load S:%d %s H:%d %s E:%d %s O:%d %s\n",
        s, getProfileStatusName(s),
        h, getProfileStatusName(h),
        e, getProfileStatusName(e),
        o, getProfileStatusName(o) );

    ret = profile_commit();
    profile_get_last( &s, &h, &e, & o );
    fprintf( stdout, "INFO: Store (no update) S:%d %s H:%d %s E:%d %s O:%d %s\n",
        s, getProfileStatusName(s),
        h, getProfileStatusName(h),
        e, getProfileStatusName(e),
        o, getProfileStatusName(o)  );

    profile_put_entry( "demo_entry", 0x12345678, &demo_entry, sizeof(demo_entry) );

    ret = profile_commit();
    profile_get_last( &s, &h, &e, & o );
    fprintf( stdout, "INFO: Store (update) S:%d %s H:%d %s E:%d %s O:%d %s\n",
        s, getProfileStatusName(s),
        h, getProfileStatusName(h),
        e, getProfileStatusName(e),
        o, getProfileStatusName(o) );

    fprintf( stdout, "INFO: Performing corruption sweep\n" );

    for ( uint32_t i = 0; i < (sizeof(corruptions) / sizeof(*corruptions)); ++i )
    {
        corrupt( corruptions[i].name, corruptions[i].offset, corruptions[i].length );

        ret = profile_init();

        profile_get_last( &s, &h, &e, & o );

        switch (ret)
        {
            case PROFILE_STATUS_INIT_SUCCESS_DEFAULT:
                fprintf( stdout, "INFO: PROFILE_STATUS_INIT_SUCCESS_DEFAULT S:%d %s H:%d %s E:%d %s O:%d %s\n",
                    s, getProfileStatusName(s),
                    h, getProfileStatusName(h),
                    e, getProfileStatusName(e),
                    o, getProfileStatusName(o) );
                break;
            case PROFILE_STATUS_INIT_SUCCESS_LOADED:
                fprintf( stdout, "INFO: PROFILE_STATUS_INIT_SUCCESS_LOADED S:%d %s H:%d %s E:%d %s O:%d %s\n",
                    s, getProfileStatusName(s),
                    h, getProfileStatusName(h),
                    e, getProfileStatusName(e),
                    o, getProfileStatusName(o) );
                break;
            case PROFILE_STATUS_INIT_FALLBACK_DEFAULT:
                fprintf( stdout, "INFO: PROFILE_STATUS_INIT_FALLBACK_DEFAULT S:%d %s H:%d %s E:%d %s O:%d %s\n",
                    s, getProfileStatusName(s),
                    h, getProfileStatusName(h),
                    e, getProfileStatusName(e),
                    o, getProfileStatusName(o) );
                break;
            default:
                fprintf( stdout, "ERROR: S:%d %s H:%d %s E:%d %s O:%d %s\n",
                    s, getProfileStatusName(s),
                    h, getProfileStatusName(h),
                    e, getProfileStatusName(e),
                    o, getProfileStatusName(o) );
                break;
        }

        revoke_corruption();
    }

    // TODO

    reset();

    free( mem );

    fprintf( stdout, "Finished Profile BIST\n" );
}
