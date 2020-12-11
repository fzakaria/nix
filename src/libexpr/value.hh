#pragma once

#include "symbol-table.hh"

#if HAVE_BOEHMGC
#include <gc/gc_allocator.h>
#endif

namespace nix {


typedef enum {
    tInt = 1,
    tBool,
    tString,
    tPath,
    tNull,
    tAttrs,
    tList1,
    tList2,
    tListN,
    tThunk,
    tApp,
    tLambda,
    tBlackhole,
    tPrimOp,
    tPrimOpApp,
    tExternal,
    tFloat
} ValueType;

// This type abstracts over all actual value types in the language,
// grouping together implementation details like tList*, different function
// types, and types in non-normal form (so thunks and co.)
typedef enum {
    nThunk,
    nInt,
    nFloat,
    nBool,
    nString,
    nPath,
    nNull,
    nAttrs,
    nList,
    nFunction,
    nExternal
} NormalType;

class Bindings;
struct Env;
struct Expr;
struct ExprLambda;
struct PrimOp;
class Symbol;
struct Pos;
class EvalState;
class XMLWriter;
class JSONPlaceholder;


typedef int64_t NixInt;
typedef double NixFloat;

/* External values must descend from ExternalValueBase, so that
 * type-agnostic nix functions (e.g. showType) can be implemented
 */
class ExternalValueBase
{
    friend std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);
    protected:
    /* Print out the value */
    virtual std::ostream & print(std::ostream & str) const = 0;

    public:
    /* Return a simple string describing the type */
    virtual string showType() const = 0;

    /* Return a string to be used in builtins.typeOf */
    virtual string typeOf() const = 0;

    /* Coerce the value to a string. Defaults to uncoercable, i.e. throws an
     * error
     */
    virtual string coerceToString(const Pos & pos, PathSet & context, bool copyMore, bool copyToStore) const;

    /* Compare to another value of the same type. Defaults to uncomparable,
     * i.e. always false.
     */
    virtual bool operator==(const ExternalValueBase & b) const;

    /* Print the value as JSON. Defaults to unconvertable, i.e. throws an error */
    virtual void printValueAsJSON(EvalState & state, bool strict,
        JSONPlaceholder & out, PathSet & context) const;

    /* Print the value as XML. Defaults to unevaluated */
    virtual void printValueAsXML(EvalState & state, bool strict, bool location,
        XMLWriter & doc, PathSet & context, PathSet & drvsSeen) const;

    virtual ~ExternalValueBase()
    {
    };
};

std::ostream & operator << (std::ostream & str, const ExternalValueBase & v);


struct Value
{
    ValueType type;

    inline void setInt() { type = tInt; };
    inline void setBool() { type = tBool; };
    inline void setString() { type = tString; };
    inline void setPath() { type = tPath; };
    inline void setNull() { type = tNull; };
    inline void setAttrs() { type = tAttrs; };
    inline void setList1() { type = tList1; };
    inline void setList2() { type = tList2; };
    inline void setListN() { type = tListN; };
    inline void setThunk() { type = tThunk; };
    inline void setApp() { type = tApp; };
    inline void setLambda() { type = tLambda; };
    inline void setBlackhole() { type = tBlackhole; };
    inline void setPrimOp() { type = tPrimOp; };
    inline void setPrimOpApp() { type = tPrimOpApp; };
    inline void setExternal() { type = tExternal; };
    inline void setFloat() { type = tFloat; };

    union
    {
        NixInt integer;
        bool boolean;

        /* Strings in the evaluator carry a so-called `context' which
           is a list of strings representing store paths.  This is to
           allow users to write things like

             "--with-freetype2-library=" + freetype + "/lib"

           where `freetype' is a derivation (or a source to be copied
           to the store).  If we just concatenated the strings without
           keeping track of the referenced store paths, then if the
           string is used as a derivation attribute, the derivation
           will not have the correct dependencies in its inputDrvs and
           inputSrcs.

           The semantics of the context is as follows: when a string
           with context C is used as a derivation attribute, then the
           derivations in C will be added to the inputDrvs of the
           derivation, and the other store paths in C will be added to
           the inputSrcs of the derivations.

           For canonicity, the store paths should be in sorted order. */
        struct {
            const char * s;
            const char * * context; // must be in sorted order
        } string;

        const char * path;
        Bindings * attrs;
        struct {
            size_t size;
            Value * * elems;
        } bigList;
        Value * smallList[2];
        struct {
            Env * env;
            Expr * expr;
        } thunk;
        struct {
            Value * left, * right;
        } app;
        struct {
            Env * env;
            ExprLambda * fun;
        } lambda;
        PrimOp * primOp;
        struct {
            Value * left, * right;
        } primOpApp;
        ExternalValueBase * external;
        NixFloat fpoint;
    };

    // Returns the normal type of a Value. This only returns nThunk if the
    // Value hasn't been forceValue'd
    inline NormalType normalType() const
    {
        switch (type) {
            case tInt: return nInt;
            case tBool: return nBool;
            case tString: return nString;
            case tPath: return nPath;
            case tNull: return nNull;
            case tAttrs: return nAttrs;
            case tList1: case tList2: case tListN: return nList;
            case tLambda: case tPrimOp: case tPrimOpApp: return nFunction;
            case tExternal: return nExternal;
            case tFloat: return nFloat;
            case tThunk: case tApp: case tBlackhole: return nThunk;
        }
        abort();
    }

    bool isList() const
    {
        return type == tList1 || type == tList2 || type == tListN;
    }

    Value * * listElems()
    {
        return type == tList1 || type == tList2 ? smallList : bigList.elems;
    }

    const Value * const * listElems() const
    {
        return type == tList1 || type == tList2 ? smallList : bigList.elems;
    }

    size_t listSize() const
    {
        return type == tList1 ? 1 : type == tList2 ? 2 : bigList.size;
    }

    /* Check whether forcing this value requires a trivial amount of
       computation. In particular, function applications are
       non-trivial. */
    bool isTrivial() const;

    std::vector<std::pair<Path, std::string>> getContext();
};


/* After overwriting an app node, be sure to clear pointers in the
   Value to ensure that the target isn't kept alive unnecessarily. */
static inline void clearValue(Value & v)
{
    v.app.left = v.app.right = 0;
}


static inline void mkInt(Value & v, NixInt n)
{
    clearValue(v);
    v.setInt();
    v.integer = n;
}


static inline void mkFloat(Value & v, NixFloat n)
{
    clearValue(v);
    v.setFloat();
    v.fpoint = n;
}


static inline void mkBool(Value & v, bool b)
{
    clearValue(v);
    v.setBool();
    v.boolean = b;
}


static inline void mkNull(Value & v)
{
    clearValue(v);
    v.setNull();
}


static inline void mkApp(Value & v, Value & left, Value & right)
{
    v.setApp();
    v.app.left = &left;
    v.app.right = &right;
}


static inline void mkPrimOpApp(Value & v, Value & left, Value & right)
{
    v.setPrimOpApp();
    v.app.left = &left;
    v.app.right = &right;
}


static inline void mkStringNoCopy(Value & v, const char * s)
{
    v.setString();
    v.string.s = s;
    v.string.context = 0;
}


static inline void mkString(Value & v, const Symbol & s)
{
    mkStringNoCopy(v, ((const string &) s).c_str());
}


void mkString(Value & v, const char * s);


static inline void mkPathNoCopy(Value & v, const char * s)
{
    clearValue(v);
    v.setPath();
    v.path = s;
}


void mkPath(Value & v, const char * s);


#if HAVE_BOEHMGC
typedef std::vector<Value *, traceable_allocator<Value *> > ValueVector;
typedef std::map<Symbol, Value *, std::less<Symbol>, traceable_allocator<std::pair<const Symbol, Value *> > > ValueMap;
#else
typedef std::vector<Value *> ValueVector;
typedef std::map<Symbol, Value *> ValueMap;
#endif


/* A value allocated in traceable memory. */
typedef std::shared_ptr<Value *> RootValue;

RootValue allocRootValue(Value * v);

}
