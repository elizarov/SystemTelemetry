#pragma once

#include <memory>
#include <type_traits>
#include <utility>

template <typename Signature> class FunctionRef;

template <typename Result, typename... Args> class FunctionRef<Result(Args...)> {
public:
    template <typename Callable>
        requires(!std::is_same_v<std::remove_cvref_t<Callable>, FunctionRef> &&
                    std::is_invocable_r_v<Result, Callable &&, Args...>)
    FunctionRef(Callable&& callable) :
        context_(const_cast<void*>(static_cast<const void*>(std::addressof(callable)))),
        invoke_([](void* context, Args... args) -> Result {
            return (*static_cast<std::remove_reference_t<Callable>*>(context))(std::forward<Args>(args)...);
        }) {}

    Result operator()(Args... args) const {
        return invoke_(context_, std::forward<Args>(args)...);
    }

private:
    void* context_ = nullptr;
    Result (*invoke_)(void*, Args...) = nullptr;
};
