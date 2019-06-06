#pragma once
#if USE_FCONTEXT || (!defined(USE_FCONTEXT) && __has_include(<boost/context/detail/fcontext.hpp>))
#   undef USE_FCONTEXT
#   define USE_FCONTEXT 1
#   include <boost/assert.hpp>
#   include <boost/context/detail/fcontext.hpp>
#   ifdef NDEBUG
#       include <boost/context/fixedsize_stack.hpp>
#   else
#       include <boost/context/protected_fixedsize_stack.hpp>
#   endif
#elif USE_LIBACO
#   include "libaco/aco.h"
#else
#   if USE_WINFIB || (!defined(USE_WINFIB) && defined(_WIN32))
#       undef USE_WINFIB
#       define USE_WINFIB 1
#       ifdef _WIN32_WINNT
#           if _WIN32_WINNT < 0x0601
#               error 需要 Windows 7 以上系统支持
#           endif
#       else
#           define _WIN32_WINNT 0x0601
#       endif
#       ifdef WIN32_LEAD_AND_MEAN
#           include <Windows.h>
#       else
#           define WIN32_LEAD_AND_MEAN 1
#           include <Windows.h>
#           undef WIN32_LEAD_AND_MEAN
#       endif
#   elif USE_UCONTEXT || (!defined(USE_UCONTEXT) && !defined(__ANDROID__) && __has_include(<ucontext.h>))
#       undef USE_UCONTEXT
#       define USE_UCONTEXT 1
#       if defined(__APPLE__)
#           define _XOPEN_SOURCE
#       endif
#       include <signal.h>
#       include <ucontext.h>
#   elif USE_SJLJ || (!defined(USE_SJLJ) && __has_include(<unistd.h>))
#       define USE_SJLJ 1
#       include <setjmp.h>
#       include <signal.h>
#       include <unistd.h>
#   endif
#endif

#include <functional>
#include <cassert>
#include <iterator>
#include <iostream>
#include <array>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <optional>
#include <any>

namespace FiberSpace {
    enum class FiberStatus {
        unstarted = -1,
        running = 1,
        suspended = 2,
        closed = 0,
    };

    /** \brief 主纤程类析构异常类
     *
     * \warning 用户代码吃掉此异常可导致未定义行为。如果捕获到此异常，请转抛出去。
     */
    struct FiberReturn {
        template <typename, typename>
        friend class Fiber;

    private:
        FiberReturn() = default;
    };


#if USE_FCONTEXT
    namespace fctx = boost::context;
#endif

    /** \brief 主纤程类
     *
     * \warning 线程安全（？）
     * \tparam ValueType 子纤程返回类型
     * \tparam ValueType 纤程本地存储变量类型
     */
    template <typename ValueType = std::any, typename FiberStorageType = std::any>
    class Fiber {
    public:
        using FuncType = std::function<void (Fiber& fiber)>;

    private:
        Fiber(const Fiber &) = delete;
        Fiber& operator =(const Fiber &) = delete;

        /// \brief 存储子纤程抛出的异常
        std::exception_ptr eptr = nullptr;
        /// \brief 子纤程是否结束
        FiberStatus status = FiberStatus::unstarted;
        /// \brief 真子纤程入口，第一个参数传入纤程对象的引用
        FuncType func;

#if USE_UCONTEXT || USE_SJLJ
        struct alignas(16) StackBuf {
            uint8_t* get() { return this->buf; }
            size_t size() { return SIGSTKSZ; }

        private:
            uint8_t buf[SIGSTKSZ];
        };
#endif

        /// \brief 纤程信息
#if USE_FCONTEXT
        fctx::detail::fcontext_t ctx_main, ctx_fnew;
#   ifdef NDEBUG
        fctx::fixedsize_stack stack_allocator;
#   else
        fctx::protected_fixedsize_stack stack_allocator;
#endif
        fctx::stack_context fnew_stack;
#elif USE_LIBACO
        aco_share_stack_t* pNewStack;
        aco_t* main_co;
        aco_t* new_co;
#elif USE_WINFIB
        PVOID pMainFiber, pNewFiber;
#elif USE_UCONTEXT
        ::ucontext_t ctx_main, ctx_fnew;
        const std::unique_ptr<StackBuf> fnew_stack = std::make_unique<StackBuf>();
#elif USE_SJLJ
        ::sigjmp_buf buf_main, buf_new;
        const std::unique_ptr<StackBuf> fnew_stack = std::make_unique<StackBuf>();
        struct sigaction old_sa;
        thread_local void *that;
#endif

        static_assert(std::is_object_v<ValueType>, "Non-object type won't work");

        /// \brief 子纤程返回值
        std::optional<ValueType> currentValue;

    public:
        /** \brief 构造函数
         *
         * 把主线程转化为纤程，并创建一个子纤程
         *
         * \param f 子纤程入口
         */
        explicit Fiber(FuncType f) : func(std::move(f)) {
#if USE_FCONTEXT
            this->fnew_stack = this->stack_allocator.allocate();
            this->ctx_fnew = fctx::detail::make_fcontext(this->fnew_stack.sp, this->fnew_stack.size, fEntry);
#elif USE_LIBACO
            this->main_co = aco_get_co();
            if (!this->main_co) {
                // WARNING: We won't destroy main_co for performance reasons
                aco_thread_init(nullptr);
                this->main_co = aco_create(nullptr, nullptr, 0, nullptr, nullptr);
            }
            this->pNewStack = aco_share_stack_new(SIGSTKSZ);
            this->new_co = aco_create(this->main_co, this->pNewStack, 0, (aco_cofuncp_t)&fEntry, this);
#elif USE_WINFIB
            if (!IsThreadAFiber()) {
                // WARNING: We won't convert main fiber back to thread for performance reasons
                this->pMainFiber = ::ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
            } else {
                this->pMainFiber = ::GetCurrentFiber();
            }
            // default stack size
            this->pNewFiber = ::CreateFiberEx(0, 0, FIBER_FLAG_FLOAT_SWITCH, (LPFIBER_START_ROUTINE)&fEntry, this);
#elif USE_UCONTEXT
            ::getcontext(&this->ctx_fnew);
            this->ctx_fnew.uc_stack.ss_sp = this->fnew_stack.get();
            this->ctx_fnew.uc_stack.ss_size = this->fnew_stack->size();
            this->ctx_fnew.uc_link = &this->ctx_main;
            ::makecontext(&this->ctx_fnew, (void(*)())&fEntry, 1, this);
#elif USE_SJLJ
            ::stack_t sigstk, oldstk;
            sigstk.ss_sp = this->fnew_stack.get();
            sigstk.ss_size = this->fnew_stack->size();
            sigstk.ss_flags = 0;
            if (::sigaltstack(&sigstk, &oldstk)) {
                 std::perror("Error while set sigstk");
                 std::abort();
            }

            struct sigaction sa;
            sa.sa_flags = SA_ONSTACK;
            sa.sa_handler = fEntry;
            sigemptyset(&sa.sa_mask);
            if (::sigaction(SIGUSR2, &sa, &this->old_sa)) {
                std::perror("Error while installing a signal handler");
                std::abort();
            }

            if (!sigsetjmp(this->buf_main, 0)) {
                Fiber::that = this; // Android doesn't support sigqueue,
                if (::raise(SIGUSR2)) {
                    std::perror("Failed to queue the signal");
                    std::abort();
                }
            }

            if (::sigaltstack(&oldstk, nullptr)) {
                std::perror("Error while reset sigstk");
                std::abort();
            }

            ::sigset_t sa_mask;
            sigemptyset(&sa_mask);
            sigaddset(&sa_mask, SIGUSR2);
            if (::sigprocmask(SIG_UNBLOCK, &sa_mask, nullptr)) {
                std::perror("Error while reset sigprocmask");
                std::abort();
            }
#endif
        }

        template <class Fp, class ...Args,
            class = typename std::enable_if
                <
                    (sizeof...(Args) > 0)
                >::type
            >
        explicit Fiber(Fp&& f, Args&&... args)
                : Fiber(std::bind(std::forward<Fp>(f), std::placeholders::_1, std::forward<Args>(args)...)) {
            static_assert (std::is_invocable_v<Fp, Fiber<ValueType, FiberStorageType>&, Args...>,
                "Wrong callback argument type list or incompatible fiber type found");
        }

        /** \brief 析构函数
         *
         * 删除子纤程，并将主纤程转回线程
         *
         * \warning 主类析构时如子纤程尚未结束（return），则会在子纤程中抛出 FiberReturn 来确保子纤程函数内所有对象都被正确析构
         */
        ~Fiber() noexcept {
            if (!isFinished()) {
                return_();
            }

#if USE_FCONTEXT
            this->stack_allocator.deallocate(this->fnew_stack);
#elif USE_LIBACO
            aco_destroy(this->new_co);
            aco_share_stack_destroy(this->pNewStack);
#elif USE_WINFIB
            ::DeleteFiber(this->pNewFiber);
#endif
        }

        /** \brief 向子纤程内部抛出异常
         *
         * 程序流程转入子纤程，并在子纤程内部抛出异常
         *
         * \param eptr 需抛出异常的指针（可以通过 std::make_exception_ptr 获取）
         * \warning 子纤程必须尚未结束
         * \return 返回子纤程是否尚未结束
         */
        bool throw_(std::exception_ptr&& eptr) {
            assert(!isFinished());
            this->eptr = std::move(eptr);
            return next();
        }

        /** \brief 强制退出子纤程
         *
         * 向子纤程内部抛出 FiberReturn 异常，以强制退出子纤程，并确保子纤程函数中所有对象都正确析构
         *
         * \warning 如果子纤程尚未开始或已经结束，本函数不做任何事情
         */
        void return_() {
            if (this->status == FiberStatus::unstarted || isFinished()) return;
            throw_(std::make_exception_ptr(FiberReturn()));
            assert(isFinished() && "请勿吃掉 FiberReturn 异常！！！");
        }

        /** \brief 判断子纤程是否结束
         * \return 子纤程已经结束(return)返回true，否则false
         */
        bool isFinished() const noexcept {
            return this->status == FiberStatus::closed;
        }

        /** \brief 重置当前值（currentValue）
         *
         * 也可以用来作为纤程返回值
         */
        void resetValue(std::optional<ValueType> value = std::nullopt) {
            this->currentValue = std::move(value);
        }

        /** \brief 调用子纤程
         *
         * 程序流程转入子纤程，并传入值（可通过 current() 获取）
         *
         * \param value 传入子纤程的值
         * \warning 子纤程必须尚未结束
         * \return 返回子纤程是否尚未结束
         */
        bool next(std::optional<ValueType> value = std::nullopt) {
            this->currentValue = std::move(value);
            this->jumpNew();
            return !isFinished();
        }

        /** \brief 获得子纤程返回的值
         * \warning 可能为空（nullopt）
         * \return 子纤程返回的值
         */
        const auto& current() const noexcept {
            return this->currentValue;
        }

        /** \brief 获得子纤程返回的值
         * \warning 可能为空（nullopt）
         * \return 子纤程返回的值
         */
        auto&& current() noexcept {
            return this->currentValue;
        }

        /** \brief 转回主纤程并输出值
         *
         * \warning 必须由子纤程调用
         *          参数类型必须与子纤程返回值相同，无类型安全
         * \param value 输出到主纤程的值
         */
        void yield(std::optional<ValueType> value = std::nullopt) {
            this->currentValue = std::move(value);
            this->jumpMain();
        }

        /** \brief 输出子纤程的所有值
         * \param fiber 另一子纤程
         */
        void yieldAll(Fiber& fiber) {
            assert(&fiber != this);
            while (fiber.next()) {
                this->yield(*fiber.current());
            }
        }

        void yieldAll(Fiber&& fiber) {
            this->yieldAll(fiber);
        }

    protected:
        /// \brief 控制流跳转主纤程
        void jumpMain() {
            assert(!isFinished());
            this->status = FiberStatus::suspended;

#if USE_FCONTEXT
            this->ctx_main = fctx::detail::jump_fcontext(this->ctx_main, this).fctx;
#elif USE_LIBACO
            aco_yield();
#elif USE_WINFIB
            assert(GetCurrentFiber() != this->pMainFiber && "这虽然是游戏，但绝不是可以随便玩的");
            ::SwitchToFiber(this->pMainFiber);
#elif USE_UCONTEXT
            ::swapcontext(&this->ctx_fnew, &this->ctx_main);
#elif USE_SJLJ
            if (!::sigsetjmp(this->buf_new, 0)) {
                ::siglongjmp(this->buf_main, 1);
            }
#endif
            // We are back to new coroutine now
            this->status = FiberStatus::running;

            if (this->eptr) {
                std::rethrow_exception(std::exchange(this->eptr, nullptr));
            }
        }

        /// \brief 控制流跳转子纤程
        void jumpNew() {
            assert(!isFinished());
#if USE_FCONTEXT
            this->ctx_fnew = fctx::detail::jump_fcontext(this->ctx_fnew, this).fctx;
#elif USE_LIBACO
            aco_resume(this->new_co);
#elif USE_WINFIB
            assert(GetCurrentFiber() != this->pNewFiber && "如果你想递归自己，请创建一个新纤程");
            ::SwitchToFiber(this->pNewFiber);
#elif USE_UCONTEXT
            ::swapcontext(&this->ctx_main, &this->ctx_fnew);
#elif USE_SJLJ
            if (!::sigsetjmp(this->buf_main, 0)) {
                ::siglongjmp(this->buf_new, 1);
            }
#endif
            // We are back to main coroutine now

            if (this->eptr) {
                std::rethrow_exception(std::exchange(this->eptr, nullptr));
            }
        }

        /// \brief 子纤程入口的warpper
#if USE_FCONTEXT
        static void fEntry(fctx::detail::transfer_t transfer) {
            auto *fiber = static_cast<Fiber *>(transfer.data);
            fiber->ctx_main = transfer.fctx;
#elif USE_LIBACO
        static void fEntry() {
            auto *fiber = static_cast<Fiber *>(aco_get_arg());
#elif USE_WINFIB
        static void WINAPI fEntry(Fiber *fiber) {
#elif USE_UCONTEXT
        static void fEntry(Fiber *fiber) {
#elif USE_SJLJ
        static void fEntry(int signo) {
            auto *fiber = static_cast<Fiber *>(std::exchange(Fiber::that, nullptr));
            if (::sigaction(signo, &fiber->old_sa, nullptr)) {
                std::perror("Failed to reset signal handler");
                std::abort();
            }
            if (!::sigsetjmp(fiber->buf_new, 0)) {
                ::siglongjmp(fiber->buf_main, 1);
            }
#endif

            if (!fiber->eptr) {
                fiber->status = FiberStatus::running;
                try {
                    fiber->func(*fiber);
                }
                catch (FiberReturn &) {
                    // 主 Fiber 对象正在析构
                }
//                catch (...) {
//                    fiber->eptr = std::current_exception();
//                }
            }
            fiber->status = FiberStatus::closed;
#if USE_FCONTEXT
            fiber->ctx_main = fctx::detail::jump_fcontext(fiber->ctx_main, fiber).fctx;
#elif USE_LIBACO
            aco_exit();
#elif USE_WINFIB
            ::SwitchToFiber(fiber->pMainFiber);
#elif USE_SJLJ
            ::siglongjmp(fiber->buf_main, 1);
#endif
        }

    public:
        /// \brief 纤程本地存储
        FiberStorageType localData;
    };

#if USE_SJLJ
    template <typename ValueType, typename FiberStorageType>
    void *Fiber<ValueType, FiberStorageType>::that;
#endif

    /** \brief 纤程迭代器类
     *
     * 它通过使用 yield 函数对数组或集合类执行自定义迭代。
     * 用于 C++11 for (... : ...)
     */
    template <typename ValueType, typename FiberStorageType>
    struct FiberIterator : std::iterator<std::output_iterator_tag, ValueType> {
        /// \brief 迭代器尾
        FiberIterator() noexcept : fiber(nullptr) {}
        /** \brief 迭代器首
         * \param _f 主线程类的引用
         */
        FiberIterator(Fiber<ValueType, FiberStorageType>& _f) : fiber(&_f) {
            next();
        }

        /// \brief 转入子纤程
        FiberIterator& operator ++() {
            next();
            return *this;
        }

        /// \brief 取得返回值
        const ValueType &operator *() const {
            assert(fiber != nullptr);
            return *fiber->current();
        }

        /** \brief 比较迭代器相等
         *
         * 通常用于判断迭代是否结束
         * 最好别干别的 ;P
         */
        bool operator ==(const FiberIterator& rhs) const noexcept {
            return fiber == rhs.fiber;
        }
        bool operator !=(const FiberIterator& rhs) const noexcept {
            return !(*this == rhs);
        }

    private:
        void next() {
            assert(fiber);
            if (!fiber->next()) fiber = nullptr;
        }

        Fiber<ValueType>* fiber;
    };

    /// \brief 返回迭代器首
    template <typename ValueType, typename FiberStorageType>
    auto begin(Fiber<ValueType, FiberStorageType>& fiber) {
        return FiberIterator<ValueType, FiberStorageType>(fiber);
    }

    /// \brief 返回迭代器尾
    template <typename ValueType, typename FiberStorageType>
    auto end(Fiber<ValueType, FiberStorageType>&) noexcept {
        return FiberIterator<ValueType, FiberStorageType>();
    }
}
