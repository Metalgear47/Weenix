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

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/shadowd.h"

#define SHADOW_SINGLETON_THRESHOLD 5

int shadow_count = 0; /* for debugging/verification purposes */
#ifdef __SHADOWD__
/*
 * number of shadow objects with a single parent, that is another shadow
 * object in the shadow objects tree(singletons)
 */
static int shadow_singleton_count = 0;
#endif

static slab_allocator_t *shadow_allocator;

static void shadow_ref(mmobj_t *o);
static void shadow_put(mmobj_t *o);
static int  shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf);
static int  shadow_fillpage(mmobj_t *o, pframe_t *pf);
static int  shadow_dirtypage(mmobj_t *o, pframe_t *pf);
static int  shadow_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t shadow_mmobj_ops = {
        .ref = shadow_ref,
        .put = shadow_put,
        .lookuppage = shadow_lookuppage,
        .fillpage  = shadow_fillpage,
        .dirtypage = shadow_dirtypage,
        .cleanpage = shadow_cleanpage
};

/*
 * This function is called at boot time to initialize the
 * shadow page sub system. Currently it only initializes the
 * shadow_allocator object.
 */
void
shadow_init()
{
    dbg(DBG_VM, "shadow function hook\n");
    shadow_allocator = slab_allocator_create("shadow object", sizeof(mmobj_t));
    KASSERT(shadow_allocator);
        /*NOT_YET_IMPLEMENTED("VM: shadow_init");*/
}

/*
 * You'll want to use the shadow_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
shadow_create()
{
    dbg(DBG_VM, "shadow function hook\n");
    mmobj_t *mmo = slab_obj_alloc(shadow_allocator);
    if (mmo) {
        mmobj_init(mmo, &shadow_mmobj_ops);
        /*mmo->mmo_un.mmo_bottom_obj = mmobj_bottom_obj(mmo);*/
    }
    return mmo;
        /*NOT_YET_IMPLEMENTED("VM: shadow_create");*/
        /*return NULL;*/
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void
shadow_ref(mmobj_t *o)
{
    dbg(DBG_VM, "shadow function hook\n");
    KASSERT(o);
    KASSERT(o->mmo_refcount >= 0);

    o->mmo_refcount++;
    dbg(DBG_ANON, "shadow_ref: 0x%p, up to %d, nrespages=%d\n",
        o,  o->mmo_refcount, o->mmo_nrespages);
        /*NOT_YET_IMPLEMENTED("VM: shadow_ref");*/
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is a shadow object, it will never
 * be used again. You should unpin and uncache all of the object's
 * pages and then free the object itself.
 */
static void
shadow_put(mmobj_t *o)
{
    dbg(DBG_VM, "shadow function hook\n");
    KASSERT(o);

    KASSERT(0 <= o->mmo_nrespages);
    KASSERT(o->mmo_nrespages < o->mmo_refcount);
    KASSERT(o->mmo_shadowed);

    dbg(DBG_ANON, "shadow_put: 0x%p, down to %d, nrespages = %d\n",
        o, o->mmo_refcount - 1, o->mmo_nrespages);

    if ((o->mmo_refcount - 1) == o->mmo_nrespages) {
        pframe_t *pframe_cur;
        list_iterate_begin(&o->mmo_respages, pframe_cur, pframe_t, pf_olink) {
            KASSERT(pframe_cur->pf_obj == o);
            pframe_unpin(pframe_cur);
            /*uncache the page frame*/
            /*pframe_clean(pframe_cur);*/
            if (pframe_is_dirty(pframe_cur)) {
                pframe_clean(pframe_cur);
            }
            
            pframe_free(pframe_cur);
        } list_iterate_end();

        KASSERT(0 == o->mmo_nrespages);
        KASSERT(1 == o->mmo_refcount);
    }

    if (0 < --o->mmo_refcount) {
        return;
    }

    o->mmo_shadowed->mmo_ops->put(o->mmo_shadowed);
    o->mmo_un.mmo_bottom_obj->mmo_ops->put(o->mmo_un.mmo_bottom_obj);

    KASSERT(0 == o->mmo_nrespages);
    KASSERT(0 == o->mmo_refcount);

    slab_obj_free(shadow_allocator, o);
        /*NOT_YET_IMPLEMENTED("VM: shadow_put");*/
}

/* This function looks up the given page in this shadow object. The
 * forwrite argument is true if the page is being looked up for
 * writing, false if it is being looked up for reading. This function
 * must handle all do-not-copy-on-not-write magic (i.e. when forwrite
 * is false find the first shadow object in the chain which has the
 * given page resident). copy-on-write magic (necessary when forwrite
 * is true) is handled in shadow_fillpage, not here. It is important to
 * use iteration rather than recursion here as a recursive implementation
 * can overflow the kernel stack when looking down a long shadow chain */
static int
shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
    dbg(DBG_VM, "shadow function hook\n");
    if (forwrite == 0) {
        mmobj_t *bottom_obj = o->mmo_un.mmo_bottom_obj;
        KASSERT(bottom_obj);

        while (o != bottom_obj) {
            *pf = pframe_get_resident(o, pagenum);
            if (*pf) {
                return 0;
            }
            o = o->mmo_shadowed;
        }
        /*the bottom of the shadow chain whould not be a shadow object*/
        KASSERT(o->mmo_shadowed == NULL);
        return pframe_lookup(o, pagenum, 0, pf);
        /*
         **pf = pframe_get_resident(o, pagenum);
         *if (*pf == NULL) {
         *    return -EINVAL;
         *}
         *KASSERT(*pf);
         *return 0;
         */
    } else {
        return pframe_get(o, pagenum, pf);
    }
        /*NOT_YET_IMPLEMENTED("VM: shadow_lookuppage");*/
        /*return 0;*/
}

/* As per the specification in mmobj.h, fill the page frame starting
 * at address pf->pf_addr with the contents of the page identified by
 * pf->pf_obj and pf->pf_pagenum. This function handles all
 * copy-on-write magic (i.e. if there is a shadow object which has
 * data for the pf->pf_pagenum-th page then we should take that data,
 * if no such shadow object exists we need to follow the chain of
 * shadow objects all the way to the bottom object and take the data
 * for the pf->pf_pagenum-th page from the last object in the chain).
 * It is important to use iteration rather than recursion here as a 
 * recursive implementation can overflow the kernel stack when 
 * looking down a long shadow chain */
static int
shadow_fillpage(mmobj_t *o, pframe_t *pf)
{
    dbg(DBG_VM, "shadow function hook\n");
    KASSERT(o == pf->pf_obj);
    mmobj_t *bottom_obj = o->mmo_un.mmo_bottom_obj;
    KASSERT(bottom_obj);

    o = o->mmo_shadowed;
    while (o != bottom_obj) {
        pframe_t *pf_source = pframe_get_resident(o, pf->pf_pagenum);
        if (pf_source) {
            /*pf_source can be the same as pf*/
            KASSERT(pf_source != pf);
            memcpy(pf->pf_addr, pf_source->pf_addr, PAGE_SIZE);
            pframe_pin(pf);
            return 0;
        }
        o = o->mmo_shadowed;
    }

    KASSERT(o->mmo_shadowed == NULL);
    KASSERT(o == bottom_obj);

    pframe_t *pf_source = NULL;
    int err = pframe_get(o, pf->pf_pagenum, &pf_source);
    if (err < 0) {
        KASSERT(pf_source == NULL);
        return err;
    }
    
    KASSERT(pf_source);
    memcpy(pf->pf_addr, pf_source->pf_addr, PAGE_SIZE);
    pframe_pin(pf);
    return 0;
        /*NOT_YET_IMPLEMENTED("VM: shadow_fillpage");*/
        /*return 0;*/
}

/* These next two functions are not difficult. */

static int
shadow_dirtypage(mmobj_t *o, pframe_t *pf)
{
    dbg(DBG_VM, "shadow function hook\n");
    KASSERT(o);
    KASSERT(pf);
    KASSERT(pf->pf_addr);
    KASSERT(o == pf->pf_obj);

    return 0;
        NOT_YET_IMPLEMENTED("VM: shadow_dirtypage");
        return -1;
}

static int
shadow_cleanpage(mmobj_t *o, pframe_t *pf)
{
    dbg(DBG_VM, "shadow function hook\n");
    KASSERT(o);
    KASSERT(pf);
    KASSERT(pf->pf_addr);
    KASSERT(o == pf->pf_obj);

    return 0;
        NOT_YET_IMPLEMENTED("VM: shadow_cleanpage");
        return -1;
}
