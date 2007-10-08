#ifndef __STOREAPI_H
#define __STOREAPI_H

#include <string>
#include <map>

#include <boost/shared_ptr.hpp>

#include "hash.hh"
#include "serialise.hh"
#include "derivations.hh"
#include "db.hh"


namespace nix {


/* A substitute is a program invocation that constructs some store
   path (typically by fetching it from somewhere, e.g., from the
   network). */
struct Substitute
{       
    /* The derivation that built this store path (empty if none). */
    Path deriver;
    
    /* Program to be executed to create the store path.  Must be in
       the output path of `storeExpr'. */
    Path program;

    /* Extra arguments to be passed to the program (the first argument
       is the store path to be substituted). */
    Strings args;

    bool operator == (const Substitute & sub) const;
};

typedef list<Substitute> Substitutes;


typedef std::map<Path, Path> Roots;


/* Garbage collector operation. */
typedef enum {
    gcReturnRoots,
    gcReturnLive,
    gcReturnDead,
    gcDeleteDead,
    gcDeleteSpecific,
} GCAction;


class StoreAPI 
{
public:

    virtual ~StoreAPI() { }

    /* Checks whether a path is valid. */ 
    virtual bool isValidPath(const Path & path) = 0;
    
    /* Checks whether a state-path is valid. */
    virtual bool isValidStatePath(const Path & path) = 0;
    
    /* TODO */
    virtual bool isValidComponentOrStatePath(const Path & path) = 0;

    /* Return the substitutes for the given path. */
    virtual Substitutes querySubstitutes(const Path & path) = 0;

    /* More efficient variant if we just want to know if a path has
       substitutes. */
    virtual bool hasSubstitutes(const Path & path);

    /* Queries the hash of a valid path. */ 
    virtual Hash queryPathHash(const Path & path) = 0;

	/* Queries the derivation Path of a valid state path. */
	virtual Path queryStatePathDrv(const Path & statePath) = 0;

    /* Queries the set of outgoing FS references for a store path.
       The result is not cleared. */
    virtual void queryStoreReferences(const Path & path,
        PathSet & references, const unsigned int revision) = 0;
        
    /* Queries the set of outgoing FS state-references for a store path.
       The result is not cleared. */
    virtual void queryStateReferences(const Path & storePath, PathSet & stateReferences, const unsigned int revision) = 0;

    /* Queries the set of incoming FS references for a store path.
       The result is not cleared. */
    virtual void queryStoreReferrers(const Path & path,
        PathSet & referrers, const unsigned int revision) = 0;

	/* Queries the set of incoming FS state-references for a store path.
       The result is not cleared. */
    virtual void queryStateReferrers(const Path & path, PathSet & stateReferrers, const unsigned int revision) = 0;

    /* Query the deriver of a store path.  Return the empty string if
       no deriver has been set. */
    virtual Path queryDeriver(const Path & path) = 0;

    /* Copy the contents of a path to the store and register the
       validity the resulting path.  The resulting path is returned.
       If `fixed' is true, then the output of a fixed-output
       derivation is pre-loaded into the Nix store.  The function
       object `filter' can be used to exclude files (see
       libutil/archive.hh). */
    virtual Path addToStore(const Path & srcPath, bool fixed = false,
        bool recursive = false, string hashAlgo = "",
        PathFilter & filter = defaultPathFilter) = 0;

    /* Like addToStore, but the contents written to the output path is
       a regular file containing the given string. */
    virtual Path addTextToStore(const string & suffix, const string & s,
        const PathSet & references) = 0;

    /* Export a store path, that is, create a NAR dump of the store
       path and append its references and its deriver.  Optionally, a
       cryptographic signature (created by OpenSSL) of the preceding
       data is attached. */
    virtual void exportPath(const Path & path, bool sign,
        Sink & sink) = 0;

    /* Import a NAR dump created by exportPath() into the Nix
       store. */
    virtual Path importPath(bool requireSignature, Source & source) = 0;

    /* Ensure that the output paths of the derivation are valid.  If
       they are already valid, this is a no-op.  Otherwise, validity
       can be reached in two ways.  First, if the output paths have
       substitutes, then those can be used.  Second, the output paths
       can be created by running the builder, after recursively
       building any sub-derivations. */
    virtual void buildDerivations(const PathSet & drvPaths) = 0;

    /* Ensure that a path is valid.  If it is not currently valid, it
       may be made valid by running a substitute (if defined for the
       path). */
    virtual void ensurePath(const Path & path) = 0;

    /* Add a store path as a temporary root of the garbage collector.
       The root disappears as soon as we exit. */
    virtual void addTempRoot(const Path & path) = 0;

    /* Add an indirect root, which is merely a symlink to `path' from
       /nix/var/nix/gcroots/auto/<hash of `path'>.  `path' is supposed
       to be a symlink to a store path.  The garbage collector will
       automatically remove the indirect root when it finds that
       `path' has disappeared. */
    virtual void addIndirectRoot(const Path & path) = 0;

    /* Acquire the global GC lock, then immediately release it.  This
       function must be called after registering a new permanent root,
       but before exiting.  Otherwise, it is possible that a running
       garbage collector doesn't see the new root and deletes the
       stuff we've just built.  By acquiring the lock briefly, we
       ensure that either:

       - The collector is already running, and so we block until the
         collector is finished.  The collector will know about our
         *temporary* locks, which should include whatever it is we
         want to register as a permanent lock.

       - The collector isn't running, or it's just started but hasn't
         acquired the GC lock yet.  In that case we get and release
         the lock right away, then exit.  The collector scans the
         permanent root and sees our's.

       In either case the permanent root is seen by the collector. */
    virtual void syncWithGC() = 0;

    /* Find the roots of the garbage collector.  Each root is a pair
       (link, storepath) where `link' is the path of the symlink
       outside of the Nix store that point to `storePath'.  */
    virtual Roots findRoots() = 0;

    /* Depending on `action', this function does the following:

       - `gcReturnRoots': find and return the set of roots for the
         garbage collector.  These are the store paths symlinked to in
         the `gcroots' directory.

       - `gcReturnLive': return the set of paths reachable from
         (i.e. in the closure of) the roots.

       - `gcReturnDead': return the set of paths not reachable from
         the roots.

       - `gcDeleteDead': actually delete the latter set.

       - `gcDeleteSpecific': delete the paths listed in
         `pathsToDelete', insofar as they are not reachable.

       If `ignoreLiveness' is set, then reachability from the roots is
       ignored (dangerous!).  However, the paths must still be
       unreferenced *within* the store (i.e., there can be no other
       store paths that depend on them).

       For `gcReturnDead', `gcDeleteDead' and `gcDeleteSpecific', the
       number of bytes that would be or was freed is returned in
       `bytesFreed'. */
    virtual void collectGarbage(GCAction action, const PathSet & pathsToDelete,
        bool ignoreLiveness, PathSet & result, unsigned long long & bytesFreed) = 0;
        
    /* TODO */
	virtual void setStatePathsInterval(const PathSet & statePath, const IntVector & intervals, bool allZero = false) = 0;
	
	/* TODO */
	virtual IntVector getStatePathsInterval(const PathSet & statePaths) = 0;
	
	/* Checks whether a path is a component path that has a statePath. */
	virtual bool isStateComponent(const Path & path) = 0;
	
	/* TODO */
	virtual void storePathRequisites(const Path & storeOrstatePath, const bool includeOutputs, PathSet & paths, const bool withComponents, const bool withState, const unsigned int revision) = 0;

	/* TODO */
	virtual void setStateRevisions(const RevisionClosure & revisions, const Path & rootStatePath, const string & comment) = 0;
	
	/*  
	 * TODO Not sure wheter this comment is 100% correct
	 * Querys all available state revision closures.
	 * (e.g. a references scan can change this)
	 */
	virtual bool queryStateRevisions(const Path & statePath, RevisionClosure & revisions, RevisionClosureTS & timestamps, const unsigned int revision) = 0;
	
	/* TODO */
	virtual bool queryAvailableStateRevisions(const Path & statePath, RevisionInfos & revisions) = 0;
	
	/* TODO */
	virtual Snapshots commitStatePath(const Path & statePath) = 0;

	/* TODO */
	virtual PathSet queryDerivers(const Path & storePath, const string & identifier, const string & user) = 0;

	/* TODO */
	virtual void scanAndUpdateAllReferences(const Path & statePath, const bool recursive) = 0;
	
	/* TODO */
	virtual bool getSharedWith(const Path & statePath1, Path & statePath2) = 0;
	
	/* TODO */
	virtual PathSet toNonSharedPathSet(const PathSet & statePaths) = 0;
	
	/* TODO */
	virtual void revertToRevision(const Path & statePath, const unsigned int revision_arg, const bool recursive) = 0;
	
	/* TODO */
	virtual void shareState(const Path & from, const Path & to, const bool snapshot) = 0;
	
	/* TODO */
	virtual void unShareState(const Path & path, const bool branch, const bool restoreOld) = 0;
};


/* !!! These should be part of the store API, I guess. */

/* Throw an exception if `path' is not directly in the Nix store. */
void assertStorePath(const Path & path);
void assertStatePath(const Path & path);

bool isInStore(const Path & path);
bool isStorePath(const Path & path);

bool isInStateStore(const Path & path);
bool isStatePath(const Path & path);


void checkStoreName(const string & name);

/* Chop off the parts after the top-level store name, e.g.,
   /nix/store/abcd-foo/bar => /nix/store/abcd-foo. */
Path toStorePath(const Path & path);
Path toStoreOrStatePath(const Path & path);

/* Constructs a unique store path name. */
Path makeStorePath(const string & type,
    const Hash & hash, const string & suffix);
    
Path makeFixedOutputPath(bool recursive,
    string hashAlgo, Hash hash, string name);

/* TODO ... */
Path makeStatePath(const string & componentHash, const string & suffix, const string & stateIdentifier, const string & user);

/* TODO ... */
void checkStatePath(const Derivation & drv);

/* This is the preparatory part of addToStore() and addToStoreFixed();
   it computes the store path to which srcPath is to be copied.
   Returns the store path and the cryptographic hash of the
   contents of srcPath. */
std::pair<Path, Hash> computeStorePathForPath(const Path & srcPath,
    bool fixed = false, bool recursive = false, string hashAlgo = "",
    PathFilter & filter = defaultPathFilter);

/* Preparatory part of addTextToStore().

   !!! Computation of the path should take the references given to
   addTextToStore() into account, otherwise we have a (relatively
   minor) security hole: a caller can register a source file with
   bogus references.  If there are too many references, the path may
   not be garbage collected when it has to be (not really a problem,
   the caller could create a root anyway), or it may be garbage
   collected when it shouldn't be (more serious).

   Hashing the references would solve this (bogus references would
   simply yield a different store path, so other users wouldn't be
   affected), but it has some backwards compatibility issues (the
   hashing scheme changes), so I'm not doing that for now. */
Path computeStorePathForText(const string & suffix, const string & s,
    const PathSet & references);


/* Remove the temporary roots file for this process.  Any temporary
   root becomes garbage after this point unless it has been registered
   as a (permanent) root. */
void removeTempRoots();


/* Register a permanent GC root. */
Path addPermRoot(const Path & storePath, const Path & gcRoot,
    bool indirect, bool allowOutsideRootsDir = false);


/* Sort a set of paths topologically under the references relation.
   If p refers to q, then p follows q in this list. */
Paths topoSortPaths(const PathSet & paths);


/* For now, there is a single global store API object, but we'll
   purify that in the future. */
extern boost::shared_ptr<StoreAPI> store;


/* Factory method: open the Nix database, either through the local or
   remote implementation. */
boost::shared_ptr<StoreAPI> openStore(bool reserveSpace = true);


}


#endif /* !__STOREAPI_H */
