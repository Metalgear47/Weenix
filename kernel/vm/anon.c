#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/debug.h"

#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/tlb.h"

int anon_count = 0; /* for debugging/verification purposes */

static slab_allocator_t *anon_allocator;

static void anon_ref(mmobj_t *o);
static void anon_put(mmobj_t *o);
static int  anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf);
static int  anon_fillpage(mmobj_t *o, pframe_t *pf);
static int  anon_dirtypage(mmobj_t *o, pframe_t *pf);
static int  anon_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t anon_mmobj_ops = {
        .ref = anon_ref,
        .put = anon_put,
        .lookuppage = anon_lookuppage,
        .fillpage  = anon_fillpage,
        .dirtypage = anon_dirtypage,
        .cleanpage = anon_cleanpage
};

/*
 * This function is called at boot time to initialize the
 * anonymous page sub system. Currently it only initializes the
 * anon_allocator object.
 */
void
anon_init()
{
    anon_allocator = slab_allocator_create("anon", sizeof(mmobj_t));
    KASSERT(anon_allocator);
        /*NOT_YET_IMPLEMENTED("VM: anon_init");*/
}

/*
 * You'll want to use the anon_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
anon_create()
{
    mmobj_t *mmo = slab_obj_alloc(anon_allocator);
    if (mmo) {
        mmobj_init(mmo, &anon_mmobj_ops);
    }
    return mmo;
        /*NOT_YET_IMPLEMENTED("VM: anon_create");*/
        /*return NULL;*/
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void
anon_ref(mmobj_t *o)
{
    KASSERT(o);
    KASSERT(o->mmo_refcount >= 0);
    o->mmo_refcount++;
    dbg(DBG_ANON, "anon_ref: 0x%p, up to %d, nrespages=%d\n",
        o,  o->mmo_refcount, o->mmo_nrespages);
        /*NOT_YET_IMPLEMENTED("VM: anon_ref");*/
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is an anonymous object, it will
 * never be used again. You should unpin and uncache all of the
 * object's pages and then free the object itself.
 */
static void
anon_put(mmobj_t *o)
{
    KASSERT(o);

    KASSERT(0 <= o->mmo_nrespages);
    KASSERT(o->mmo_nrespages < o->mmo_refcount);

    dbg(DBG_ANON, "anon_put: 0x%p, down to %d, nrespages = %d\n",
        o, o->mmo_refcount - 1, o->mmo_nrespages);


    if (o->mmo_refcount == (o->mmo_nrespages - 1)) {
        pframe_t *pframe_cur;
        list_iterate_begin(&o->mmo_respages, pframe_cur, pframe_t, pf_olink) {
            pframe_unpin(pframe_cur);
            /*what about uncache it?*/
            pframe_free(pframe_cur);
        } list_iterate_end();

        /*KASSERT(list_emtpy(&o->mmo_respages));*/
        KASSERT(0 == o->mmo_nrespages);
        KASSERT(1 == o->mmo_refcount);
    }

    if (0 < --o->mmo_refcount) {
        return;
    }

    KASSERT(0 == o->mmo_nrespages);
    KASSERT(0 == o->mmo_refcount);

    slab_obj_free(anon_allocator, o);
        /*NOT_YET_IMPLEMENTED("VM: anon_put");*/
}

/* Get the corresponding page from the mmobj. No special handling is
 * required. */
static int
anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
    *pf = pframe_get_resident(o, pagenum);
    if (*pf) {
        return 0;
    } else {
        panic("edgy case, not sure if I need to handle it.");
        return -1;
    }
        /*NOT_YET_IMPLEMENTED("VM: anon_lookuppage");*/
        /*return -1;*/
}

/* The following three functions should not be difficult. */

static int
anon_fillpage(mmobj_t *o, pframe_t *pf)
{
        NOT_YET_IMPLEMENTED("VM: anon_fillpage");
        return 0;
}

static int
anon_dirtypage(mmobj_t *o, pframe_t *pf)
{
        NOT_YET_IMPLEMENTED("VM: anon_dirtypage");
        return -1;
}

static int
anon_cleanpage(mmobj_t *o, pframe_t *pf)
{
        NOT_YET_IMPLEMENTED("VM: anon_cleanpage");
        return -1;
}
