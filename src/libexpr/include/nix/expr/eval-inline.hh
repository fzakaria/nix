#pragma once
///@file

#include "nix/expr/print.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-error.hh"
#include "nix/expr/eval-settings.hh"

namespace nix {

/**
 * Note: Various places expect the allocated memory to be zeroed.
 */
[[gnu::always_inline]]
inline void * allocBytes(size_t n)
{
    void * p;
    p = calloc(n, 1);
    if (!p) throw std::bad_alloc();
    return p;
}


[[gnu::always_inline]]
Value * EvalState::allocValue()
{
    void * p = allocBytes(sizeof(Value));
    nrValues++;
    return (Value *) p;
}


[[gnu::always_inline]]
Env & EvalState::allocEnv(size_t size)
{
    nrEnvs++;
    nrValuesInEnvs += size;

    Env * env;
    env = (Env *) allocBytes(sizeof(Env) + size * sizeof(Value *));

    /* We assume that env->values has been cleared by the allocator; maybeThunk() and lookupVar fromWith expect this. */

    return *env;
}


[[gnu::always_inline]]
void EvalState::forceValue(Value & v, const PosIdx pos)
{
    if (v.isThunk()) {
        Env * env = v.payload.thunk.env;
        assert(env || v.isBlackhole());
        Expr * expr = v.payload.thunk.expr;
        try {
            v.mkBlackhole();
            //checkInterrupt();
            if (env) [[likely]]
                expr->eval(*this, *env, v);
            else
                ExprBlackHole::throwInfiniteRecursionError(*this, v);
        } catch (...) {
            v.mkThunk(env, expr);
            tryFixupBlackHolePos(v, pos);
            throw;
        }
    }
    else if (v.isApp())
        callFunction(*v.payload.app.left, *v.payload.app.right, v, pos);
}


[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceAttrs(v, [&]() { return pos; }, errorCtx);
}


template <typename Callable>
[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, Callable getPos, std::string_view errorCtx)
{
    PosIdx pos = getPos();
    forceValue(v, pos);
    if (v.type() != nAttrs) {
        error<TypeError>(
            "expected a set but found %1%: %2%",
            showType(v),
            ValuePrinter(*this, v, errorPrintOptions)
        ).withTrace(pos, errorCtx).debugThrow();
    }
}


[[gnu::always_inline]]
inline void EvalState::forceList(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v, pos);
    if (!v.isList()) {
        error<TypeError>(
            "expected a list but found %1%: %2%",
            showType(v),
            ValuePrinter(*this, v, errorPrintOptions)
        ).withTrace(pos, errorCtx).debugThrow();
    }
}

[[gnu::always_inline]]
inline CallDepth EvalState::addCallDepth(const PosIdx pos) {
    if (callDepth > settings.maxCallDepth)
        error<EvalBaseError>("stack overflow; max-call-depth exceeded").atPos(pos).debugThrow();

    return CallDepth(callDepth);
};

}
