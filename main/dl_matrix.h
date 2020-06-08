typedef float fptp_t;
typedef uint8_t uc_t;


typedef struct
{
    int w;      /*!< Width */
    int h;      /*!< Height */
    int c;      /*!< Channel */
    int n;      /*!< Number of filter, input and output must be 1 */
    int stride; /*!< Step between lines */
    uc_t *item; /*!< Data */
} dl_matrix3du_t;

static inline void dl_lib_free(void *d)
{
    if (NULL == d)
        return;

    free(((void **)d)[-1]);
}


/*
 * @brief Allocate a zero-initialized space. Must use 'dl_lib_free' to free the memory.
 *
 * @param cnt  Count of units.
 * @param size Size of unit.
 * @param align Align of memory. If not required, set 0.
 * @return Pointer of allocated memory. Null for failed.
 */
static inline void *dl_lib_calloc(int cnt, int size, int align)
{
    int total_size = cnt * size + align + sizeof(void *);
    void *res = malloc(total_size);
    if (NULL == res)
    {
#if CONFIG_SPIRAM_SUPPORT
        res = heap_caps_malloc(total_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    }
    if (NULL == res)
    {
        printf("Item psram alloc failed. Size: %d x %d\n", cnt, size);
#else
        printf("Item alloc failed. Size: %d x %d\n", cnt, size);
#endif
        return NULL;
    }
    bzero(res, total_size);
    void **data = (void **)res + 1;
    void **aligned;
    if (align)
        aligned = (void **)(((size_t)data + (align - 1)) & -align);
    else
        aligned = data;

    aligned[-1] = res;
    return (void *)aligned;
}


/*
 * @brief Free a matrix3d
 *
 * @param m matrix3d with 8-bits items
 */
static inline void dl_matrix3du_free(dl_matrix3du_t *m)
{
    if (NULL == m)
        return;
    if (NULL == m->item)
    {
        dl_lib_free(m);
        return;
    }
    dl_lib_free(m->item);
    dl_lib_free(m);
}

/*
 * @brief Allocate a 3D matrix with 8-bits items, the access sequence is NHWC
 *
 * @param n     Number of matrix3d, for filters it is out channels, for others it is 1
 * @param w     Width of matrix3d
 * @param h     Height of matrix3d
 * @param c     Channel of matrix3d
 * @return      3d matrix
 */
static inline dl_matrix3du_t *dl_matrix3du_alloc(int n, int w, int h, int c)
{
    dl_matrix3du_t *r = (dl_matrix3du_t *)dl_lib_calloc(1, sizeof(dl_matrix3du_t), 0);
    if (NULL == r)
    {
        printf("internal r failed.\n");
        return NULL;
    }
    uc_t *items = (uc_t *)dl_lib_calloc(n * w * h * c, sizeof(uc_t), 0);
    if (NULL == items)
    {
        printf("matrix3du item alloc failed.\n");
        dl_lib_free(r);
        return NULL;
    }

    r->w = w;
    r->h = h;
    r->c = c;
    r->n = n;
    r->stride = w * c;
    r->item = items;

    return r;
}

