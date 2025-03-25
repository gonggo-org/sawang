#include <glib.h>

#ifdef GLIBSHIM

#define MIN_ARRAY_SIZE  16

typedef struct _GRealPtrArray  GRealPtrArray;

struct _GRealPtrArray
{
    gpointer       *pdata;
    guint           len;
    guint           alloc;
    gatomicrefcount ref_count;
    guint8          null_terminated : 1; /* always either 0 or 1, so it can be added to array lengths */
    GDestroyNotify  element_free_func;
};

static GPtrArray * ptr_array_new (guint reserved_size, GDestroyNotify element_free_func, gboolean null_terminated);
static void g_ptr_array_maybe_expand (GRealPtrArray *array, guint len);
static void ptr_array_maybe_null_terminate (GRealPtrArray *rarray);

/* Returns the smallest power of 2 greater than or equal to n,
 * or 0 if such power does not fit in a gsize
 */
static inline gsize g_nearest_pow (gsize num)
{
    gsize n = num - 1;

    g_assert (num > 0 && num <= G_MAXSIZE / 2);

    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if GLIB_SIZEOF_SIZE_T == 8
    n |= n >> 32;
#endif

    return n + 1;
}

GPtrArray* g_ptr_array_copy (GPtrArray* array, GCopyFunc func, gpointer user_data) {
    GRealPtrArray *rarray = (GRealPtrArray *) array;
    GPtrArray *new_array;

    g_return_val_if_fail (array != NULL, NULL);

    new_array = ptr_array_new (0,
        rarray->element_free_func,
        rarray->null_terminated);

    if (rarray->alloc > 0)
    {
        g_ptr_array_maybe_expand ((GRealPtrArray *) new_array, array->len + rarray->null_terminated);

        if (array->len > 0)
        {
            if (func != NULL)
            {
                guint i;

                for (i = 0; i < array->len; i++)
                    new_array->pdata[i] = func (array->pdata[i], user_data);
            }
            else
            {
                memcpy (new_array->pdata, array->pdata,
                    array->len * sizeof (*array->pdata));
            }

            new_array->len = array->len;
        }

        ptr_array_maybe_null_terminate ((GRealPtrArray *) new_array);
    }

    return new_array;
}

static GPtrArray * ptr_array_new (guint reserved_size, GDestroyNotify element_free_func, gboolean null_terminated)
{
    GRealPtrArray *array;

    array = g_slice_new (GRealPtrArray);

    array->pdata = NULL;
    array->len = 0;
    array->alloc = 0;
    array->null_terminated = null_terminated ? 1 : 0;
    array->element_free_func = element_free_func;

    g_atomic_ref_count_init (&array->ref_count);

    if (reserved_size != 0)
    {
        if (G_LIKELY (reserved_size < G_MAXUINT) &&
            null_terminated)
            reserved_size++;

        g_ptr_array_maybe_expand (array, reserved_size);
        g_assert (array->pdata != NULL);

        if (null_terminated)
        {
            /* don't use ptr_array_maybe_null_terminate(). It helps the compiler
            * to see when @null_terminated is false and thereby inline
            * ptr_array_new() and possibly remove the code entirely. */
            array->pdata[0] = NULL;
        }
    }

    return (GPtrArray *) array;
}

static void ptr_array_maybe_null_terminate (GRealPtrArray *rarray)
{
    if (G_UNLIKELY (rarray->null_terminated))
        rarray->pdata[rarray->len] = NULL;
}

static void g_ptr_array_maybe_expand (GRealPtrArray *array, guint len)
{
    guint max_len;

    /* The maximum array length is derived from following constraints:
    * - The number of bytes must fit into a gsize / 2.
    * - The number of elements must fit into guint.
    */
    max_len = MIN (G_MAXSIZE / 2 / sizeof (gpointer), G_MAXUINT);

    /* Detect potential overflow */
    if G_UNLIKELY ((max_len - array->len) < len)
        g_error ("adding %u to array would overflow", len);

    if ((array->len + len) > array->alloc)
    {
        guint old_alloc = array->alloc;
        gsize want_alloc = g_nearest_pow (sizeof (gpointer) * (array->len + len));
        want_alloc = MAX (want_alloc, MIN_ARRAY_SIZE);
        array->alloc = MIN (want_alloc / sizeof (gpointer), G_MAXUINT);
        array->pdata = g_realloc (array->pdata, want_alloc);
        if (G_UNLIKELY (g_mem_gc_friendly))
            for ( ; old_alloc < array->alloc; old_alloc++)
                array->pdata [old_alloc] = NULL;
    }
}

#endif //GLIBSHIM