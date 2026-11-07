/* Authored TEB-fiber stack contract. No simulator or aircraft code is used. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FIBER_COUNT 4

typedef struct
{
    void *root_fiber;
    ULONG_PTR initial_base;
    ULONG_PTR initial_limit;
    ULONG_PTR before_write_limit;
    ULONG_PTR after_write_limit;
    ULONG_PTR guard_address;
    DWORD before_write_state;
    DWORD before_write_protect;
    DWORD after_write_state;
    DWORD after_write_protect;
    DWORD written_value;
    LONG ready;
} result;

static ULONG_PTR find_guard_below(ULONG_PTR limit)
{
    ULONG_PTR page;
    MEMORY_BASIC_INFORMATION info;
    for (page = limit; page > limit - 0x100000; page -= 0x1000)
    {
        if (VirtualQuery((void *)page, &info, sizeof(info)) &&
            info.State == MEM_COMMIT && (info.Protect & PAGE_GUARD))
            return page;
    }
    return 0;
}

static void WINAPI fiber_entry(void *arg)
{
    result *r = arg;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    MEMORY_BASIC_INFORMATION info;
    ULONG_PTR guard;
    volatile DWORD *target;

    r->initial_base = (ULONG_PTR)tib->StackBase;
    r->initial_limit = (ULONG_PTR)tib->StackLimit;
    guard = find_guard_below(r->initial_limit);
    if (!guard) goto done;

    /* This syscall return runs Wine's existing stack preflight on this fiber. */
    Sleep(0);
    r->before_write_limit = (ULONG_PTR)tib->StackLimit;
    guard = find_guard_below(r->before_write_limit);
    if (!guard || !VirtualQuery((void *)guard, &info, sizeof(info))) goto done;
    r->guard_address = guard;
    r->before_write_state = info.State;
    r->before_write_protect = info.Protect;
    printf("STACK base=%llx limit=%llx guard=%llx rsp=%llx region=%llx/%llx state=%lx protect=%lx\n",
           (unsigned long long)tib->StackBase, (unsigned long long)tib->StackLimit,
           (unsigned long long)guard, (unsigned long long)(ULONG_PTR)&info,
           (unsigned long long)(ULONG_PTR)info.BaseAddress,
           (unsigned long long)(ULONG_PTR)info.RegionSize,
           (unsigned long)info.State, (unsigned long)info.Protect);
    fflush(stdout);

    /* With the experiment enabled, the host page is RW while Windows still reports
     * PAGE_GUARD. With it disabled, normal guard handling consumes this page. */
    target = (volatile DWORD *)guard;
    *target = 0x78563412;
    r->written_value = *target;
    r->after_write_limit = (ULONG_PTR)tib->StackLimit;
    if (VirtualQuery((void *)guard, &info, sizeof(info)))
    {
        r->after_write_state = info.State;
        r->after_write_protect = info.Protect;
    }
done:
    InterlockedExchange(&r->ready, 1);
    SwitchToFiber(r->root_fiber);
}

int main(int argc, char **argv)
{
    result r[FIBER_COUNT];
    void *fibers[FIBER_COUNT];
    BOOL enabled;
    int i, pass = 1;
    if (argc != 2) return 2;
    enabled = !strcmp(argv[1], "enabled");
    if (!enabled && strcmp(argv[1], "disabled") && strcmp(argv[1], "wrong-target")) return 2;
    memset(r, 0, sizeof(r));
    r[0].root_fiber = ConvertThreadToFiber(&r[0]);
    if (!r[0].root_fiber) return 3;
    for (i = 0; i < FIBER_COUNT; i++)
    {
        r[i].root_fiber = r[0].root_fiber;
        fibers[i] = CreateFiberEx(0x80000, 0x180000, 0, fiber_entry, &r[i]);
        if (!fibers[i]) return 3;
    }
    for (i = 0; i < FIBER_COUNT; i++)
    {
        int fiber_pass;
        SwitchToFiber(fibers[i]);
        fiber_pass = r[i].ready && r[i].initial_base && r[i].initial_limit && r[i].guard_address &&
            r[i].before_write_state == MEM_COMMIT && (r[i].before_write_protect & PAGE_GUARD) &&
            r[i].written_value == 0x78563412 && r[i].after_write_state == MEM_COMMIT;
        if (fiber_pass && enabled)
            fiber_pass = r[i].after_write_limit == r[i].before_write_limit &&
                         r[i].after_write_protect == r[i].before_write_protect &&
                         (r[i].after_write_protect & PAGE_GUARD);
        printf("FIBER index=%d initial_limit=%llx prewrite_limit=%llx postwrite_limit=%llx guard=%llx protect=%lx/%lx value=%08lx PASS=%d\n",
               i, (unsigned long long)r[i].initial_limit,
               (unsigned long long)r[i].before_write_limit, (unsigned long long)r[i].after_write_limit,
               (unsigned long long)r[i].guard_address, (unsigned long)r[i].before_write_protect,
               (unsigned long)r[i].after_write_protect, (unsigned long)r[i].written_value, fiber_pass);
        pass &= fiber_pass;
        /* Returning to the root fiber ends this fiber's test; its stack remains
         * independent from the other TEB stacks below. */
        for (int j = 0; j <= i; j++) if (fibers[j]) DeleteFiber(fibers[j]);
        fibers[i] = NULL;
    }
    printf("CONTRACT mode=%s fibers=%d PASS=%d\n", argv[1], FIBER_COUNT, pass);
    ConvertFiberToThread();
    return pass && enabled ? 0 : 1;
}
