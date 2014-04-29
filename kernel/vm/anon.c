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
    dbg(DBG_ANON, "anon function hook\n");
    anon_allocator = slab_allocator_create("anonymous object", sizeof(mmobj_t));
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
    dbg(DBG_ANON, "anon function hook\n");
    mmobj_t *mmo = slab_obj_alloc(anon_allocator);
    if (mmo) {
        mmobj_init(mmo, &anon_mmobj_ops);
        /*list_init(&mmo->mmo_un.mmo_vmas);*/
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

    if ((o->mmo_refcount - 1) == o->mmo_nrespages) {
        pframe_t *pframe_cur;
        list_iterate_begin(&o->mmo_respages, pframe_cur, pframe_t, pf_olink) {
            KASSERT(pframe_cur->pf_obj == o);
            pframe_unpin(pframe_cur);
            /*uncache the page frame*/
            /*maybe no need to free it here*/
            pframe_clean(pframe_cur);
            /*o->mmo_ops->cleanpage(o, pframe_cur);*/

            pframe_free(pframe_cur);
        } list_iterate_end();

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
    dbg(DBG_ANON, "anon function hook\n");
    KASSERT(o);

    int err = pframe_get(o, pagenum, pf);
    if (err < 0) {
        KASSERT(*pf == NULL);
        return err;
    }

    /*NOT here!!! We're not sure it's resident or not*/
    /*pin it once we get it*/
    /*pframe_pin(*pf);*/

    return err;
        /*NOT_YET_IMPLEMENTED("VM: anon_lookuppage");*/
        /*return -1;*/
}

/* The following three functions should not be difficult. */

static int
anon_fillpage(mmobj_t *o, pframe_t *pf)
{
    dbg(DBG_ANON, "anon function hook\n");
    KASSERT(o);
    KASSERT(pf);
    KASSERT(pf->pf_addr);
    KASSERT(o == pf->pf_obj);

    memset(pf->pf_addr, 0, PAGE_SIZE);
    pframe_pin(pf);

    return 0;
        /*NOT_YET_IMPLEMENTED("VM: anon_fillpage");*/
        /*return 0;*/
}

static int
anon_dirtypage(mmobj_t *o, pframe_t *pf)
{
    dbg(DBG_ANON, "anon function hook\n");
    KASSERT(o);
    KASSERT(pf);
    KASSERT(pf->pf_addr);
    KASSERT(o == pf->pf_obj);

    return 0;
        /*NOT_YET_IMPLEMENTED("VM: anon_dirtypage");*/
        /*return -1;*/
}

static int
anon_cleanpage(mmobj_t *o, pframe_t *pf)
{
    dbg(DBG_ANON, "anon function hook\n");
    KASSERT(o);
    KASSERT(pf);
    KASSERT(pf->pf_addr);
    KASSERT(o == pf->pf_obj);

    /*just disable them for now*/
    /*seems most of the cleanup is done during pageoutd*/
    /*pframe_free(pf);*/
    
    /*
     *it's pinned thru the whole life cycle, and should be unpinned
     *before it's cleaned
     */
    /*pframe_unpin(pf);*/

    return 0;
        /*NOT_YET_IMPLEMENTED("VM: anon_cleanpage");*/
        /*return -1;*/
}
