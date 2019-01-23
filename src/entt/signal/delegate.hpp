#ifndef ENTT_SIGNAL_DELEGATE_HPP
#define ENTT_SIGNAL_DELEGATE_HPP


#include <cassert>
#include <algorithm>
#include <functional>
#include <type_traits>
#include "../config/config.h"


namespace entt {


/**
 * @cond TURN_OFF_DOXYGEN
 * Internal details not to be documented.
 */


namespace internal {


template<typename Ret, typename... Args>
auto to_function_pointer(Ret(*)(Args...)) -> Ret(*)(Args...);


template<typename Ret, typename... Args, typename Type, typename Value>
auto to_function_pointer(Ret(*)(Type, Args...), Value) -> Ret(*)(Args...);


template<typename Class, typename Ret, typename... Args>
auto to_function_pointer(Ret(Class:: *)(Args...), Class *) -> Ret(*)(Args...);


template<typename Class, typename Ret, typename... Args>
auto to_function_pointer(Ret(Class:: *)(Args...) const, Class *) -> Ret(*)(Args...);


}


/**
 * Internal details not to be documented.
 * @endcond TURN_OFF_DOXYGEN
 */


/*! @brief Used to wrap a function or a member of a specified type. */
template<auto>
struct connect_arg_t {};


/*! @brief Constant of type connect_arg_t used to disambiguate calls. */
template<auto Func>
inline static connect_arg_t<Func> connect_arg{};


/**
 * @brief Basic delegate implementation.
 *
 * Primary template isn't defined on purpose. All the specializations give a
 * compile-time error unless the template parameter is a function type.
 */
template<typename>
class delegate;


/**
 * @brief Utility class to use to send around functions and members.
 *
 * Unmanaged delegate for function pointers and members. Users of this class are
 * in charge of disconnecting instances before deleting them.
 *
 * A delegate can be used as general purpose invoker with no memory overhead for
 * free functions and members provided along with an instance on which to invoke
 * them. It comes also with limited support for curried functions.
 *
 * @tparam Ret Return type of a function type.
 * @tparam Args Types of arguments of a function type.
 */
template<typename Ret, typename... Args>
class delegate<Ret(Args...)> {
    using storage_type = std::aligned_storage_t<sizeof(void *), alignof(void *)>;
    using proto_fn_type = Ret(storage_type &, Args...);

public:
    /*! @brief Function type of the delegate. */
    using function_type = Ret(Args...);

    /*! @brief Default constructor. */
    delegate() ENTT_NOEXCEPT
        : storage{}, fn{nullptr}
    {
        new (&storage) void *{nullptr};
    }

    /**
     * @brief Constructs a delegate and connects a free function to it.
     * @tparam Function A valid free function pointer.
     */
    template<auto Function>
    delegate(connect_arg_t<Function>) ENTT_NOEXCEPT
        : delegate{}
    {
        connect<Function>();
    }

    /**
     * @brief Constructs a delegate and connects a member for a given instance
     * or a curried free function to it.
     * @tparam Candidate Member or curried free function to connect to the
     * delegate.
     * @tparam Type Type of class to which the member belongs or type of value
     * used for currying.
     * @param value_or_instance A valid pointer to an instance of class type or
     * the value to use for currying.
     */
    template<auto Candidate, typename Type>
    delegate(connect_arg_t<Candidate>, Type value_or_instance) ENTT_NOEXCEPT
        : delegate{}
    {
        connect<Candidate>(value_or_instance);
    }

    /**
     * @brief Constructs a delegate and connects a lambda or a functor to it.
     * @tparam Invokable Type of lambda or functor to connect.
     * @param invokable A valid instance of the given type.
     */
    template<typename Invokable>
    delegate(Invokable invokable) ENTT_NOEXCEPT
        : delegate{}
    {
        connect(std::move(invokable));
    }

    /**
     * @brief Connects a free function to a delegate.
     * @tparam Function A valid free function pointer.
     */
    template<auto Function>
    void connect() ENTT_NOEXCEPT {
        static_assert(std::is_invocable_r_v<Ret, decltype(Function), Args...>);
        new (&storage) void *{nullptr};

        fn = [](storage_type &, Args... args) -> Ret {
            return std::invoke(Function, args...);
        };
    }

    /**
     * @brief Connects a member for a given instance or a curried free function
     * to a delegate.
     *
     * When used to connect a member, the delegate isn't responsible for the
     * connected object. Users must guarantee that the lifetime of the instance
     * overcomes the one of the delegate.<br/>
     * When used to connect a curried free function, the linked value must be
     * both trivially copyable and trivially destructible, other than such that
     * its size is lower than or equal to the one of a `void *`. It means that
     * all the primitive types are accepted as well as pointers. Moreover, the
     * signature of the free function must be such that the value is the first
     * argument before the ones used to define the delegate itself.
     *
     * @tparam Candidate Member or curried free function to connect to the
     * delegate.
     * @tparam Type Type of class to which the member belongs or type of value
     * used for currying.
     * @param value_or_instance A valid pointer to an instance of class type or
     * the value to use for currying.
     */
    template<auto Candidate, typename Type>
    void connect(Type value_or_instance) ENTT_NOEXCEPT {
        static_assert(sizeof(Type) <= sizeof(void *));
        static_assert(std::is_trivially_copyable_v<Type>);
        static_assert(std::is_trivially_destructible_v<Type>);
        static_assert(std::is_invocable_r_v<Ret, decltype(Candidate), Type &, Args...>);
        new (&storage) Type{value_or_instance};

        fn = [](storage_type &storage, Args... args) -> Ret {
            Type &value_or_instance = *reinterpret_cast<Type *>(&storage);
            return std::invoke(Candidate, value_or_instance, args...);
        };
    }

    /**
     * @brief Connects a lambda or a functor to a delegate.
     * @tparam Invokable Type of lambda or functor to connect.
     * @param invokable A valid instance of the given type.
     */
    template<typename Invokable>
    void connect(Invokable invokable) ENTT_NOEXCEPT {
        static_assert(sizeof(Invokable) < sizeof(void *));
        static_assert(std::is_class_v<Invokable>);
        static_assert(std::is_trivially_destructible_v<Invokable>);
        static_assert(std::is_invocable_r_v<Ret, Invokable, Args...>);
        new (&storage) Invokable{std::move(invokable)};

        fn = [](storage_type &storage, Args... args) -> Ret {
            Invokable &invokable = *reinterpret_cast<Invokable *>(&storage);
            return std::invoke(invokable, args...);
        };
    }

    /**
     * @brief Resets a delegate.
     *
     * After a reset, a delegate cannot be invoked anymore.
     */
    void reset() ENTT_NOEXCEPT {
        new (&storage) void *{nullptr};
        fn = nullptr;
    }

    /**
     * @brief Returns the instance linked to a delegate, if any.
     *
     * @warning
     * Attempting to use an instance returned by a delegate that doesn't contain
     * a pointer to a member results in undefined behavior.
     *
     * @return An opaque pointer to the instance linked to the delegate, if any.
     */
    const void * instance() const ENTT_NOEXCEPT {
        return *reinterpret_cast<const void **>(&storage);
    }

    /**
     * @brief Triggers a delegate.
     *
     * The delegate invokes the underlying function and returns the result.
     *
     * @warning
     * Attempting to trigger an invalid delegate results in undefined
     * behavior.<br/>
     * An assertion will abort the execution at runtime in debug mode if the
     * delegate has not yet been set.
     *
     * @param args Arguments to use to invoke the underlying function.
     * @return The value returned by the underlying function.
     */
    Ret operator()(Args... args) const {
        assert(fn);
        return fn(storage, args...);
    }

    /**
     * @brief Checks whether a delegate actually stores a listener.
     * @return False if the delegate is empty, true otherwise.
     */
    explicit operator bool() const ENTT_NOEXCEPT {
        // no need to test also data
        return fn;
    }

    /**
     * @brief Checks if the connected functions differ.
     *
     * In case of members, the instances connected to the delegate are not
     * verified by this operator. Use the `instance` member function instead.
     *
     * @param other Delegate with which to compare.
     * @return False if the connected functions differ, true otherwise.
     */
    bool operator==(const delegate<Ret(Args...)> &other) const ENTT_NOEXCEPT {
        return fn == other.fn;
    }

private:
    mutable storage_type storage;
    proto_fn_type *fn;
};


/**
 * @brief Checks if the connected functions differ.
 *
 * In case of members, the instances connected to the delegate are not verified
 * by this operator. Use the `instance` member function instead.
 *
 * @tparam Ret Return type of a function type.
 * @tparam Args Types of arguments of a function type.
 * @param lhs A valid delegate object.
 * @param rhs A valid delegate object.
 * @return True if the connected functions differ, false otherwise.
 */
template<typename Ret, typename... Args>
bool operator!=(const delegate<Ret(Args...)> &lhs, const delegate<Ret(Args...)> &rhs) ENTT_NOEXCEPT {
    return !(lhs == rhs);
}


/**
 * @brief Deduction guideline.
 *
 * It allows to deduce the function type of the delegate directly from a
 * function provided to the constructor.
 *
 * @tparam Function A valid free function pointer.
 */
template<auto Function>
delegate(connect_arg_t<Function>) ENTT_NOEXCEPT
-> delegate<std::remove_pointer_t<decltype(internal::to_function_pointer(Function))>>;


/**
 * @brief Deduction guideline.
 *
 * It allows to deduce the function type of the delegate directly from a member
 * or a curried free function provided to the constructor.
 *
 * @tparam Candidate Member or curried free function to connect to the delegate.
 * @tparam Type Type of class to which the member belongs or type of value used
 * for currying.
 */
template<auto Candidate, typename Type>
delegate(connect_arg_t<Candidate>, Type) ENTT_NOEXCEPT
-> delegate<std::remove_pointer_t<decltype(internal::to_function_pointer(Candidate, std::declval<Type>()))>>;


/**
 * @brief Deduction guideline.
 *
 * It allows to deduce the function type of the delegate directly from a lambda
 * or a functor provided to the constructor.
 *
 * @tparam Invokable Type of lambda or functor to connect.
 * @param invokable A valid instance of the given type.
 */
template<typename Invokable>
delegate(Invokable invokable) ENTT_NOEXCEPT
-> delegate<std::remove_pointer_t<decltype(internal::to_function_pointer(&Invokable::operator(), &invokable))>>;


}


#endif // ENTT_SIGNAL_DELEGATE_HPP
