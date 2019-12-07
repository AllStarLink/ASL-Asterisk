/*
 * astobj2 - replacement containers for asterisk data structures.
 *
 * Copyright (C) 2006 Marta Carbone, Luigi Rizzo - Univ. di Pisa, Italy
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#ifndef _ASTERISK_ASTOBJ2_H
#define _ASTERISK_ASTOBJ2_H

#include "asterisk/compat.h"

/*! \file 
 *
 * \brief Object Model implementing objects and containers.

These functions implement an abstraction for objects (with
locks and reference counts) and containers for these user-defined objects,
supporting locking, reference counting and callbacks.

The internal implementation of the container is opaque to the user,
so we can use different data structures as needs arise.

At the moment, however, the only internal data structure is a hash
table. When other structures will be implemented, the initialization
function may change.

USAGE - OBJECTS

An object is a block of memory that must be allocated with the
function ao2_alloc(), and for which the system keeps track (with
abit of help from the programmer) of the number of references around.
When an object has no more references, it is destroyed, by first
invoking whatever 'destructor' function the programmer specifies
(it can be NULL), and then freeing the memory.
This way objects can be shared without worrying who is in charge
of freeing them.

Basically, creating an object requires the size of the object and
and a pointer to the destructor function:
 
    struct foo *o;
 
    o = ao2_alloc(sizeof(struct foo), my_destructor_fn);

The object returned has a refcount = 1.
Note that the memory for the object is allocated and zeroed.
- We cannot realloc() the object itself.
- We cannot call free(o) to dispose of the object; rather we
  tell the system that we do not need the reference anymore:

    ao2_ref(o, -1)

  causing the destructor to be called (and then memory freed) when
  the refcount goes to 0. This is also available as ao2_unref(o),
  and returns NULL as a convenience, so you can do things like
	o = ao2_unref(o);
  and clean the original pointer to prevent errors.

- ao2_ref(o, +1) can be used to modify the refcount on the
  object in case we want to pass it around.
	

- other calls on the object are ao2_lock(obj), ao2_unlock(),
  ao2_trylock(), to manipulate the lock.


USAGE - CONTAINERS

A containers is an abstract data structure where we can store
objects, search them (hopefully in an efficient way), and iterate
or apply a callback function to them. A container is just an object
itself.

A container must first be allocated, specifying the initial
parameters. At the moment, this is done as follows:

    <b>Sample Usage:</b>
    \code

    struct ao2_container *c;

    c = ao2_container_alloc(MAX_BUCKETS, my_hash_fn, my_cmp_fn);

where
- MAX_BUCKETS is the number of buckets in the hash table,
- my_hash_fn() is the (user-supplied) function that returns a
  hash key for the object (further reduced moduly MAX_BUCKETS
  by the container's code);
- my_cmp_fn() is the default comparison function used when doing
  searches on the container,

A container knows little or nothing about the object itself,
other than the fact that it has been created by ao2_alloc()
All knowledge of the (user-defined) internals of the object
is left to the (user-supplied) functions passed as arguments
to ao2_container_alloc().

If we want to insert the object in the container, we should
initialize its fields -- especially, those used by my_hash_fn() --
to compute the bucket to use.
Once done, we can link an object to a container with

    ao2_link(c, o);

The function returns NULL in case of errors (and the object
is not inserted in the container). Other values mean success
(we are not supposed to use the value as a pointer to anything).

\note While an object o is in a container, we expect that
my_hash_fn(o) will always return the same value. The function
does not lock the object to be computed, so modifications of
those fields that affect the computation of the hash should
be done by extractiong the object from the container, and
reinserting it after the change (this is not terribly expensive).

\note A container with a single buckets is effectively a linked
list. However there is no ordering among elements.

Objects implement a reference counter keeping the count
of the number of references that reference an object.

When this number becomes zero the destructor will be
called and the object will be free'd.
 */

/*!
 * Invoked just before freeing the memory for the object.
 * It is passed a pointer to user data.
 */
typedef void (*ao2_destructor_fn)(void *);

void ao2_bt(void);	/* backtrace */
/*!
 * Allocate and initialize an object.
 * 
 * \param data_size The sizeof() of user-defined structure.
 * \param destructor_fn The function destructor (can be NULL)
 * \return A pointer to user data. 
 *
 * Allocates a struct astobj2 with sufficient space for the
 * user-defined structure.
 * \notes:
 * - storage is zeroed; XXX maybe we want a flag to enable/disable this.
 * - the refcount of the object just created is 1
 * - the returned pointer cannot be free()'d or realloc()'ed;
 *   rather, we just call ao2_ref(o, -1);
 */
void *ao2_alloc(const size_t data_size, ao2_destructor_fn destructor_fn);

/*!
 * Reference/unreference an object and return the old refcount.
 *
 * \param o A pointer to the object
 * \param delta Value to add to the reference counter.
 * \return The value of the reference counter before the operation.
 *
 * Increase/decrease the reference counter according
 * the value of delta.
 *
 * If the refcount goes to zero, the object is destroyed.
 *
 * \note The object must not be locked by the caller of this function, as
 *       it is invalid to try to unlock it after releasing the reference.
 *
 * \note if we know the pointer to an object, it is because we
 * have a reference count to it, so the only case when the object
 * can go away is when we release our reference, and it is
 * the last one in existence.
 */
int ao2_ref(void *o, int delta);

/*!
 * Lock an object.
 * 
 * \param a A pointer to the object we want lock.
 * \return 0 on success, other values on error.
 */
#ifndef DEBUG_THREADS
int ao2_lock(void *a);
#else
#define ao2_lock(a) __ao2_lock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
int __ao2_lock(void *a, const char *file, const char *func, int line, const char *var);
#endif

#ifndef DEBUG_THREADS
int ao2_trylock(void *a);
#else
#define ao2_trylock(a) __ao2_trylock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
int __ao2_trylock(void *a, const char *file, const char *func, int line, const char *var);
#endif

/*!
 * Unlock an object.
 * 
 * \param a A pointer to the object we want unlock.
 * \return 0 on success, other values on error.
 */
#ifndef DEBUG_THREADS
int ao2_unlock(void *a);
#else
#define ao2_unlock(a) __ao2_unlock(a, __FILE__, __PRETTY_FUNCTION__, __LINE__, #a)
int __ao2_unlock(void *a, const char *file, const char *func, int line, const char *var);
#endif

/*!
 *
 * Containers

containers are data structures meant to store several objects,
and perform various operations on them.
Internally, objects are stored in lists, hash tables or other
data structures depending on the needs.

NOTA BENE: at the moment the only container we support is the
hash table and its degenerate form, the list.

Operations on container include:

    c = ao2_container_alloc(size, cmp_fn, hash_fn)
	allocate a container with desired size and default compare
	and hash function

    ao2_find(c, arg, flags)
	returns zero or more element matching a given criteria
	(specified as arg). Flags indicate how many results we
	want (only one or all matching entries), and whether we
	should unlink the object from the container.

    ao2_callback(c, flags, fn, arg)
	apply fn(obj, arg) to all objects in the container.
	Similar to find. fn() can tell when to stop, and
	do anything with the object including unlinking it.
	Note that the entire operation is run with the container
	locked, so noone else can change its content while we work on it.
	However, we pay this with the fact that doing
	anything blocking in the callback keeps the container
	blocked.
	The mechanism is very flexible because the callback function fn()
	can do basically anything e.g. counting, deleting records, etc.
	possibly using arg to store the results.
   
    iterate on a container
	this is done with the following sequence

	    struct ao2_container *c = ... // our container
	    struct ao2_iterator i;
	    void *o;

	    i = ao2_iterator_init(c, flags);
     
	    while ( (o = ao2_iterator_next(&i)) ) {
		... do something on o ...
		ao2_ref(o, -1);
	    }

 	    ao2_iterator_destroy(&i);

	The difference with the callback is that the control
	on how to iterate is left to us.

    ao2_ref(c, -1)
	dropping a reference to a container destroys it, very simple!
 
Containers are astobj2 object themselves, and this is why their
implementation is simple too.

 */

/*!
 * We can perform different operation on an object. We do this
 * according the following flags.
 */
enum search_flags {
	/*! unlink the object found */
	OBJ_UNLINK	 = (1 << 0),
	/*! on match, don't return the object or increase its reference count. */
	OBJ_NODATA	 = (1 << 1),
	/*! don't stop at the first match 
	 *  \note This is not fully implemented. */
	OBJ_MULTIPLE = (1 << 2),
	/*! obj is an object of the same type as the one being searched for.
	 *  This implies that it can be passed to the object's hash function
	 *  for optimized searching. */
	OBJ_POINTER	 = (1 << 3),
	/*! 
	 * \brief Continue if a match is not found in the hashed out bucket
	 *
	 * This flag is to be used in combination with OBJ_POINTER.  This tells
	 * the ao2_callback() core to keep searching through the rest of the
	 * buckets if a match is not found in the starting bucket defined by
	 * the hash value on the argument.
	 */
	OBJ_CONTINUE = (1 << 4),

};

/*!
 * Type of a generic function to generate a hash value from an object.
 *
 */
typedef int (*ao2_hash_fn)(const void *obj, const int flags);

/*!
 * valid callback results:
 * We return a combination of
 * CMP_MATCH when the object matches the request,
 * and CMP_STOP when we should not continue the search further.
 */
enum _cb_results {
	CMP_MATCH	= 0x1,
	CMP_STOP	= 0x2,
};

/*!
 * generic function to compare objects.
 * This, as other callbacks, should return a combination of
 * _cb_results as described above.
 *
 * \param o	object from container
 * \param arg	search parameters (directly from ao2_find)
 * \param flags	passed directly from ao2_find
 *	XXX explain.
 */

/*!
 * Type of a generic callback function
 * \param obj  pointer to the (user-defined part) of an object.
 * \param arg callback argument from ao2_callback()
 * \param flags flags from ao2_callback()
 * The return values are the same as a compare function.
 * In fact, they are the same thing.
 */
typedef int (*ao2_callback_fn)(void *obj, void *arg, int flags);

/*!
 * Here start declarations of containers.
 */
struct ao2_container;

/*!
 * Allocate and initialize a container 
 * with the desired number of buckets.
 * 
 * We allocate space for a struct astobj_container, struct container
 * and the buckets[] array.
 *
 * \param my_hash_fn Pointer to a function computing a hash value.
 * \param my_cmp_fn Pointer to a function comparating key-value 
 * 			with a string. (can be NULL)
 * \return A pointer to a struct container.
 *
 * destructor is set implicitly.
 */
struct ao2_container *ao2_container_alloc(const unsigned int n_buckets,
		ao2_hash_fn hash_fn, ao2_callback_fn cmp_fn);

/*!
 * Returns the number of elements in a container.
 */
int ao2_container_count(struct ao2_container *c);

/*
 * Here we have functions to manage objects.
 *
 * We can use the functions below on any kind of 
 * object defined by the user.
 */

/*!
 * \brief Add an object to a container.
 *
 * \param c the container to operate on.
 * \param newobj the object to be added.
 *
 * \return NULL on errors, other values on success.
 *
 * This function inserts an object in a container according its key.
 *
 * \note Remember to set the key before calling this function.
 *
 * \note This function automatically increases the reference count to
 *       account for the reference to the object that the container now holds.
 *
 * For Asterisk 1.4 only, there is a dirty hack here to ensure that chan_iax2
 * can have objects linked in to the container at the head instead of tail
 * when it is just a linked list.  This is to maintain some existing behavior
 * where the order must be maintained as it was before this conversion so that
 * matching behavior doesn't change.
 */
#define ao2_link(c, o) __ao2_link(c, o, 0)
void *__ao2_link(struct ao2_container *c, void *newobj, int iax2_hack);

/*!
 * \brief Remove an object from the container
 *
 * \arg c the container
 * \arg obj the object to unlink
 *
 * \retval NULL, always
 *
 * \note The object requested to be unlinked must be valid.  However, if it turns
 *       out that it is not in the container, this function is still safe to
 *       be called.
 *
 * \note If the object gets unlinked from the container, the container's
 *       reference to the object will be automatically released.
 */
void *ao2_unlink(struct ao2_container *c, void *obj);

/*! \struct Used as return value if the flag OBJ_MULTIPLE is set */
struct ao2_list {
	struct ao2_list *next;
	void *obj;	/* pointer to the user portion of the object */
};

/*!
 * ao2_callback() and astob2_find() are the same thing with only one difference:
 * the latter uses as a callback the function passed as my_cmp_f() at
 * the time of the creation of the container.
 * 
 * \param c A pointer to the container to operate on.
 * \param arg passed to the callback.
 * \param flags A set of flags specifying the operation to perform,
	partially used by the container code, but also passed to
	the callback.
 * \return 	A pointer to the object found/marked, 
 * 		a pointer to a list of objects matching comparison function,
 * 		NULL if not found.
 * If the function returns any objects, their refcount is incremented,
 * and the caller is in charge of decrementing them once done.
 * Also, in case of multiple values returned, the list used
 * to store the objects must be freed by the caller.
 *
 * This function searches through a container and performs operations
 * on objects according on flags passed.
 * XXX describe better
 * The comparison is done calling the compare function set implicitly. 
 * The p pointer can be a pointer to an object or to a key, 
 * we can say this looking at flags value.
 * If p points to an object we will search for the object pointed
 * by this value, otherwise we serch for a key value.
 * If the key is not uniq we only find the first matching valued.
 * If we use the OBJ_MARK flags, we mark all the objects matching 
 * the condition.
 *
 * The use of flags argument is the follow:
 *
 *	OBJ_UNLINK 		unlinks the object found
 *	OBJ_NODATA		on match, do return an object
 *				Callbacks use OBJ_NODATA as a default
 *				functions such as find() do
 *	OBJ_MULTIPLE		return multiple matches
 *				Default for _find() is no.
 *				to a key (not yet supported)
 *	OBJ_POINTER 		the pointer is an object pointer
 *
 * In case we return a list, the callee must take care to destroy 
 * that list when no longer used.
 *
 * \note When the returned object is no longer in use, ao2_ref() should
 * be used to free the additional reference possibly created by this function.
 */
/* XXX order of arguments to find */
void *ao2_find(struct ao2_container *c, void *arg, enum search_flags flags);
void *ao2_callback(struct ao2_container *c,
	enum search_flags flags,
	ao2_callback_fn cb_fn, void *arg);

int ao2_match_by_addr(void *user_data, void *arg, int flags);
/*!
 *
 *
 * When we need to walk through a container, we use
 * ao2_iterator to keep track of the current position.
 * 
 * Because the navigation is typically done without holding the
 * lock on the container across the loop,
 * objects can be inserted or deleted or moved
 * while we work. As a consequence, there is no guarantee that
 * the we manage to touch all the elements on the list, or it
 * is possible that we touch the same object multiple times.
 * However, within the current hash table container, the following is true:
 *  - It is not possible to miss an object in the container while iterating
 *    unless it gets added after the iteration begins and is added to a bucket
 *    that is before the one the current object is in.  In this case, even if
 *    you locked the container around the entire iteration loop, you still would
 *    not see this object, because it would still be waiting on the container
 *    lock so that it can be added.
 *  - It would be extremely rare to see an object twice.  The only way this can
 *    happen is if an object got unlinked from the container and added again 
 *    during the same iteration.  Furthermore, when the object gets added back,
 *    it has to be in the current or later bucket for it to be seen again.
 *
 * An iterator must be first initialized with ao2_iterator_init(),
 * then we can use o = ao2_iterator_next() to move from one
 * element to the next. Remember that the object returned by
 * ao2_iterator_next() has its refcount incremented,
 * and the reference must be explicitly released when done with it.
 *
 * In addition, ao2_iterator_init() will hold a reference to the container
 * being iterated, which will be freed when ao2_iterator_destroy() is called
 * to free up the resources used by the iterator (if any).
 *
 * Example:
 *
 *  \code
 *
 *  struct ao2_container *c = ... // the container we want to iterate on
 *  struct ao2_iterator i;
 *  struct my_obj *o;
 *
 *  i = ao2_iterator_init(c, flags);
 *
 *  while ( (o = ao2_iterator_next(&i)) ) {
 *     ... do something on o ...
 *     ao2_ref(o, -1);
 *  }
 *
 *  ao2_iterator_destroy(&i);
 *
 *  \endcode
 *
 */

/*!
 * You are not supposed to know the internals of an iterator!
 * We would like the iterator to be opaque, unfortunately
 * its size needs to be known if we want to store it around
 * without too much trouble.
 * Anyways...
 * The iterator has a pointer to the container, and a flags
 * field specifying various things e.g. whether the container
 * should be locked or not while navigating on it.
 * The iterator "points" to the current object, which is identified
 * by three values:
 * - a bucket number;
 * - the object_id, which is also the container version number
 *   when the object was inserted. This identifies the object
 *   uniquely, however reaching the desired object requires
 *   scanning a list.
 * - a pointer, and a container version when we saved the pointer.
 *   If the container has not changed its version number, then we
 *   can safely follow the pointer to reach the object in constant time.
 * Details are in the implementation of ao2_iterator_next()
 * A freshly-initialized iterator has bucket=0, version=0.
 */

struct ao2_iterator {
	/*! the container */
	struct ao2_container *c;
	/*! operation flags */
	int flags;
	/*! current bucket */
	int bucket;
	/*! container version */
	unsigned int c_version;
	/*! pointer to the current object */
	void *obj;
	/*! container version when the object was created */
	unsigned int version;
};

/*! Flags that can be passed to ao2_iterator_init() to modify the behavior
 * of the iterator.
 */
enum ao2_iterator_flags {
	/*! Prevents ao2_iterator_next() from locking the container
	 * while retrieving the next object from it.
	 */
	AO2_ITERATOR_DONTLOCK = (1 << 0),
};

/*!
 * \brief Create an iterator for a container
 *
 * \param c the container
 * \param flags one or more flags from ao2_iterator_flags
 *
 * \retval the constructed iterator
 *
 * \note This function does \b not take a pointer to an iterator;
 *       rather, it returns an iterator structure that should be
 *       assigned to (overwriting) an existing iterator structure
 *       allocated on the stack or on the heap.
 *
 * This function will take a reference on the container being iterated.
 *
 */
struct ao2_iterator ao2_iterator_init(struct ao2_container *c, int flags);

/*!
 * \brief Destroy a container iterator
 *
 * \param i the iterator to destroy
 *
 * \retval none
 *
 * This function will release the container reference held by the iterator
 * and any other resources it may be holding.
 *
 */
void ao2_iterator_destroy(struct ao2_iterator *i);

void *ao2_iterator_next(struct ao2_iterator *a);

#endif /* _ASTERISK_ASTOBJ2_H */
