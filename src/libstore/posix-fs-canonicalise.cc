#include "nix/store/posix-fs-canonicalise.hh"
#include "nix/util/file-system.hh"
#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-api.hh"

#include "store-config-private.hh"

#if NIX_SUPPORT_ACL
# include <sys/xattr.h>
#endif

namespace nix {

const time_t mtimeStore = 1; /* 1 second into the epoch */


static void canonicaliseTimestampAndPermissions(const Path & path, const struct stat & st)
{
    if (!S_ISLNK(st.st_mode)) {

        /* Mask out all type related bits. */
        mode_t mode = st.st_mode & ~S_IFMT;

        if (mode != 0444 && mode != 0555) {
            mode = (st.st_mode & S_IFMT)
                 | 0444
                 | (st.st_mode & S_IXUSR ? 0111 : 0);
            if (chmod(path.c_str(), mode) == -1)
                throw SysError("changing mode of '%1%' to %2$o", path, mode);
        }

    }

    if (st.st_mtime != mtimeStore) {
        struct stat st2 = st;
        st2.st_mtime = mtimeStore,
        setWriteTime(path, st2);
    }
}


void canonicaliseTimestampAndPermissions(const Path & path)
{
    canonicaliseTimestampAndPermissions(path, lstat(path));
}


static void canonicalisePathMetaData_(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen)
{
    checkInterrupt();

#ifdef __APPLE__
    /* Remove flags, in particular UF_IMMUTABLE which would prevent
       the file from being garbage-collected. FIXME: Use
       setattrlist() to remove other attributes as well. */
    if (lchflags(path.c_str(), 0)) {
        if (errno != ENOTSUP)
            throw SysError("clearing flags of path '%1%'", path);
    }
#endif

    auto st = lstat(path);

    /* Really make sure that the path is of a supported type. */
    if (!(S_ISREG(st.st_mode) || S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)))
        throw Error("file '%1%' has an unsupported type", path);

#if NIX_SUPPORT_ACL
    /* Remove extended attributes / ACLs. */
    ssize_t eaSize = llistxattr(path.c_str(), nullptr, 0);

    if (eaSize < 0) {
        if (errno != ENOTSUP && errno != ENODATA)
            throw SysError("querying extended attributes of '%s'", path);
    } else if (eaSize > 0) {
        std::vector<char> eaBuf(eaSize);

        if ((eaSize = llistxattr(path.c_str(), eaBuf.data(), eaBuf.size())) < 0)
            throw SysError("querying extended attributes of '%s'", path);

        for (auto & eaName: tokenizeString<Strings>(std::string(eaBuf.data(), eaSize), std::string("\000", 1))) {
            if (settings.ignoredAcls.get().count(eaName)) continue;
            if (lremovexattr(path.c_str(), eaName.c_str()) == -1)
                throw SysError("removing extended attribute '%s' from '%s'", eaName, path);
        }
     }
#endif

    /* Fail if the file is not owned by the build user.  This prevents
       us from messing up the ownership/permissions of files
       hard-linked into the output (e.g. "ln /etc/shadow $out/foo").
       However, ignore files that we chown'ed ourselves previously to
       ensure that we don't fail on hard links within the same build
       (i.e. "touch $out/foo; ln $out/foo $out/bar"). */
    if (uidRange && (st.st_uid < uidRange->first || st.st_uid > uidRange->second)) {
        if (S_ISDIR(st.st_mode) || !inodesSeen.count(Inode(st.st_dev, st.st_ino)))
            throw BuildError("invalid ownership on file '%1%'", path);
        mode_t mode = st.st_mode & ~S_IFMT;
        assert(S_ISLNK(st.st_mode) || (st.st_uid == geteuid() && (mode == 0444 || mode == 0555) && st.st_mtime == mtimeStore));
        return;
    }

    inodesSeen.insert(Inode(st.st_dev, st.st_ino));

    canonicaliseTimestampAndPermissions(path, st);

    /* Change ownership to the current uid.  If it's a symlink, use
       lchown if available, otherwise don't bother.  Wrong ownership
       of a symlink doesn't matter, since the owning user can't change
       the symlink and can't delete it because the directory is not
       writable.  The only exception is top-level paths in the Nix
       store (since that directory is group-writable for the Nix build
       users group); we check for this case below. */
    if (st.st_uid != geteuid()) {
#if HAVE_LCHOWN
        if (lchown(path.c_str(), geteuid(), getegid()) == -1)
#else
        if (!S_ISLNK(st.st_mode) &&
            chown(path.c_str(), geteuid(), getegid()) == -1)
#endif
            throw SysError("changing owner of '%1%' to %2%",
                path, geteuid());
    }

    if (S_ISDIR(st.st_mode)) {
        for (auto & i : DirectoryIterator{path}) {
            checkInterrupt();
            canonicalisePathMetaData_(
                i.path().string(),
                uidRange,
                inodesSeen);
        }
    }
}


void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen)
{
    canonicalisePathMetaData_(
        path,
        uidRange,
        inodesSeen);

    /* On platforms that don't have lchown(), the top-level path can't
       be a symlink, since we can't change its ownership. */
    auto st = lstat(path);

    if (st.st_uid != geteuid()) {
        assert(S_ISLNK(st.st_mode));
        throw Error("wrong ownership of top-level store path '%1%'", path);
    }
}


void canonicalisePathMetaData(const Path & path
    , std::optional<std::pair<uid_t, uid_t>> uidRange
    )
{
    InodesSeen inodesSeen;
    canonicalisePathMetaData_(
        path,
        uidRange,
        inodesSeen);
}

}
